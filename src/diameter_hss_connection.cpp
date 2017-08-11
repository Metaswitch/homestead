/**
 * @file diameter_hss_connection.cpp Implementation of HssConnection that uses Diameter
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "diameter_hss_connection.h"



namespace HssConnection {

std::string DiameterHssConnection::_scheme_digest;
std::string DiameterHssConnection::_scheme_akav1;
std::string DiameterHssConnection::_scheme_akav2;

MultimediaAuthAnswer DiameterHssConnection::MARDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // First, create the Diameter MAA from the response
  Cx::MultimediaAuthAnswer diameter_maa = Cx::MultimediaAuthAnswer(rsp);

  // Now, parse into our generic MAA
  std::string auth_scheme;
  AuthVector* av = NULL;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_maa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_maa.experimental_result(experimental_result, vendor_id);

  TRC_DEBUG("Recieved MAA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

  if (result_code == DIAMETER_SUCCESS)
  {
    auth_scheme = diameter_maa.sip_auth_scheme();
    av = NULL;

    if (auth_scheme == DiameterHssConnection::_scheme_digest)
    {
      av = new DigestAuthVector(diameter_maa.digest_auth_vector());
    }
    else if (auth_scheme == DiameterHssConnection::_scheme_akav1)
    {
      av = new AKAAuthVector(diameter_maa.aka_auth_vector());
    }
    else if (auth_scheme == DiameterHssConnection::_scheme_akav2)
    {
      av = new AKAAuthVector(diameter_maa.akav2_auth_vector());
    }
    else
    {
      // TODO SAS log here
      rc = UNKNOWN_AUTH_SCHEME;
    }
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = SERVER_UNAVAILABLE;
  }
  else if (experimental_result == DIAMETER_ERROR_USER_UNKNOWN &&
           vendor_id == VENDOR_ID_3GPP)
  {
    rc = NOT_FOUND;
  }
  else
  {
    rc = UNKNOWN;
  }

  MultimediaAuthAnswer maa = MultimediaAuthAnswer(rc,
                                                  av,
                                                  auth_scheme);
  return maa;
}

UserAuthAnswer DiameterHssConnection::UARDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  //TODO healthchecker? in handlers.cpp before
  // First, create the Diameter UAA from the response
  Cx::UserAuthorizationAnswer diameter_uaa = Cx::UserAuthorizationAnswer(rsp);

  // Now, parse into our generic UAA
  int32_t json_result = 0;
  std::string server_name;
  ServerCapabilities* server_capabilities = NULL;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_uaa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_uaa.experimental_result(experimental_result, vendor_id);

  TRC_DEBUG("Recieved UAA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

  if ((result_code == DIAMETER_SUCCESS) ||
      (experimental_result == DIAMETER_FIRST_REGISTRATION) ||
      (experimental_result == DIAMETER_SUBSEQUENT_REGISTRATION))
  {
    json_result = result_code ? result_code : experimental_result;
    if (!diameter_uaa.server_name(server_name))
    {
      // If we don't have a server name, create the ServerCapabilities
      server_capabilities = new ServerCapabilities(diameter_uaa.server_capabilities());
    }
  }
  else if ((experimental_result == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result == DIAMETER_ERROR_IDENTITIES_DONT_MATCH))
  {
    rc = ResultCode::NOT_FOUND;
  }
  else if ((result_code == DIAMETER_AUTHORIZATION_REJECTED) ||
           (experimental_result == DIAMETER_ERROR_ROAMING_NOT_ALLOWED))
  {
    rc = ResultCode::FORBIDDEN;
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    rc = ResultCode::TIMEOUT;
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = ResultCode::SERVER_UNAVAILABLE;
  }
  else
  {
    rc = ResultCode::UNKNOWN;
  }

  UserAuthAnswer uaa = UserAuthAnswer(rc, json_result, server_name, server_capabilities);
  return uaa;
}

LocationInfoAnswer DiameterHssConnection::LIRDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // TODO results table
  // First, create the Diameter SAA from the response
  Cx::LocationInfoAnswer diameter_lia = Cx::LocationInfoAnswer(rsp);

  // Now, parse into our generic LIA
  int32_t json_result = 0;
  std::string server_name;
  ServerCapabilities* server_capabilities = NULL;
  std::string wildcard_impu;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_lia.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_lia.experimental_result(experimental_result, vendor_id);

  TRC_DEBUG("Recieved LIA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

 if ((result_code == DIAMETER_SUCCESS) ||
     ((vendor_id == VENDOR_ID_3GPP) &&
      ((experimental_result == DIAMETER_UNREGISTERED_SERVICE) ||
       (experimental_result == DIAMETER_ERROR_IDENTITY_NOT_REGISTERED))))
  {
    json_result = result_code ? result_code : experimental_result;

    // Get the server name
    if (!diameter_lia.server_name(server_name))
    {
      // If we don't have a server name, create the ServerCapabilities
      server_capabilities = new ServerCapabilities(diameter_lia.server_capabilities());
    }

    // Get the wildcard impu
    diameter_lia.wildcarded_public_identity(wildcard_impu);
  }
  else if ((vendor_id == VENDOR_ID_3GPP) &&
           (experimental_result == DIAMETER_ERROR_USER_UNKNOWN))
  {
    rc = ResultCode::NOT_FOUND;
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    rc = ResultCode::TIMEOUT;
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = ResultCode::SERVER_UNAVAILABLE;
  }
  else
  {
    rc = ResultCode::UNKNOWN;
  }

  LocationInfoAnswer lia = LocationInfoAnswer(rc,
                                              json_result,
                                              server_name,
                                              server_capabilities,
                                              wildcard_impu);
  return lia;
}

ServerAssignmentAnswer DiameterHssConnection::SARDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // TODO results table
  // First, create the Diameter SAA from the response
  Cx::ServerAssignmentAnswer diameter_saa = Cx::ServerAssignmentAnswer(rsp);

  // Now, parse into our generic SAA
  std::string service_profile;
  std::string wildcard_impu;
  ChargingAddresses charging_addresses;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_saa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_saa.experimental_result(experimental_result, vendor_id);

  TRC_DEBUG("Recieved SAA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

  if (result_code == DIAMETER_SUCCESS)
  {
    diameter_saa.charging_addrs(charging_addresses);
    diameter_saa.user_data(service_profile);
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = ResultCode::SERVER_UNAVAILABLE;
  }
  else if ((experimental_result == DIAMETER_ERROR_USER_UNKNOWN) &&
           (vendor_id == VENDOR_ID_3GPP))
  {
    rc = ResultCode::NOT_FOUND;
  }
  else if (experimental_result == DIAMETER_ERROR_IN_ASSIGNMENT_TYPE)
  {
    diameter_saa.wildcarded_public_identity(wildcard_impu);
    rc = ResultCode::NEW_WILDCARD;
  }
  else
  {
    rc = ResultCode::UNKNOWN;
  }

  ServerAssignmentAnswer saa = ServerAssignmentAnswer(rc,
                                                      charging_addresses,
                                                      service_profile,
                                                      wildcard_impu);
  return saa;
}

DiameterHssConnection::DiameterHssConnection(StatisticsManager* stats_manager,
                                             Cx::Dictionary* dict,
                                             Diameter::Stack* diameter_stack,
                                             const std::string& dest_realm,
                                             const std::string& dest_host,
                                             int diameter_timeout_ms) :
  HssConnection(stats_manager),
  _dict(dict),
  _diameter_stack(diameter_stack),
  _dest_realm(dest_realm),
  _dest_host(dest_host),
  _diameter_timeout_ms(diameter_timeout_ms)
{
}

// Send a multimedia auth request to the HSS
void DiameterHssConnection::send_multimedia_auth_request(maa_cb callback,
                                                         MultimediaAuthRequest request,
                                                         SAS::TrailId trail)
{
  //TODO latency stats?
  // TODO can't use the same _cx_results_table for all of them, we'll need a few
  // and each tsx will have to choose the correct one

  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  MARDiameterTransaction* tsx =
    new MARDiameterTransaction(_dict, trail, DIGEST_STATS, callback, _cx_results_tbl);

  Cx::MultimediaAuthRequest mar(_dict,
                                _diameter_stack,
                                _dest_realm,
                                _dest_host,
                                request.impi,
                                request.impu,
                                request.server_name,
                                request.scheme,
                                request.authorization);

  mar.send(tsx, _diameter_timeout_ms);
}

// Send a user auth request to the HSS
void DiameterHssConnection::send_user_auth_request(uaa_cb callback,
                                                   UserAuthRequest request,
                                                   SAS::TrailId trail)
{
  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  UARDiameterTransaction* tsx =
    new UARDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, _cx_results_tbl);

  Cx::UserAuthorizationRequest uar(_dict,
                                   _diameter_stack,
                                   _dest_host,
                                   _dest_realm,
                                   request.impi,
                                   request.impu,
                                   request.visited_network,
                                   request.authorization_type,
                                   request.emergency);

  uar.send(tsx, _diameter_timeout_ms);
}

// Send a location info request to the HSS
void DiameterHssConnection::send_location_info_request(lia_cb callback,
                                                       LocationInfoRequest request,
                                                       SAS::TrailId trail)
{
  LIRDiameterTransaction* tsx =
    new LIRDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, _cx_results_tbl);

  Cx::LocationInfoRequest lir(_dict,
                              _diameter_stack,
                              _dest_host,
                              _dest_realm,
                              request.originating,
                              request.impu,
                              request.authorization_type);

  lir.send(tsx, _diameter_timeout_ms);
}

// Send a server assignment request to the HSS
void DiameterHssConnection::send_server_assignment_request(saa_cb callback,
                                                           ServerAssignmentRequest request,
                                                           SAS::TrailId trail)
{
  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  SARDiameterTransaction* tsx =
    new SARDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, _cx_results_tbl);

  Cx::ServerAssignmentRequest sar(_dict,
                                  _diameter_stack,
                                  _dest_host,
                                  _dest_realm,
                                  request.impi,
                                  request.impu,
                                  request.server_name,
                                  request.type,
                                  request.support_shared_ifcs,
                                  request.wildcard_impu);

  sar.send(tsx, _diameter_timeout_ms);
}

}; // namespace HssConnection