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

#include "charging_addresses.h"
#include "diameter_hss_connection.h"
#include "homesteadsasevent.h"
#include "servercapabilities.h"

namespace HssConnection {

static SNMP::CxCounterTable* mar_results_tbl;
static SNMP::CxCounterTable* sar_results_tbl;
static SNMP::CxCounterTable* uar_results_tbl;
static SNMP::CxCounterTable* lir_results_tbl;

template <class AnswerType>
void DiameterHssConnection::DiameterTransaction<AnswerType>::on_response(Diameter::Message& rsp)
{
  update_latency_stats();
  AnswerType answer = create_answer(rsp);
  _response_clbk(answer);
}

template <class T>
void DiameterHssConnection::DiameterTransaction<T>::increment_results(int32_t result,
                                                                      int32_t experimental,
                                                                      uint32_t vendor)
{
  // IMS mandates that exactly one of result code or experimental result code
  // will be set, so we can unambiguously assume that, if one is set, then the
  // other one won't be.
  if (result != 0)
  {
    _cx_results_tbl->increment(SNMP::DiameterAppId::BASE, result);
  }
  else if (experimental != 0 && vendor == VENDOR_ID_3GPP)
  {
    _cx_results_tbl->increment(SNMP::DiameterAppId::_3GPP, experimental);
  }
}

template <class AnswerType>
void DiameterHssConnection::DiameterTransaction<AnswerType>::on_timeout()
{
  TRC_INFO("Diameter timeout - translating to ResultCode::SERVER_UNAVAILABLE");

  update_latency_stats();

  // No result-code returned on timeout, so use 0.
  _cx_results_tbl->increment(SNMP::DiameterAppId::TIMEOUT, 0);

  // Call the callback with SERVER_UNAVAILABLE
  AnswerType answer = AnswerType(ResultCode::SERVER_UNAVAILABLE);
  _response_clbk(answer);
}

template <class T>
void DiameterHssConnection::DiameterTransaction<T>::update_latency_stats()
{
  if (_stats_manager != nullptr)
  {
    unsigned long latency = 0;
    if (get_duration(latency))
    {
      // We want to subtract the diameter latency time from our stopwatch
      if (_stopwatch != nullptr)
      {
        _stopwatch->subtract_time(latency);
      }

      if (_stat_updates & STAT_HSS_LATENCY)
      {
        _stats_manager->update_H_hss_latency_us(latency);
      }
      if (_stat_updates & STAT_HSS_DIGEST_LATENCY)
      {
        _stats_manager->update_H_hss_digest_latency_us(latency);
      }
      if (_stat_updates & STAT_HSS_SUBSCRIPTION_LATENCY)
      {
        _stats_manager->update_H_hss_subscription_latency_us(latency);
      }
    }
  }
}

template <class T>
void DiameterHssConnection::DiameterTransaction<T>::sas_log_hss_failure(int event_id,
                                                                        int32_t result_code,
                                                                        int32_t experimental_result_code)
{
  SAS::Event event(trail(), event_id, 0);
  event.add_static_param(result_code);
  event.add_static_param(experimental_result_code);
  SAS::report_event(event);
}

MultimediaAuthAnswer DiameterHssConnection::MarDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // First, create the Diameter MAA from the response
  Cx::MultimediaAuthAnswer diameter_maa = Cx::MultimediaAuthAnswer(rsp);

  // Now, parse into our generic MAA
  std::string auth_scheme;
  AuthVector* av = nullptr;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_maa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_maa.experimental_result(experimental_result, vendor_id);

  increment_results(result_code, experimental_result, vendor_id);

