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
  ResultCode rc;

  int32_t result_code = 0;
  diameter_maa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_maa.experimental_result(experimental_result, vendor_id);

  TRC_DEBUG("Recieved MAR from HSS with result code %d, experimental result code %d and vendor id %u",
            result_code,
            experimental_result,
            vendor_id);

  if (result_code == DIAMETER_SUCCESS)
  {
    auth_scheme = diameter_maa.sip_auth_scheme();
    av = NULL;

    if (auth_scheme == DiameterHssConnection::_scheme_digest)
    {
      rc = SUCCESS;
      av = new DigestAuthVector(diameter_maa.digest_auth_vector());
    }
    else if (auth_scheme == DiameterHssConnection::_scheme_akav1)
    {
      rc = SUCCESS;
      av = new AKAAuthVector(diameter_maa.aka_auth_vector());
    }
    else if (auth_scheme == DiameterHssConnection::_scheme_akav2)
    {
      rc = SUCCESS;
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
                                                   SAS::TrailId trail) {}

// Send a location info request to the HSS
void DiameterHssConnection::send_location_info_request(lia_cb callback,
                                                       LocationInfoRequest request,
                                                       SAS::TrailId trail) {}

// Send a server assignment request to the HSS
void DiameterHssConnection::send_server_assignment_request(saa_cb callback,
                                                           ServerAssignmentRequest request,
                                                           SAS::TrailId trail) {}

}; // namespace HssConnection