/**
 * @file hsprov_hss_connection.cpp Implementation of HssConnection that uses HsProv
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "hsprov_hss_connection.h"
#include "cx.h" //TODO this is sad, but we need to fake up the DIAMETER_SUCCESS on UAR

namespace HssConnection {

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

// Column names in the IMPI_MAPPING column family
const static std::string IMPI_MAPPING_PREFIX = "associated_primary_impu__";

// Column names in the IMPI column family.
const static std::string ASSOC_PUBLIC_ID_COLUMN_PREFIX = "public_id_";
const static std::string DIGEST_HA1_COLUMN_NAME      ="digest_ha1";
const static std::string DIGEST_REALM_COLUMN_NAME    = "digest_realm";
const static std::string DIGEST_QOP_COLUMN_NAME      = "digest_qop";

// Column name marking rows created by homestead-prov
const static std::string EXISTS_COLUMN_NAME = "_exists";

std::string HsProvHssConnection::_configured_server_name;

template <class AnswerType>
void HsProvHssConnection::HsProvTransaction<AnswerType>::on_response(CassandraStore::Operation* op)
{
  update_latency_stats();
  AnswerType answer = create_answer(op);
  _response_clbk(answer);
}

template <class T>
void HsProvHssConnection::HsProvTransaction<T>::update_latency_stats()
{
  unsigned long latency = 0;
  if ((_stats_manager != NULL) && get_duration(latency))
  {
    // TODO
    // This should not be cache, this should be Cassandra latency
    _stats_manager->update_H_cache_latency_us(latency);
  }
}

MultimediaAuthAnswer HsProvHssConnection::MARHsProvTransaction::create_answer(CassandraStore::Operation* op)
{
  HsProvStore::GetAuthVector* get_av = (HsProvStore::GetAuthVector*)op;
  CassandraStore::ResultCode cass_result = op->get_result_code();

  ResultCode rc = ResultCode::SUCCESS;
  AuthVector* av = NULL;
  
  if (cass_result == CassandraStore::OK)
  {
    // HsProv uses DigestAuthVectors only
    DigestAuthVector temp_av;
    get_av->get_result(temp_av);
    av = new DigestAuthVector(temp_av);
  }
  else if (cass_result == CassandraStore::NOT_FOUND)
  {
    rc = ResultCode::NOT_FOUND;
  }
  else
  {
    TRC_DEBUG("HsProv query failed with rc %d", cass_result);

    // For any other error, we want Homestead to return a 504 so pretend there
    // was an upstream timeout
    rc = ResultCode::TIMEOUT;
  }

  MultimediaAuthAnswer maa = MultimediaAuthAnswer(rc,
                                                  av,
                                                  HssConnection::_scheme_digest);
  return maa;
}

LocationInfoAnswer HsProvHssConnection::LIRHsProvTransaction::create_answer(CassandraStore::Operation* op)
{
  HsProvStore::GetRegData* get_reg_data = (HsProvStore::GetRegData*)op;
  CassandraStore::ResultCode cass_result = op->get_result_code();
  ServerCapabilities capabilities;

  int32_t json_result = 0;
  std::string server_name;
  ResultCode rc = ResultCode::SUCCESS;

  if (cass_result == CassandraStore::OK)
  {
    std::string xml;
    get_reg_data->get_xml(xml);
    json_result = DIAMETER_SUCCESS;
    server_name = _configured_server_name;
  }
  else if (cass_result == CassandraStore::NOT_FOUND)
  {
    rc = ResultCode::NOT_FOUND;
  }
  else
  {
    TRC_DEBUG("HsProv query failed with rc %d", cass_result);

    // For any other error, we want Homestead to return a 504 so pretend there
    // was an upstream timeout
    rc = ResultCode::TIMEOUT;
  }

  LocationInfoAnswer lia = LocationInfoAnswer(rc,
                                              json_result,
                                              server_name,
                                              capabilities,
                                              "");
  return lia;
}

ServerAssignmentAnswer HsProvHssConnection::SARHsProvTransaction::create_answer(CassandraStore::Operation* op)
{
  HsProvStore::GetRegData* get_reg_data = (HsProvStore::GetRegData*)op;
  CassandraStore::ResultCode cass_result = op->get_result_code();

  // Now, parse into our generic SAA
  std::string service_profile;
  ChargingAddresses charging_addresses;
  ResultCode rc = ResultCode::SUCCESS;

  if (cass_result == CassandraStore::OK)
  {
    get_reg_data->get_xml(service_profile);
    get_reg_data->get_charging_addrs(charging_addresses);
  }
  else if (cass_result == CassandraStore::NOT_FOUND)
  {
    rc = ResultCode::NOT_FOUND;
  }
  else
  {
    TRC_DEBUG("HsProv query failed with rc %d", cass_result);

    // For any other error, we want Homestead to return a 504 so pretend there
    // was an upstream timeout
    rc = ResultCode::TIMEOUT;
  }

  ServerAssignmentAnswer saa = ServerAssignmentAnswer(rc,
                                                      charging_addresses,
                                                      service_profile,
                                                      "");
  return saa;
}

HsProvHssConnection::HsProvHssConnection(StatisticsManager* stats_manager,
                                         HsProvStore* store,
                                         std::string server_name) :
  HssConnection(stats_manager),
  _store(store)
{
  _configured_server_name = server_name;
}

// Send a multimedia auth request to the HSS
void HsProvHssConnection::send_multimedia_auth_request(maa_cb callback,
                                                       MultimediaAuthRequest request,
                                                       SAS::TrailId trail)
{
  // Create the CassandraTransaction that we'll use to send the request
  CassandraStore::Transaction* tsx = new MARHsProvTransaction(trail, callback, _stats_manager);

  // Create the CassandraStore::Operation that will actually get the info.
  CassandraStore::Operation* op = _store->create_GetAuthVector(request.impi, request.impu);

  // Get the info from Cassandra
  // (Note that the Store takes ownership of the Transaction and Operation from us)
  _store->do_async(op, tsx);
}

// Send a user auth request to the HSS
void HsProvHssConnection::send_user_auth_request(uaa_cb callback,
                                                 UserAuthRequest request,
                                                 SAS::TrailId trail)
{
  // We don't actually talk to Cassandra for a UAR, we just create and return
  // a faked response
  ServerCapabilities capabilities;
  UserAuthAnswer uaa = UserAuthAnswer(ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      _configured_server_name,
                                      capabilities);
  callback(uaa);
}

// Send a location info request to the HSS
void HsProvHssConnection::send_location_info_request(lia_cb callback,
                                                     LocationInfoRequest request,
                                                     SAS::TrailId trail)
{
  // Create the CassandraTransaction that we'll use to send the request
  CassandraStore::Transaction* tsx = new LIRHsProvTransaction(trail, callback, _stats_manager);

  // Create the CassandraStore::Operation that will actually get the info.
  CassandraStore::Operation* op = _store->create_GetRegData(request.impu);

  // Get the info from Cassandra
  // (Note that the Store takes ownership of the Transaction and Operation from us)
  _store->do_async(op, tsx);
}

// Send a server assignment request to the HSS
void HsProvHssConnection::send_server_assignment_request(saa_cb callback,
                                                         ServerAssignmentRequest request,
                                                         SAS::TrailId trail)
{
  if ((request.type == Cx::ServerAssignmentType::REGISTRATION) ||
      (request.type == Cx::ServerAssignmentType::RE_REGISTRATION) ||
      (request.type == Cx::ServerAssignmentType::UNREGISTERED_USER))
  {
    // If this is a (re-)registration or a call to an unregistered user, we have
    // to get the data from Cassandra

    // Create the CassandraTransaction that we'll use to send the request
    CassandraStore::Transaction* tsx = new SARHsProvTransaction(trail, callback, _stats_manager);

    // Create the CassandraStore::Operation that will actually get the info.
    CassandraStore::Operation* op = _store->create_GetRegData(request.impu);

    // Get the info from Cassandra
    // (Note that the Store takes ownership of the Transaction and Operation from us)
    _store->do_async(op, tsx);
  }
  else
  {
    // For any other type of SAR, there's nothing for Homestead-prov to do, so
    // we just create the SAA now
    // We don't need to specify any of the info in the SAA as it's going to be
    // deleted from the cache anyway
    ServerAssignmentAnswer saa = ServerAssignmentAnswer(ResultCode::SUCCESS);
    callback(saa);
  }
}
}; // namespace HssConnection