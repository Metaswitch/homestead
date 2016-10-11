/**
 * @file cache.cpp implementation of a cassandra-backed cache.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include <boost/format.hpp>

#include "cache.h"

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;
using namespace org::apache::cassandra;

// Keyspace and column family names.
const static std::string KEYSPACE = "homestead_cache";
const static std::string IMPI = "impi";
const static std::string IMPI_MAPPING = "impi_mapping";
const static std::string IMPU = "impu";

// Column names in the IMPU column family.
const static std::string IMS_SUB_XML_COLUMN_NAME = "ims_subscription_xml";
const static std::string REG_STATE_COLUMN_NAME = "is_registered";
const static std::string PRIMARY_CCF_COLUMN_NAME = "primary_ccf";
const static std::string SECONDARY_CCF_COLUMN_NAME = "secondary_ccf";
const static std::string PRIMARY_ECF_COLUMN_NAME = "primary_ecf";
const static std::string SECONDARY_ECF_COLUMN_NAME = "secondary_ecf";
const static std::string IMPI_COLUMN_PREFIX = "associated_impi__";
const static std::string IMPI_MAPPING_PREFIX = "associated_primary_impu__";

// Column names in the IMPI column family.
const static std::string ASSOC_PUBLIC_ID_COLUMN_PREFIX = "public_id_";
const static std::string DIGEST_HA1_COLUMN_NAME      ="digest_ha1";
const static std::string DIGEST_REALM_COLUMN_NAME    = "digest_realm";
const static std::string DIGEST_QOP_COLUMN_NAME      = "digest_qop";

// Column name marking rows created by homestead-prov
const static std::string EXISTS_COLUMN_NAME = "_exists";

// Variables to store the singleton cache object.
//
// Must create this after the constants above so that they have been
// initialized before we initialize the cache.
Cache* Cache::INSTANCE = &DEFAULT_INSTANCE;
Cache Cache::DEFAULT_INSTANCE;

//
// Cache methods
//

Cache::Cache() : CassandraStore::Store(KEYSPACE) {}

Cache::~Cache() {}


//
// PutRegData methods.
//

Cache::PutRegData::
PutRegData(const std::string& public_id,
           const int64_t timestamp,
           const int32_t ttl):
  CassandraStore::Operation(),
  _public_ids(1, public_id),
  _timestamp(timestamp),
  _ttl(ttl)
{}

Cache::PutRegData::
PutRegData(const std::vector<std::string>& public_ids,
           const int64_t timestamp,
           const int32_t ttl):
  CassandraStore::Operation(),
  _public_ids(public_ids),
  _timestamp(timestamp),
  _ttl(ttl)
{}

Cache::PutRegData::
~PutRegData()
{}

Cache::PutRegData& Cache::PutRegData::with_xml(const std::string& xml)
{
  _columns[IMS_SUB_XML_COLUMN_NAME] = xml;
  return *this;
}

Cache::PutRegData& Cache::PutRegData::with_reg_state(const RegistrationState reg_state)
{
  if (reg_state == RegistrationState::REGISTERED)
  {
    _columns[REG_STATE_COLUMN_NAME] = CassandraStore::BOOLEAN_TRUE;
  }
  else if (reg_state == RegistrationState::UNREGISTERED)
  {
    _columns[REG_STATE_COLUMN_NAME] = CassandraStore::BOOLEAN_FALSE;
  }
  else
  {
    // LCOV_EXCL_START - invalid case not hit in UT
    TRC_ERROR("Unexpected registration state %d", reg_state);
    // LCOV_EXCL_STOP
  }
  return *this;
}

Cache::PutRegData& Cache::PutRegData::with_associated_impis(const std::vector<std::string>& impis)
{
  for (std::vector<std::string>::const_iterator impi = impis.begin();
       impi != impis.end();
       impi++)
  {
    std::string column_name = IMPI_COLUMN_PREFIX + *impi;
    _columns[column_name] = "";

    std::map<std::string, std::string> impi_columns;
    std::vector<std::string>::iterator default_public_id = _public_ids.begin();
    impi_columns[IMPI_MAPPING_PREFIX + *default_public_id] = "";
    _to_put.push_back(CassandraStore::RowColumns(IMPI_MAPPING, *impi, impi_columns));
  }
  return *this;
}

Cache::PutRegData& Cache::PutRegData::with_charging_addrs(const ChargingAddresses& charging_addrs)
{
  if (charging_addrs.ccfs.empty())
  {
    _columns[PRIMARY_CCF_COLUMN_NAME] = "";
    _columns[SECONDARY_CCF_COLUMN_NAME] = "";
  }
  else if (charging_addrs.ccfs.size() == 1)
  {
    _columns[PRIMARY_CCF_COLUMN_NAME] = charging_addrs.ccfs[0];
    _columns[SECONDARY_CCF_COLUMN_NAME] = "";
  }
  else
  {
    _columns[PRIMARY_CCF_COLUMN_NAME] = charging_addrs.ccfs[0];
    _columns[SECONDARY_CCF_COLUMN_NAME] = charging_addrs.ccfs[1];
  }

  if (charging_addrs.ecfs.empty())
  {
    _columns[PRIMARY_ECF_COLUMN_NAME] = "";
    _columns[SECONDARY_ECF_COLUMN_NAME] = "";
  }
  else if (charging_addrs.ecfs.size() == 1)
  {
    _columns[PRIMARY_ECF_COLUMN_NAME] = charging_addrs.ecfs[0];
    _columns[SECONDARY_ECF_COLUMN_NAME] = "";
  }
  else
  {
    _columns[PRIMARY_ECF_COLUMN_NAME] = charging_addrs.ecfs[0];
    _columns[SECONDARY_ECF_COLUMN_NAME] = charging_addrs.ecfs[1];
  }
  return *this;
}

bool Cache::PutRegData::perform(CassandraStore::Client* client,
                                SAS::TrailId trail)
{
  for (std::vector<std::string>::iterator row = _public_ids.begin();
       row != _public_ids.end();
       row++)
  {
    _to_put.push_back(CassandraStore::RowColumns(IMPU, *row, _columns));
  }

  client->put_columns(_to_put, _timestamp, _ttl);

  return true;
}

//
// PutAssociatedPrivateID methods
//

Cache::PutAssociatedPrivateID::
PutAssociatedPrivateID(const std::vector<std::string>& impus,
                       const std::string& impi,
                       const int64_t timestamp,
                       const int32_t ttl) :
  CassandraStore::Operation(),
  _impus(impus),
  _impi(impi),
  _timestamp(timestamp),
  _ttl(ttl)
{}


Cache::PutAssociatedPrivateID::
~PutAssociatedPrivateID()
{}


bool Cache::PutAssociatedPrivateID::perform(CassandraStore::Client* client,
                                            SAS::TrailId trail)
{
  std::vector<CassandraStore::RowColumns> to_put;
  std::map<std::string, std::string> impu_columns;
  std::map<std::string, std::string> impi_columns;
  impu_columns[IMPI_COLUMN_PREFIX + _impi] = "";

  std::string default_public_id = _impus.front();
  impi_columns[IMPI_MAPPING_PREFIX + default_public_id] = "";
  to_put.push_back(CassandraStore::RowColumns(IMPI_MAPPING, _impi, impi_columns));

  for (std::vector<std::string>::iterator row = _impus.begin();
       row != _impus.end();
       row++)
  {
    to_put.push_back(CassandraStore::RowColumns(IMPU, *row, impu_columns));
  }

  client->put_columns(to_put, _timestamp, _ttl);

  return true;
}


//
// PutAssociatedPublicID methods.
//

Cache::PutAssociatedPublicID::
PutAssociatedPublicID(const std::string& private_id,
                      const std::string& assoc_public_id,
                      const int64_t timestamp,
                      const int32_t ttl) :
  CassandraStore::Operation(),
  _private_id(private_id),
  _assoc_public_id(assoc_public_id),
  _timestamp(timestamp),
  _ttl(ttl)
{}


Cache::PutAssociatedPublicID::
~PutAssociatedPublicID()
{}


bool Cache::PutAssociatedPublicID::perform(CassandraStore::Client* client,
                                           SAS::TrailId trail)
{
  std::map<std::string, std::string> columns;
  columns[ASSOC_PUBLIC_ID_COLUMN_PREFIX + _assoc_public_id] = "";

  std::vector<std::string> keys(1, _private_id);

  client->put_columns(IMPI, keys, columns, _timestamp, _ttl);
  return true;
}

//
// PutAuthVector methods.
//

Cache::PutAuthVector::
PutAuthVector(const std::string& private_id,
              const DigestAuthVector& auth_vector,
              const int64_t timestamp,
              const int32_t ttl) :
  CassandraStore::Operation(),
  _private_ids(1, private_id),
  _auth_vector(auth_vector),
  _timestamp(timestamp),
  _ttl(ttl)
{}


Cache::PutAuthVector::
~PutAuthVector()
{}


bool Cache::PutAuthVector::perform(CassandraStore::Client* client,
                                   SAS::TrailId trail)
{
  std::map<std::string, std::string> columns;
  columns[DIGEST_HA1_COLUMN_NAME]      = _auth_vector.ha1;
  columns[DIGEST_REALM_COLUMN_NAME]    = _auth_vector.realm;
  columns[DIGEST_QOP_COLUMN_NAME]      = _auth_vector.qop;

  client->put_columns(IMPI, _private_ids, columns, _timestamp, _ttl);
  return true;
}


//
// GetRegData methods
//

Cache::GetRegData::
GetRegData(const std::string& public_id) :
  CassandraStore::Operation(),
  _public_id(public_id),
  _xml(),
  _reg_state(RegistrationState::NOT_REGISTERED),
  _xml_ttl(0),
  _reg_state_ttl(0),
  _impis(),
  _charging_addrs()
{}


Cache::GetRegData::
~GetRegData()
{}


bool Cache::GetRegData::perform(CassandraStore::Client* client,
                                SAS::TrailId trail)
{
  int64_t now = generate_timestamp();
  TRC_DEBUG("Issuing get for key %s", _public_id.c_str());
  std::vector<ColumnOrSuperColumn> results;

  try
  {
    client->ha_get_all_columns(IMPU, _public_id, results, trail);

    for(std::vector<ColumnOrSuperColumn>::iterator it = results.begin(); it != results.end(); ++it)
    {
      if (it->column.name == IMS_SUB_XML_COLUMN_NAME)
      {
        _xml = it->column.value;

        // Cassandra timestamps are in microseconds (see
        // generate_timestamp) but TTLs are in seconds, so divide the
        // timestamps by a million.
        if (it->column.ttl > 0)
        {
          _xml_ttl = ((it->column.timestamp/1000000) + it->column.ttl) - (now / 1000000);
        };
        TRC_DEBUG("Retrieved XML column with TTL %d and value %s", _xml_ttl, _xml.c_str());
      }
      else if (it->column.name == REG_STATE_COLUMN_NAME)
      {
        if (it->column.ttl > 0)
        {
          _reg_state_ttl = ((it->column.timestamp/1000000) + it->column.ttl) - (now / 1000000);
        };
        if (it->column.value == CassandraStore::BOOLEAN_TRUE)
        {
          _reg_state = RegistrationState::REGISTERED;
          TRC_DEBUG("Retrieved is_registered column with value True and TTL %d",
                    _reg_state_ttl);
        }
        else if (it->column.value == CassandraStore::BOOLEAN_FALSE)
        {
          _reg_state = RegistrationState::UNREGISTERED;
          TRC_DEBUG("Retrieved is_registered column with value False and TTL %d",
                    _reg_state_ttl);
        }
        else if ((it->column.value == ""))
        {
          TRC_DEBUG("Retrieved is_registered column with empty value and TTL %d",
                    _reg_state_ttl);
        }
        else
        {
          TRC_WARNING("Registration state column has invalid value %d %s",
                      it->column.value.c_str()[0],
                      it->column.value.c_str());
        };
      }
      else if (it->column.name.find(IMPI_COLUMN_PREFIX) == 0)
      {
        std::string impi = it->column.name.substr(IMPI_COLUMN_PREFIX.length());
        _impis.push_back(impi);
      }
      else if ((it->column.name == PRIMARY_CCF_COLUMN_NAME) && (it->column.value != ""))
      {
        _charging_addrs.ccfs.push_front(it->column.value);
        TRC_DEBUG("Retrived primary_ccf column with value %s",
                  it->column.value.c_str());
      }
      else if ((it->column.name == SECONDARY_CCF_COLUMN_NAME) && (it->column.value != ""))
      {
        _charging_addrs.ccfs.push_back(it->column.value);
        TRC_DEBUG("Retrived secondary_ccf column with value %s",
                  it->column.value.c_str());
      }
      else if ((it->column.name == PRIMARY_ECF_COLUMN_NAME) && (it->column.value != ""))
      {
        _charging_addrs.ecfs.push_front(it->column.value);
        TRC_DEBUG("Retrived primary_ecf column with value %s",
                  it->column.value.c_str());
      }
      else if ((it->column.name == SECONDARY_ECF_COLUMN_NAME) && (it->column.value != ""))
      {
        _charging_addrs.ecfs.push_back(it->column.value);
        TRC_DEBUG("Retrived secondary_ecf column with value %s",
                  it->column.value.c_str());
      }
    }

    // If we're storing user data for this subscriber (i.e. there is
    // XML), then by definition they cannot be in NOT_REGISTERED state
    // - they must be in UNREGISTERED state.
    if ((_reg_state == RegistrationState::NOT_REGISTERED) && !_xml.empty())
    {
      TRC_DEBUG("Found stored XML for subscriber, treating as UNREGISTERED state");
      _reg_state = RegistrationState::UNREGISTERED;
    }

  }
  catch(CassandraStore::RowNotFoundException& rnfe)
  {
    // This is a valid state rather than an exceptional one, so we
    // catch the exception and return success. Values ae left in the
    // default state (NOT_REGISTERED and empty XML).
  }


  return true;
}

void Cache::GetRegData::get_xml(std::string& xml, int32_t& ttl)
{
  xml = _xml;
  ttl = _xml_ttl;
}

void Cache::GetRegData::get_associated_impis(std::vector<std::string>& associated_impis)
{
  associated_impis = _impis;
}


void Cache::GetRegData::get_registration_state(RegistrationState& reg_state, int32_t& ttl)
{
  reg_state = _reg_state;
  ttl = _reg_state_ttl;
}


void Cache::GetRegData::get_charging_addrs(ChargingAddresses& charging_addrs)
{
  charging_addrs = _charging_addrs;
}


void Cache::GetRegData::get_result(std::pair<RegistrationState, std::string>& result)
{
  RegistrationState state;
  int32_t reg_ttl;
  std::string xml;
  int32_t xml_ttl;

  get_registration_state(state, reg_ttl);
  get_xml(xml, xml_ttl);
  result.first = state;
  result.second = xml;
}


void Cache::GetRegData::get_result(Cache::GetRegData::Result& result)
{
  int32_t unused_ttl;

  get_registration_state(result.state, unused_ttl);
  get_xml(result.xml, unused_ttl);
  get_associated_impis(result.impis);
  get_charging_addrs(result.charging_addrs);
}


//
// GetAssociatedPublicIDs methods
//

Cache::GetAssociatedPublicIDs::
GetAssociatedPublicIDs(const std::string& private_id) :
  CassandraStore::Operation(),
  _private_ids(1, private_id),
  _public_ids()
{}


Cache::GetAssociatedPublicIDs::
GetAssociatedPublicIDs(const std::vector<std::string>& private_ids) :
  CassandraStore::Operation(),
  _private_ids(private_ids),
  _public_ids()
{}


Cache::GetAssociatedPublicIDs::
~GetAssociatedPublicIDs()
{}


bool Cache::GetAssociatedPublicIDs::perform(CassandraStore::Client* client,
                                            SAS::TrailId trail)
{
  std::map<std::string, std::vector<ColumnOrSuperColumn> > columns;
  std::set<std::string> public_ids;

  TRC_DEBUG("Looking for public IDs for private ID %s and %d others",
            _private_ids.front().c_str(),
            _private_ids.size());
  try
  {
    client->ha_multiget_columns_with_prefix(IMPI,
                                            _private_ids,
                                            ASSOC_PUBLIC_ID_COLUMN_PREFIX,
                                            columns,
                                            trail);
  }
  catch(CassandraStore::RowNotFoundException& rnfe)
  {
    TRC_INFO("Couldn't find any public IDs");
  }

  // Convert the query results from a vector of columns to a vector containing
  // the column names. The public_id prefix has already been stripped, so this
  // is just a list of public IDs and can be passed directly to on_success.
  for(std::map<std::string, std::vector<ColumnOrSuperColumn> >::const_iterator key_it = columns.begin();
      key_it != columns.end();
      ++key_it)
  {
    for(std::vector<ColumnOrSuperColumn>::const_iterator column = key_it->second.begin();
        column != key_it->second.end();
        ++column)
    {
      TRC_DEBUG("Found associated public ID %s", column->column.name.c_str());
      public_ids.insert(column->column.name);
    }
  }

  // Move the std::set of public_ids to the std::vector of _public_ids so that they
  // are available to the handler.
  std::copy(public_ids.begin(), public_ids.end(), std::back_inserter(_public_ids));

  return true;
}

void Cache::GetAssociatedPublicIDs::get_result(std::vector<std::string>& ids)
{
  ids = _public_ids;
}

//
// GetAssociatedPrimaryPublicIDs methods
//

Cache::GetAssociatedPrimaryPublicIDs::
GetAssociatedPrimaryPublicIDs(const std::string& private_id) :
  CassandraStore::Operation(),
  _private_ids(1, private_id),
  _public_ids()
{}

Cache::GetAssociatedPrimaryPublicIDs::
GetAssociatedPrimaryPublicIDs(const std::vector<std::string>& private_ids) :
  CassandraStore::Operation(),
  _private_ids(private_ids),
  _public_ids()
{}


bool Cache::GetAssociatedPrimaryPublicIDs::perform(CassandraStore::Client* client,
                                                   SAS::TrailId trail)
{
  std::set<std::string> public_ids_set;
  std::map<std::string, std::vector<ColumnOrSuperColumn> > columns;

  TRC_DEBUG("Looking for primary public IDs for private ID %s and %d others", _private_ids.front().c_str(), _private_ids.size());
  try
  {
    client->ha_multiget_columns_with_prefix(IMPI_MAPPING,
                                            _private_ids,
                                            IMPI_MAPPING_PREFIX,
                                            columns,
                                            trail);
  }
  catch(CassandraStore::RowNotFoundException& rnfe)
  {
    TRC_INFO("Couldn't find any public IDs");
  }

  // Convert the query results from a vector of columns to a vector containing
  // the column names. The public_id prefix has already been stripped, so this
  // is just a list of public IDs and can be passed directly to on_success.
  for(std::map<std::string, std::vector<ColumnOrSuperColumn> >::const_iterator key_it = columns.begin();
      key_it != columns.end();
      ++key_it)
  {
    for(std::vector<ColumnOrSuperColumn>::const_iterator column = key_it->second.begin();
        column != key_it->second.end();
        ++column)
    {
      TRC_DEBUG("Found associated public ID %s", column->column.name.c_str());
      public_ids_set.insert(column->column.name);
    }
  }
  std::copy(public_ids_set.begin(), public_ids_set.end(), std::back_inserter(_public_ids));
  return true;
}

void Cache::GetAssociatedPrimaryPublicIDs::get_result(std::vector<std::string>& ids)
{
  ids = _public_ids;
}

//
// GetAuthVector methods.
//

Cache::GetAuthVector::
GetAuthVector(const std::string& private_id) :
  CassandraStore::Operation(),
  _private_id(private_id),
  _public_id(""),
  _auth_vector()
{}


Cache::GetAuthVector::
GetAuthVector(const std::string& private_id,
              const std::string& public_id) :
  CassandraStore::Operation(),
  _private_id(private_id),
  _public_id(public_id),
  _auth_vector()
{}


Cache::GetAuthVector::
~GetAuthVector()
{}


bool Cache::GetAuthVector::perform(CassandraStore::Client* client,
                                   SAS::TrailId trail)
{
  TRC_DEBUG("Looking for authentication vector for %s", _private_id.c_str());
  std::vector<std::string> requested_columns;
  std::string public_id_col = "";
  bool public_id_requested = false;
  bool public_id_found = false;

  requested_columns.push_back(DIGEST_HA1_COLUMN_NAME);
  requested_columns.push_back(DIGEST_REALM_COLUMN_NAME);
  requested_columns.push_back(DIGEST_QOP_COLUMN_NAME);

  if (_public_id.length() > 0)
  {
    TRC_DEBUG("Checking public ID %s", _public_id.c_str());
    // We've been asked to verify the private ID has an associated public ID.
    // So request the public ID column as well.
    //
    // This is a dynamic column so we include it's prefix.
    public_id_col = ASSOC_PUBLIC_ID_COLUMN_PREFIX + _public_id;
    requested_columns.push_back(public_id_col);
    public_id_requested = true;
  }

  TRC_DEBUG("Issuing cache query");
  std::vector<ColumnOrSuperColumn> results;
  client->ha_get_columns(IMPI, _private_id, requested_columns, results, trail);

  for (std::vector<ColumnOrSuperColumn>::const_iterator it = results.begin();
       it != results.end();
       ++it)
  {
    const Column* col = &it->column;

    if (col->name == DIGEST_HA1_COLUMN_NAME)
    {
      _auth_vector.ha1 = col->value;
    }
    else if (col->name == DIGEST_REALM_COLUMN_NAME)
    {
      _auth_vector.realm = col->value;
    }
    else if (col->name == DIGEST_QOP_COLUMN_NAME)
    {
      _auth_vector.qop = col->value;
    }
    else if (col->name == public_id_col)
    {
      public_id_found = true;
    }
  }

  if (public_id_requested && !public_id_found)
  {
    // We were asked to verify a public ID, but that public ID that was not
    // found.  This is a failure.
    _cass_status = CassandraStore::NOT_FOUND;
    _cass_error_text = (boost::format(
                        "Private ID '%s' exists but does not have associated public ID '%s'")
                         % _private_id % _public_id).str();
    TRC_DEBUG("Cache query failed: %s", _cass_error_text.c_str());
    return false;
  }
  else if (_auth_vector.ha1 == "")
  {
    // The HA1 column was not found.  This cannot be defaulted so is an error.
    _cass_status = CassandraStore::NOT_FOUND;
    _cass_error_text = "HA1 column not found";
    TRC_DEBUG("Cache query failed: %s", _cass_error_text.c_str());
    return false;
  }
  else
  {
    return true;
  }
}

void Cache::GetAuthVector::get_result(DigestAuthVector& av)
{
  av = _auth_vector;
}

//
// DeletePublicIDs methods
//

Cache::DeletePublicIDs::
DeletePublicIDs(const std::string& public_id,
                const std::vector<std::string>& impis,
                int64_t timestamp) :
  CassandraStore::Operation(),
  _public_ids(1, public_id),
  _impis(impis),
  _timestamp(timestamp)
{}

Cache::DeletePublicIDs::
DeletePublicIDs(const std::vector<std::string>& public_ids,
                const std::vector<std::string>& impis,
                int64_t timestamp) :
  CassandraStore::Operation(),
  _public_ids(public_ids),
  _impis(impis),
  _timestamp(timestamp)
{}


Cache::DeletePublicIDs::
~DeletePublicIDs()
{}

bool Cache::DeletePublicIDs::perform(CassandraStore::Client* client,
                                     SAS::TrailId trail)
{
  std::vector<CassandraStore::RowColumns> to_delete;

  for (std::vector<std::string>::const_iterator it = _public_ids.begin();
       it != _public_ids.end();
       ++it)
  {
    // Don't specify columns as we're deleting the whole row
    to_delete.push_back(CassandraStore::RowColumns(IMPU, *it));
  }

  std::string primary_public_id = _public_ids.front();
  std::map<std::string, std::string> impi_columns_to_delete;
  impi_columns_to_delete[IMPI_MAPPING_PREFIX + primary_public_id] = "";

  for (std::vector<std::string>::const_iterator it = _impis.begin();
       it != _impis.end();
       ++it)
  {
    // Delete the column for this primary public ID from the IMPI
    // mapping table
    to_delete.push_back(CassandraStore::RowColumns(IMPI_MAPPING, *it, impi_columns_to_delete));
  }

  // Perform the batch deletion we've built up
  client->delete_columns(to_delete, _timestamp);

  return true;
}

//
// DeletePrivateIDs methods
//

Cache::DeletePrivateIDs::
DeletePrivateIDs(const std::string& private_id, int64_t timestamp) :
  CassandraStore::Operation(),
  _private_ids(1, private_id),
  _timestamp(timestamp)
{}


Cache::DeletePrivateIDs::
DeletePrivateIDs(const std::vector<std::string>& private_ids, int64_t timestamp) :
  CassandraStore::Operation(),
  _private_ids(private_ids),
  _timestamp(timestamp)
{}


Cache::DeletePrivateIDs::
~DeletePrivateIDs()
{}


bool Cache::DeletePrivateIDs::perform(CassandraStore::Client* client,
                                      SAS::TrailId trail)
{
  for (std::vector<std::string>::const_iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    client->delete_row(IMPI, *it, _timestamp);
  }

  return true;
}

//
// DeleteIMPIMapping methods
//

Cache::DeleteIMPIMapping::
DeleteIMPIMapping(const std::vector<std::string>& private_ids, int64_t timestamp) :
  CassandraStore::Operation(),
  _private_ids(private_ids),
  _timestamp(timestamp)
{}

bool Cache::DeleteIMPIMapping::perform(CassandraStore::Client* client,
                                       SAS::TrailId trail)
{
  std::vector<CassandraStore::RowColumns> to_delete;

  for (std::vector<std::string>::const_iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    // Don't specify columns as we're deleting the whole row
    to_delete.push_back(CassandraStore::RowColumns(IMPI_MAPPING, *it));
  }

  client->delete_columns(to_delete, _timestamp);
  return true;
}

//
// DissociateImplicitRegistrationSetFromImpi methods
//

Cache::DissociateImplicitRegistrationSetFromImpi::
DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                          const std::string& impi,
                                          int64_t timestamp) :
  CassandraStore::Operation(),
  _impus(impus),
  _timestamp(timestamp)
{
  _impis.push_back(impi);
}

Cache::DissociateImplicitRegistrationSetFromImpi::
DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                          const std::vector<std::string>& impis,
                                          int64_t timestamp) :
  CassandraStore::Operation(),
  _impus(impus),
  _impis(impis),
  _timestamp(timestamp)
{}

bool Cache::DissociateImplicitRegistrationSetFromImpi::perform(CassandraStore::Client* client,
                                                               SAS::TrailId trail)
{
  std::vector<CassandraStore::RowColumns> to_delete;

  // Go through IMPI mapping table and delete the columns

  std::string primary_public_id = _impus.front();

  std::map<std::string, std::string> impi_columns_to_delete;
  std::map<std::string, std::string> impu_columns_to_delete;

  // Value doesn't matter for deletions
  impi_columns_to_delete[IMPI_MAPPING_PREFIX + primary_public_id] = "";
  for (std::vector<std::string>::const_iterator it = _impis.begin();
       it != _impis.end();
       ++it)
  {
    TRC_DEBUG("Deleting association between primary public ID %s and IMPI %s", primary_public_id.c_str(), it->c_str());
    impu_columns_to_delete[IMPI_COLUMN_PREFIX + *it] = "";
    to_delete.push_back(CassandraStore::RowColumns(IMPI_MAPPING, *it, impi_columns_to_delete));
  }

  // Check how many IMPIs are associated with this implicit
  // registration set (all the columns are the same, so we only need
  // to check the first)

  std::vector<ColumnOrSuperColumn> columns;
  client->ha_get_columns_with_prefix(IMPU,
                                     primary_public_id,
                                     IMPI_COLUMN_PREFIX,
                                     columns,
                                     trail);
  TRC_DEBUG("%d IMPIs are associated with this IRS", columns.size());

  std::set<std::string> associated_impis_set;

  for (std::vector<ColumnOrSuperColumn>::const_iterator it = columns.begin();
       it != columns.end();
       ++it)
  {
    associated_impis_set.insert(it->column.name);
  }


  // Are any IMPIs in _impis but not in associated_impis_set? If so,
  // warn.

  std::vector<std::string> output;
  std::set_difference(_impis.begin(),
                      _impis.end(),
                      associated_impis_set.begin(),
                      associated_impis_set.end(),
                      std::back_inserter(output));

  TRC_DEBUG("Set difference: %d", output.size());

  if (output.size() > 0)
  {
    TRC_WARNING("DissociateImplicitRegistrationSetFromImpi was called but not all the provided IMPIs are associated with the IMPU");
  }

  //  Are we deleting all the associated impis?

  output.clear();
  std::set_intersection(_impis.begin(),
                        _impis.end(),
                        associated_impis_set.begin(),
                        associated_impis_set.end(),
                        std::back_inserter(output));
  TRC_DEBUG("Set intersection: %d %d", output.size(), associated_impis_set.size());

  bool deleting_all_impis = (output.size() == associated_impis_set.size());

  for (std::vector<std::string>::const_iterator it = _impus.begin();
       it != _impus.end();
       ++it)
  {
    if (!deleting_all_impis)
    {
      // Go through IMPU table and delete the column
      // specifically for this IMPI
      to_delete.push_back(CassandraStore::RowColumns(IMPU, *it, impu_columns_to_delete));
    }
    else
    {
      // Delete the IMPU rows completely by not specifying columns
      to_delete.push_back(CassandraStore::RowColumns(IMPU, *it));
    }
  }

  // Perform the batch deletion we've built up
  client->delete_columns(to_delete, _timestamp);

  return true;
}

//
// Operation that lists all the IMPUs in the impu table.
//

// The maximum number of IMPUs to request (and therefore return) on each
// database query.
const int MAX_IMPUS_TO_RETURN = 1000;

bool Cache::ListImpus::perform(CassandraStore::Client* client, SAS::TrailId trail)
{
  std::vector<KeySlice> key_slices;

  // Specify what column family (table) we want to work on.
  ColumnParent cparent;
  cparent.__set_column_family(IMPU);

  // Specify what columns we want. We ask for the reg_state and _exists columns.
  //
  // In Cassandra, deleted rows appear like rows with no columns. So we need to
  // ask for a column we know should exist (to determine if the row exists or
  // not). The only column that must exists is the subscription XML so fetch
  // this.
  //
  // TODO: The XML column contains a lot of data so it would be better to
  // instead write an `_exists` column to cassandra and use that to determine if
  // the row is present.
  SlicePredicate sp;
  sp.__set_column_names({IMS_SUB_XML_COLUMN_NAME});

  KeyRange kr;
  kr.__set_start_key(""); // Start from the beginning.
  kr.__set_end_key("");   // No end.
  kr.__set_count(MAX_IMPUS_TO_RETURN);

  client->get_range_slices(key_slices, cparent, sp, kr, ConsistencyLevel::ONE);

  for (const KeySlice& key_slice: key_slices)
  {
    // Only report an IMPU if some of its columns exist (see comment above).
    if (!key_slice.columns.empty())
    {
      _impus.push_back(key_slice.key);
    }
  }

  return true;
}
