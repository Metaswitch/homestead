/**
 * @file hsprov_store.cpp implementation of a cassandra-backed store.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <boost/format.hpp>

#include "hsprov_store.h"

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;
using namespace org::apache::cassandra;

// Keyspace and column family names.
const static std::string KEYSPACE = "homestead_cache";
const static std::string IMPI     = "impi";
const static std::string IMPU     = "impu";

// Column names in the IMPU column family.
const static std::string IMS_SUB_XML_COLUMN_NAME   = "ims_subscription_xml";
const static std::string PRIMARY_CCF_COLUMN_NAME   = "primary_ccf";
const static std::string SECONDARY_CCF_COLUMN_NAME = "secondary_ccf";
const static std::string PRIMARY_ECF_COLUMN_NAME   = "primary_ecf";
const static std::string SECONDARY_ECF_COLUMN_NAME = "secondary_ecf";

// Column names in the IMPI column family.
const static std::string ASSOC_PUBLIC_ID_COLUMN_PREFIX = "public_id_";
const static std::string DIGEST_HA1_COLUMN_NAME        = "digest_ha1";
const static std::string DIGEST_REALM_COLUMN_NAME      = "digest_realm";
const static std::string DIGEST_QOP_COLUMN_NAME        = "digest_qop";

// Column name marking rows created by homestead-prov
const static std::string EXISTS_COLUMN_NAME = "_exists";

// Variables to store the singleton cache object.
//
// Must create this after the constants above so that they have been
// initialized before we initialize the cache.
HsProvStore* HsProvStore::INSTANCE = &DEFAULT_INSTANCE;
HsProvStore HsProvStore::DEFAULT_INSTANCE;

//
// HsProvStore methods
//

HsProvStore::HsProvStore() : CassandraStore::Store(KEYSPACE) {}

HsProvStore::~HsProvStore() {}

//
// GetRegData methods
//

HsProvStore::GetRegData::
GetRegData(const std::string& public_id) :
  CassandraStore::HAOperation(),
  _public_id(public_id),
  _xml(),
  _charging_addrs()
{}


HsProvStore::GetRegData::
~GetRegData()
{}


bool HsProvStore::GetRegData::perform(CassandraStore::Client* client,
                                      SAS::TrailId trail)
{
  TRC_DEBUG("Issuing get for key %s", _public_id.c_str());

  std::vector<std::string> requested_columns = {
    IMS_SUB_XML_COLUMN_NAME,
    PRIMARY_CCF_COLUMN_NAME,
    SECONDARY_CCF_COLUMN_NAME,
    PRIMARY_ECF_COLUMN_NAME,
    SECONDARY_ECF_COLUMN_NAME
  };

  std::vector<ColumnOrSuperColumn> results;

  ha_get_columns(client, IMPU, _public_id, requested_columns, results, trail);

  for(std::vector<ColumnOrSuperColumn>::iterator it = results.begin(); it != results.end(); ++it)
  {
    if (it->column.name == IMS_SUB_XML_COLUMN_NAME)
    {
      _xml = it->column.value;
      TRC_DEBUG("Retrieved XML column with value %s", _xml.c_str());
    }
    else if ((it->column.name == PRIMARY_CCF_COLUMN_NAME) && (it->column.value != ""))
    {
      _charging_addrs.ccfs.push_front(it->column.value);
      TRC_DEBUG("Retrieved primary_ccf column with value %s",
                it->column.value.c_str());
    }
    else if ((it->column.name == SECONDARY_CCF_COLUMN_NAME) && (it->column.value != ""))
    {
      _charging_addrs.ccfs.push_back(it->column.value);
      TRC_DEBUG("Retrieved secondary_ccf column with value %s",
                it->column.value.c_str());
    }
    else if ((it->column.name == PRIMARY_ECF_COLUMN_NAME) && (it->column.value != ""))
    {
      _charging_addrs.ecfs.push_front(it->column.value);
      TRC_DEBUG("Retrieved primary_ecf column with value %s",
                it->column.value.c_str());
    }
    else if ((it->column.name == SECONDARY_ECF_COLUMN_NAME) && (it->column.value != ""))
    {
      _charging_addrs.ecfs.push_back(it->column.value);
      TRC_DEBUG("Retrieved secondary_ecf column with value %s",
                it->column.value.c_str());
    }
  }

  // All exceptions will rise up to the calling function, where the return code
  // will be set as unsuccessful.

  return true;
}

void HsProvStore::GetRegData::get_xml(std::string& xml)
{
  xml = _xml;
}

void HsProvStore::GetRegData::get_charging_addrs(ChargingAddresses& charging_addrs)
{
  charging_addrs = _charging_addrs;
}

void HsProvStore::GetRegData::get_result(HsProvStore::GetRegData::Result& result)
{
  get_xml(result.xml);
  get_charging_addrs(result.charging_addrs);
}

//
// GetAuthVector methods.
//

HsProvStore::GetAuthVector::
GetAuthVector(const std::string& private_id) :
  CassandraStore::HAOperation(),
  _private_id(private_id),
  _public_id(""),
  _auth_vector()
{}


HsProvStore::GetAuthVector::
GetAuthVector(const std::string& private_id,
              const std::string& public_id) :
  CassandraStore::HAOperation(),
  _private_id(private_id),
  _public_id(public_id),
  _auth_vector()
{}


HsProvStore::GetAuthVector::
~GetAuthVector()
{}


bool HsProvStore::GetAuthVector::perform(CassandraStore::Client* client,
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
  ha_get_columns(client, IMPI, _private_id, requested_columns, results, trail);

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
    TRC_DEBUG("HsProvStore query failed: %s", _cass_error_text.c_str());
    return false;
  }
  else if (_auth_vector.ha1 == "")
  {
    // The HA1 column was not found.  This cannot be defaulted so is an error.
    _cass_status = CassandraStore::NOT_FOUND;
    _cass_error_text = "HA1 column not found";
    TRC_DEBUG("HsProvStore query failed: %s", _cass_error_text.c_str());
    return false;
  }
  else
  {
    return true;
  }
}

void HsProvStore::GetAuthVector::get_result(DigestAuthVector& av)
{
  av = _auth_vector;
}