  TRC_DEBUG("Recieved MAA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

  if (result_code == DIAMETER_SUCCESS)
  {
    auth_scheme = diameter_maa.sip_auth_scheme();
    av = nullptr;

    if (auth_scheme == HssConnection::_scheme_digest)
    {
      av = diameter_maa.digest_auth_vector();
    }
    else if (auth_scheme == HssConnection::_scheme_akav1)
    {
      av = diameter_maa.aka_auth_vector();
    }
    else if (auth_scheme == HssConnection::_scheme_akav2)
    {
      av = diameter_maa.akav2_auth_vector();
    }
    else
    {
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
    TRC_INFO("Multimedia-Auth answer - user unknown");
    SAS::Event event(this->trail(), SASEvent::NO_AV_HSS, 0);
    SAS::report_event(event);
    rc = NOT_FOUND;
  }
  else
  {
    TRC_INFO("Multimedia-Auth answer with result code %d and experimental result code %d and vendor id %d",
             result_code, experimental_result, vendor_id);
    SAS::Event event(this->trail(), SASEvent::NO_AV_HSS, 0);
    SAS::report_event(event);
    rc = UNKNOWN;
  }

  return MultimediaAuthAnswer(rc,
                              av,
                              auth_scheme);
}

UserAuthAnswer DiameterHssConnection::UarDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // First, create the Diameter UAA from the response
  Cx::UserAuthorizationAnswer diameter_uaa = Cx::UserAuthorizationAnswer(rsp);

  // Now, parse into our generic UAA
  int32_t json_result = 0;
  std::string server_name;
  ServerCapabilities server_capabilities;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_uaa.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_uaa.experimental_result(experimental_result, vendor_id);

  increment_results(result_code, experimental_result, vendor_id);

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
      server_capabilities = diameter_uaa.server_capabilities();
    }
  }
  else if ((experimental_result == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result == DIAMETER_ERROR_IDENTITIES_DONT_MATCH))
  {
    sas_log_hss_failure(SASEvent::REG_STATUS_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::NOT_FOUND;
  }
  else if ((result_code == DIAMETER_AUTHORIZATION_REJECTED) ||
           (experimental_result == DIAMETER_ERROR_ROAMING_NOT_ALLOWED))
  {
    sas_log_hss_failure(SASEvent::REG_STATUS_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::FORBIDDEN;
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    sas_log_hss_failure(SASEvent::REG_STATUS_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::TIMEOUT;
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = ResultCode::SERVER_UNAVAILABLE;
  }
  else
  {
    sas_log_hss_failure(SASEvent::REG_STATUS_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::UNKNOWN;
  }

  return UserAuthAnswer(rc, json_result, server_name, server_capabilities);
}

LocationInfoAnswer DiameterHssConnection::LirDiameterTransaction::create_answer(Diameter::Message& rsp)
{
  // First, create the Diameter SAA from the response
  Cx::LocationInfoAnswer diameter_lia = Cx::LocationInfoAnswer(rsp);

  // Now, parse into our generic LIA
  int32_t json_result = 0;
  std::string server_name;
  ServerCapabilities server_capabilities;
  std::string wildcard_impu;
  ResultCode rc = ResultCode::SUCCESS;

  int32_t result_code = 0;
  diameter_lia.result_code(result_code);
  int32_t experimental_result = 0;
  uint32_t vendor_id = 0;
  diameter_lia.experimental_result(experimental_result, vendor_id);

  increment_results(result_code, experimental_result, vendor_id);

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
      server_capabilities = diameter_lia.server_capabilities();
    }

    // Get the wildcard impu
    diameter_lia.wildcarded_public_identity(wildcard_impu);
  }
  else if ((vendor_id == VENDOR_ID_3GPP) &&
           (experimental_result == DIAMETER_ERROR_USER_UNKNOWN))
  {
    sas_log_hss_failure(SASEvent::LOC_INFO_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::NOT_FOUND;
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    sas_log_hss_failure(SASEvent::LOC_INFO_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::TIMEOUT;
  }
  else if (result_code == DIAMETER_UNABLE_TO_DELIVER)
  {
    rc = ResultCode::SERVER_UNAVAILABLE;
  }
  else
  {
    sas_log_hss_failure(SASEvent::LOC_INFO_HSS_FAIL, result_code, experimental_result);
    rc = ResultCode::UNKNOWN;
  }

  return LocationInfoAnswer(rc,
                            json_result,
                            server_name,
                            server_capabilities,
                            wildcard_impu);
}

ServerAssignmentAnswer DiameterHssConnection::SarDiameterTransaction::create_answer(Diameter::Message& rsp)
{
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

  increment_results(result_code, experimental_result, vendor_id);

  TRC_DEBUG("Recieved SAA from HSS with result code %d, experimental result code %d",
            result_code,
            experimental_result);

  if (result_code == DIAMETER_SUCCESS)
  {
    SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_SUCCESS, 0);
    SAS::report_event(event);
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
    TRC_INFO("Server-Assignment answer - user unknown");
    SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_FAIL, 0);
    event.add_static_param(result_code);
    event.add_static_param(experimental_result);
    SAS::report_event(event);
    rc = ResultCode::NOT_FOUND;
  }
  else if (experimental_result == DIAMETER_ERROR_IN_ASSIGNMENT_TYPE)
  {
    diameter_saa.wildcarded_public_identity(wildcard_impu);
    if (!wildcard_impu.empty())
    {
      // The callback will handle tracking whether the wildcard has actually changed
      rc = ResultCode::NEW_WILDCARD;
    }
    else
    {
      // An error has been recieved in the SAA, and no wildcard returned
      int type = 0;
      diameter_saa.server_assignment_type(type);
      TRC_INFO("Server-Assignment answer with experimental result code "
               "DIAMETER_ERROR_IN_ASSIGNMENT_TYPE with vendor id %d and "
               "assignment type %d",
               vendor_id, type);
      SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_FAIL_ASSIGNMENT_TYPE, 0);

      // Present this case as a generic error
      rc = ResultCode::UNKNOWN;
    }
  }
  else
  {
    TRC_INFO("Server-Assignment answer with result code %d and experimental result code %d with vendor id %d",
             result_code, experimental_result, vendor_id);
    SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_FAIL, 0);
    event.add_static_param(result_code);
    event.add_static_param(experimental_result);
    SAS::report_event(event);
    rc = ResultCode::UNKNOWN;
  }

  return ServerAssignmentAnswer(rc,
                                charging_addresses,
                                service_profile,
                                wildcard_impu);
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
                                                         SAS::TrailId trail,
                                                         Utils::StopWatch* stopwatch)
{
  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  MarDiameterTransaction* tsx =
    new MarDiameterTransaction(_dict, trail, DIGEST_STATS, callback, mar_results_tbl, _stats_manager, stopwatch);

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
                                                   SAS::TrailId trail,
                                                   Utils::StopWatch* stopwatch)
{
  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  UarDiameterTransaction* tsx =
    new UarDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, uar_results_tbl, _stats_manager, stopwatch);

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
                                                       SAS::TrailId trail,
                                                       Utils::StopWatch* stopwatch)
{
  LirDiameterTransaction* tsx =
    new LirDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, lir_results_tbl, _stats_manager, stopwatch);

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
                                                           SAS::TrailId trail,
                                                           Utils::StopWatch* stopwatch)
{
  // Transactions are deleted in the DiameterStack's on_response or or_timeout,
  // so we don't have to delete this after sending
  SarDiameterTransaction* tsx =
    new SarDiameterTransaction(_dict, trail, SUBSCRIPTION_STATS, callback, sar_results_tbl, _stats_manager, stopwatch);

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

void configure_cx_results_tables(SNMP::CxCounterTable* mar_results_table,
                                 SNMP::CxCounterTable* sar_results_table,
                                 SNMP::CxCounterTable* uar_results_table,
                                 SNMP::CxCounterTable* lir_results_table)
{
  mar_results_tbl = mar_results_table;
  sar_results_tbl = sar_results_table;
  uar_results_tbl = uar_results_table;
  lir_results_tbl = lir_results_table;
}

}; // namespace HssConnection
