/**
 * @file http_handlers.cpp HTTP handlers for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "http_handlers.h"
#include "homestead_xml_utils.h"
#include "servercapabilities.h"
#include "homesteadsasevent.h"

#include "log.h"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidxml/rapidxml.hpp"
#include "boost/algorithm/string/join.hpp"
#include "base64.h"

using std::placeholders::_1;

const std::string SIP_URI_PRE = "sip:";

std::string HssCacheTask::_configured_server_name;
HssConnection::HssConnection* HssCacheTask::_hss = NULL;
HssCacheProcessor* HssCacheTask::_cache = NULL;
HealthChecker* HssCacheTask::_health_checker = NULL;

void HssCacheTask::configure_hss_connection(HssConnection::HssConnection* hss,
                                            std::string configured_server_name)
{
  _hss = hss;
  _configured_server_name = configured_server_name;
}

void HssCacheTask::configure_cache(HssCacheProcessor* cache)
{
  _cache = cache;
}

void HssCacheTask::configure_health_checker(HealthChecker* hc)
{
  _health_checker = hc;
}

// General IMPI handling.

void ImpiTask::run()
{
  if (parse_request())
  {
    TRC_DEBUG("Parsed HTTP request: private ID %s, public ID %s, scheme %s, authorization %s",
              _impi.c_str(), _impu.c_str(), _scheme.c_str(), _authorization.c_str());
    get_av();
  }
  else
  {
    send_http_reply(HTTP_NOT_FOUND);
    delete this;
  }
}

void ImpiTask::get_av()
{
  if (_impu.empty())
  {
    TRC_INFO("Public ID unknown - reject");
    SAS::Event event(this->trail(), SASEvent::NO_IMPU, 0);
    SAS::report_event(event);
    send_http_reply(HTTP_NOT_FOUND);
    delete this;
  }
  else
  {
    send_mar();
  }
}

void ImpiTask::send_mar()
{
  // Create the MAR to send to the hss
  HssConnection::MultimediaAuthRequest request = {
    _impi,
    _impu,
    (_provided_server_name == "" ? _configured_server_name :
     _provided_server_name),
    _scheme,
    _authorization
  };

  TRC_DEBUG("Requesting HSS Connection sends MAR");
  // Create the callback that will be invoked on a response
  HssConnection::maa_cb callback =
    std::bind(&ImpiTask::on_mar_response, this, _1);

  // Send the request
  _hss->send_multimedia_auth_request(callback, request, this->trail(), _req.get_stopwatch());
}

void ImpiTask::on_mar_response(const HssConnection::MultimediaAuthAnswer& maa)
{
  HssConnection::ResultCode rc = maa.get_result();
  TRC_DEBUG("Received Multimedia-Authorization answer with result code %d", rc);

  if (rc == HssConnection::ResultCode::SUCCESS)
  {
    std::string sip_auth_scheme = maa.get_scheme();
    if (sip_auth_scheme == _cfg->scheme_digest)
    {
      DigestAuthVector* av = (DigestAuthVector*)(maa.get_av());
      send_reply(*av);
    }
    else if (sip_auth_scheme == _cfg->scheme_akav1)
    {
      AKAAuthVector* av = (AKAAuthVector*)(maa.get_av());
      send_reply(*av);
    }
    else if (sip_auth_scheme == _cfg->scheme_akav2)
    {
      AKAAuthVector* av = (AKAAuthVector*)(maa.get_av());
      av->version = 2;
      send_reply(*av);
    }
    else
    {
      TRC_DEBUG("Unsupported auth scheme: %s", sip_auth_scheme.c_str());
      SAS::Event event(this->trail(), SASEvent::UNSUPPORTED_SCHEME, 0);
      event.add_var_param(sip_auth_scheme);
      event.add_var_param(_cfg->scheme_digest);
      event.add_var_param(_cfg->scheme_akav1);
      event.add_var_param(_cfg->scheme_akav2);
      SAS::report_event(event);
      send_http_reply(HTTP_NOT_FOUND);
    }
  }
  else if (rc == HssConnection::SERVER_UNAVAILABLE)
  {
    // LCOV_EXCL_START - nothing interesting to UT.
    // This may mean we don't have any Diameter connections. Another Homestead
    // node might have Diameter connections (either to the HSS, or to an SLF
    // which is able to talk to the HSS), and we should return a 503 so that
    // Sprout tries a different Homestead.
    send_http_reply(HTTP_SERVER_UNAVAILABLE);
    // LCOV_EXCL_STOP
  }
  else if (rc == HssConnection::ResultCode::NOT_FOUND)
  {
    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_NOT_FOUND);
  }
  else if (rc == HssConnection::ResultCode::TIMEOUT)
  {
    TRC_ERROR("Timeout error at HSS when attempting MAR - reject with 504");

    // We also record a penalty for the purposes of overload control
    record_penalty();

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  else
  {
    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_SERVER_ERROR);
  }

  delete this;
}

//
// IMPI digest handling.
//

bool ImpiDigestTask::parse_request()
{
  const std::string prefix = "/impi/";
  std::string path = _req.path();

  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impu = _req.param("public_id");
  _scheme = _cfg->scheme_digest;
  _authorization = "";
  _provided_server_name = _req.param(SERVER_NAME_FIELD);

  return true;
}

void ImpiDigestTask::send_reply(const DigestAuthVector& av)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  writer.String(JSON_DIGEST_HA1.c_str());
  writer.String(av.ha1.c_str());
  writer.EndObject();
  _req.add_content(sb.GetString());
  send_http_reply(HTTP_OK);
}

void ImpiDigestTask::send_reply(const AKAAuthVector& av)
{
  // It is an error to request AKA authentication through the digest URL.
  TRC_INFO("Digest requested but AKA received - reject");
  send_http_reply(HTTP_NOT_FOUND);
}

//
// IMPI AV handling.
//

bool ImpiAvTask::parse_request()
{
  const std::string prefix = "/impi/";
  std::string path = _req.path();

  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  std::string scheme = _req.file();
  if (scheme == "av")
  {
    _scheme = _cfg->scheme_unknown;
  }
  else if (scheme == "digest")
  {
    _scheme = _cfg->scheme_digest; // LCOV_EXCL_LINE - digests are handled by the ImpiDigestTask so we can't get here.
  }
  else if (scheme == "aka")
  {
    _scheme = _cfg->scheme_akav1;
  }
  else if (scheme == "aka2")
  {
    _scheme = _cfg->scheme_akav2;
  }
  else
  {
    TRC_INFO("Couldn't parse scheme %s", scheme.c_str());
    SAS::Event event(this->trail(), SASEvent::INVALID_SCHEME, 0);
    event.add_var_param(scheme);
    SAS::report_event(event);
    return false;
  }
  _impu = _req.param("impu");
  _authorization = base64_decode(_req.param(AUTH_FIELD_NAME));
  _provided_server_name = _req.param(SERVER_NAME_FIELD);

  return true;
}

void ImpiAvTask::send_reply(const DigestAuthVector& av)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

  // The qop value can be empty - in this case it should be replaced
  // with 'auth'.
  std::string qop_value = (!av.qop.empty()) ? av.qop : JSON_AUTH;

  writer.StartObject();
  {
    writer.String(JSON_DIGEST.c_str());
    writer.StartObject();
    {
      writer.String(JSON_HA1.c_str());
      writer.String(av.ha1.c_str());
      writer.String(JSON_REALM.c_str());
      writer.String(av.realm.c_str());
      writer.String(JSON_QOP.c_str());
      writer.String(qop_value.c_str());
    }
    writer.EndObject();
  }
  writer.EndObject();

  _req.add_content(sb.GetString());
  send_http_reply(HTTP_OK);
}

void ImpiAvTask::send_reply(const AKAAuthVector& av)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

  writer.StartObject();
  {
    writer.String(JSON_AKA.c_str());
    writer.StartObject();
    {
      writer.String(JSON_CHALLENGE.c_str());
      writer.String(av.challenge.c_str());
      writer.String(JSON_RESPONSE.c_str());
      writer.String(av.response.c_str());
      writer.String(JSON_CRYPTKEY.c_str());
      writer.String(av.crypt_key.c_str());
      writer.String(JSON_INTEGRITYKEY.c_str());
      writer.String(av.integrity_key.c_str());
      writer.String(JSON_VERSION.c_str());
      writer.Int(av.version);
    }
    writer.EndObject();
  }
  writer.EndObject();
  _req.add_content(sb.GetString());
  send_http_reply(HTTP_OK);
}

//
// IMPI Registration Status handling.
//
// A 200 OK response from this URL passes Homestead's health-check criteria.
//

void ImpiRegistrationStatusTask::run()
{
  const std::string prefix = "/impi/";
  std::string path = _req.path();
  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impu = _req.param("impu");
  _visited_network = _req.param("visited-network");
  if (_visited_network.empty())
  {
    _visited_network = _cfg->default_realm;
  }
  _authorization_type = _req.param("auth-type");
  std::string sos = _req.param("sos");
  _emergency = sos == "true" ? true : false;
  TRC_DEBUG("Parsed HTTP request: private ID %s, public ID %s, visited network %s, authorization type %s",
            _impi.c_str(), _impu.c_str(), _visited_network.c_str(), _authorization_type.c_str());

  // Create the request
  HssConnection::UserAuthRequest request = {
    _impi,
    _impu,
    _visited_network,
    _authorization_type,
    _emergency
  };

  // Create the callback that will be invoked on a response
  HssConnection::uaa_cb callback =
    std::bind(&ImpiRegistrationStatusTask::on_uar_response, this, _1);

  // Send the request
  _hss->send_user_auth_request(callback, request, this->trail(), _req.get_stopwatch());
}

void ImpiRegistrationStatusTask::on_uar_response(const HssConnection::UserAuthAnswer& uaa)
{
  HssConnection::ResultCode rc = uaa.get_result();
  TRC_DEBUG("Received User-Authorization answer with result %d", rc);

  if (rc == HssConnection::ResultCode::SUCCESS)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(uaa.get_json_result());

    std::string server_name = uaa.get_server();
    if (!server_name.empty())
    {
      // If we have a server name, use that
      TRC_DEBUG("Got Server-Name %s", server_name.c_str());
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
      TRC_DEBUG("Got Server-Capabilities");
      ServerCapabilities capabilities = uaa.get_server_capabilities();

      if (!capabilities.server_name.empty())
      {
        TRC_DEBUG("Got Server-Name %s from Capabilities AVP", capabilities.server_name.c_str());
        writer.String(JSON_SCSCF.c_str());
        writer.String(capabilities.server_name.c_str());
      }

      capabilities.write_capabilities(&writer);
    }

    writer.EndObject();

    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);

    if (_health_checker)
    {
      _health_checker->health_check_passed();
    }
  }
  else if (rc == HssConnection::ResultCode::NOT_FOUND)
  {
    TRC_INFO("User unknown or public/private ID conflict - reject");

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_NOT_FOUND);
  }
  else if (rc == HssConnection::ResultCode::FORBIDDEN)
  {
    TRC_INFO("Authorization rejected due to roaming not allowed - reject");

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_FORBIDDEN);
  }
  else if (rc == HssConnection::ResultCode::TIMEOUT)
  {
    TRC_ERROR("Timeout error at HSS when attempting UAR - reject with 504");

    // We also record a penalty for the purposes of overload control
    record_penalty();

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  else if (rc == HssConnection::ResultCode::SERVER_UNAVAILABLE)
  {
    // LCOV_EXCL_START - nothing interesting to UT.
    // This may mean we don't have any Diameter connections. Another Homestead
    // node might have Diameter connections (either to the HSS, or to an SLF
    // which is able to talk to the HSS), and we should return a 503 so that
    // Sprout tries a different Homestead.
    send_http_reply(HTTP_SERVER_UNAVAILABLE);
    // LCOV_EXCL_STOP
  }
  else
  {
    TRC_INFO("User-Authorization answer with result %d - reject", rc);

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_SERVER_ERROR);
  }

  delete this;
}

//
// IMPU Location Information handling
//

void ImpuLocationInfoTask::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.path();
  _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _originating = _req.param("originating");
  _authorization_type = _req.param("auth-type");
  TRC_DEBUG("Parsed HTTP request: public ID %s, originating %s, authorization type %s",
            _impu.c_str(), _originating.c_str(), _authorization_type.c_str());

  // Create the request
  HssConnection::LocationInfoRequest request = {
    _impu,
    _originating,
    _authorization_type
  };

  // Create the callback that will be invoked on a response
  HssConnection::lia_cb callback =
    std::bind(&ImpuLocationInfoTask::on_lir_response, this, _1);

  // Send the request
  _hss->send_location_info_request(callback, request, this->trail(), _req.get_stopwatch());
}

void ImpuLocationInfoTask::on_lir_response(const HssConnection::LocationInfoAnswer& lia)
{
  HssConnection::ResultCode rc = lia.get_result();
  TRC_DEBUG("Received Server-Assignment answer with result code %d", rc);

  if (rc == HssConnection::ResultCode::SUCCESS)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(lia.get_json_result());

    std::string server_name = lia.get_server();
    if (!server_name.empty())
    {
      // If we have a server name, use that
      TRC_DEBUG("Got Server-Name %s", server_name.c_str());
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
      TRC_DEBUG("Got Server-Capabilities");
      ServerCapabilities capabilities = lia.get_server_capabilities();
      if (!capabilities.server_name.empty())
      {
        TRC_DEBUG("Got Server-Name %s from Capabilities AVP", capabilities.server_name.c_str());
        writer.String(JSON_SCSCF.c_str());
        writer.String(capabilities.server_name.c_str());
      }

      capabilities.write_capabilities(&writer);
    }

    // If the HSS returned a wildcarded public user identity, add this to
    // the response.
    std::string wildcard_impu = lia.get_wildcard_impu();
    if (!wildcard_impu.empty())
    {
      TRC_DEBUG("Got Wildcarded-Public-Identity %s", wildcard_impu.c_str());
      writer.String(JSON_WILDCARD.c_str());
      writer.String(wildcard_impu.c_str());
    }

    writer.EndObject();
    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);
  }
  else if (rc == HssConnection::ResultCode::NOT_FOUND)
  {
    TRC_INFO("User unknown or public/private ID conflict - reject");

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_NOT_FOUND);
  }
  else if (rc == HssConnection::ResultCode::TIMEOUT)
  {
    TRC_ERROR("Timeout error at HSS when attempting LIR - reject with 504");

    // We also record a penalty for the purposes of overload control
    record_penalty();

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  else if (rc == HssConnection::ResultCode::SERVER_UNAVAILABLE)
  {
    // LCOV_EXCL_START - nothing interesting to UT.
    // This may mean we don't have any Diameter connections. Another Homestead
    // node might have Diameter connections (either to the HSS, or to an SLF
    // which is able to talk to the HSS), and we should return a 503 so that
    // Sprout tries a different Homestead.
    send_http_reply(HTTP_SERVER_UNAVAILABLE);
    // LCOV_EXCL_STOP
  }
  else
  {
    TRC_INFO("Location-Info answer with result %d - reject", rc);

    // SAS logging for the errors is the responsibility of the HssConnection
    send_http_reply(HTTP_SERVER_ERROR);
  }

  delete this;
}

//
// IMPU IMS Subscription handling for URLs of the form "/impu/<public ID>/reg-data"
//

// Determines whether an incoming HTTP request indicates deregistration
bool ImpuRegDataTask::is_deregistration_request(RequestType type)
{
  switch (type)
  {
    case RequestType::DEREG_USER:
    case RequestType::DEREG_ADMIN:
    case RequestType::DEREG_TIMEOUT:
      return true;
    default:
      return false;
  }
}

// Determines whether an incoming HTTP request indicates
// authentication failure
bool ImpuRegDataTask::is_auth_failure_request(RequestType type)
{
  switch (type)
  {
    case RequestType::DEREG_AUTH_FAIL:
    case RequestType::DEREG_AUTH_TIMEOUT:
      return true;
    default:
      return false;
  }
}

// If a HTTP request maps directly to a Diameter
// Server-Assignment-Type field, return the appropriate field.
Cx::ServerAssignmentType ImpuRegDataTask::sar_type_for_request(RequestType type)
{
  switch (type)
  {
    case RequestType::DEREG_USER:
      return Cx::ServerAssignmentType::USER_DEREGISTRATION;
    case RequestType::DEREG_ADMIN:
      return Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION;
    case RequestType::DEREG_TIMEOUT:
      return Cx::ServerAssignmentType::TIMEOUT_DEREGISTRATION;
    case RequestType::DEREG_AUTH_FAIL:
      return Cx::ServerAssignmentType::AUTHENTICATION_FAILURE;
    case RequestType::DEREG_AUTH_TIMEOUT:
      return Cx::ServerAssignmentType::AUTHENTICATION_TIMEOUT;
    default:
      // Should never be called for CALL or REG as they don't map to
      // an obvious value.

      // LCOV_EXCL_START
      TRC_ERROR("Couldn't produce an appropriate SAR - internal software error'");
      return Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION;
      // LCOV_EXCL_STOP
  }
}

ImpuRegDataTask::RequestType ImpuRegDataTask::request_type_from_body(std::string body)
{
  TRC_DEBUG("Determining request type from '%s'", body.c_str());
  RequestType ret = RequestType::UNKNOWN;

  std::string reqtype;
  rapidjson::Document document;
  document.Parse<0>(body.c_str());

  if (!document.IsObject() || !document.HasMember("reqtype") || !document["reqtype"].IsString())
  {
    TRC_ERROR("Did not receive valid JSON with a 'reqtype' element");
  }
  else
  {
    reqtype = document["reqtype"].GetString();
  }

  if (reqtype == "reg")
  {
    ret = RequestType::REG;
  }
  else if (reqtype == "call")
  {
    ret = RequestType::CALL;
  }
  else if (reqtype == "dereg-user")
  {
    ret = RequestType::DEREG_USER;
  }
  else if (reqtype == "dereg-admin")
  {
    ret = RequestType::DEREG_ADMIN;
  }
  else if (reqtype == "dereg-timeout")
  {
    ret = RequestType::DEREG_TIMEOUT;
  }
  else if (reqtype == "dereg-auth-failed")
  {
    ret = RequestType::DEREG_AUTH_FAIL;
  }
  else if (reqtype == "dereg-auth-timeout")
  {
    ret = RequestType::DEREG_AUTH_TIMEOUT;
  }

  TRC_DEBUG("New value of _type is %d", ret);
  return ret;
}

std::string ImpuRegDataTask::server_name_from_body(std::string body)
{
  rapidjson::Document document;
  document.Parse<0>(body.c_str());

  if (!document.IsObject() ||
      !document.HasMember("server_name") ||
      !document["server_name"].IsString())
  {
    TRC_DEBUG("Did not receive valid JSON with a 'server_name' element");
    return "";
  }
  else
  {
    return document["server_name"].GetString();
  }
}

std::string ImpuRegDataTask::wildcard_from_body(std::string body)
{
  rapidjson::Document document;
  document.Parse<0>(body.c_str());

  if (!document.IsObject() ||
      !document.HasMember("wildcard_identity") ||
      !document["wildcard_identity"].IsString())
  {
    TRC_DEBUG("Did not receive valid JSON with a 'wildcard_identity' element");
    return "";
  }
  else
  {
    return document["wildcard_identity"].GetString();
  }
}

void ImpuRegDataTask::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.full_path();

  _impu = Utils::url_unescape(path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length()));
  _impi = Utils::url_unescape(_req.param("private_id"));
  _provided_server_name = server_name_from_body(_req.get_rx_body());
  _sprout_wildcard = wildcard_from_body(_req.get_rx_body());

  TRC_DEBUG("Parsed HTTP request: private ID %s, public ID %s, server name %s",
            _impi.c_str(), _impu.c_str(), _provided_server_name.c_str());

  htp_method method = _req.method();

  // Police preconditions:
  //    - Method must either be GET or PUT
  //    - PUT requests must have a body of "reg", "call", "dereg-user"
  //   "dereg-admin", "dereg-timeout", "dereg-auth-failed" or
  //   "dereg-auth-timeout"

  if (method == htp_method_PUT)
  {
    _type = request_type_from_body(_req.get_rx_body());
    if (_type == RequestType::UNKNOWN)
    {
      TRC_ERROR("HTTP request contains invalid value %s for type", _req.get_rx_body().c_str());
      SAS::Event event(this->trail(), SASEvent::INVALID_REG_TYPE, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_BAD_REQUEST);
      delete this;
      return;
    }
  }
  else if (method == htp_method_GET)
  {
    _type = RequestType::UNKNOWN;
  }
  else
  {
    send_http_reply(HTTP_BADMETHOD);
    delete this;
    return;
  }

  // We must always get the data from the cache - even if we're doing
  // a deregistration, we'll need to use the existing private ID, and
  // need to return the iFCs to Sprout.
  get_reg_data();
}

void ImpuRegDataTask::get_reg_data()
{
  TRC_DEBUG("Try to find IMS Subscription information in the cache");
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA, 0);
  event.add_var_param(public_id());
  SAS::report_event(event);

  // Create the success and failure callbacks
  irs_success_callback success_cb =
    std::bind(&ImpuRegDataTask::on_get_reg_data_success, this, _1);

  failure_callback failure_cb =
    std::bind(&ImpuRegDataTask::on_get_reg_data_failure, this, _1);

  // Request the IRS from the cache
  _cache->get_implicit_registration_set_for_impu(success_cb,
                                                 failure_cb,
                                                 public_id(),
                                                 this->trail(),
                                                 _req.get_stopwatch());
}

std::string regstate_to_str(RegistrationState state)
{
  switch (state)
  {
  case REGISTERED:
    return "REGISTERED";
  case UNREGISTERED:
    return "UNREGISTERED";
  case NOT_REGISTERED:
    return "NOT_REGISTERED";
  default:
    return "???"; // LCOV_EXCL_LINE - unreachable
  }
}

void ImpuRegDataTask::on_get_reg_data_success(ImplicitRegistrationSet* irs)
{
  TRC_DEBUG("Got IMS subscription from cache");

  // We take ownership of the ImplicitRegistrationSet that the Cache has created
  _irs = irs;

  int32_t ttl = _irs->get_ttl();
  std::string service_profile = _irs->get_ims_sub_xml();
  RegistrationState reg_state = _irs->get_reg_state();
  std::vector<std::string> associated_impis = _irs->get_associated_impis();
  ChargingAddresses charging_addrs = _irs->get_charging_addresses();

  SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_SUCCESS, 0);
  event.add_compressed_param(service_profile, &SASEvent::PROFILE_SERVICE_PROFILE);
  event.add_static_param(reg_state);
  std::string associated_impis_str = boost::algorithm::join(associated_impis, ", ");
  event.add_var_param(associated_impis_str);
  event.add_var_param(charging_addrs.log_string());
  SAS::report_event(event);

  TRC_DEBUG("TTL for this database record is %d, IMS Subscription XML is %s, registration state is %s, and the charging addresses are %s",
            ttl,
            service_profile.empty() ? "empty" : "not empty",
            regstate_to_str(reg_state).c_str(),
            charging_addrs.empty() ? "empty" : charging_addrs.log_string().c_str());

  process_received_reg_data();
}

void ImpuRegDataTask::on_get_reg_data_failure(Store::Status rc)
{
  TRC_DEBUG("IMS subscription cache query failed: %d", rc);

  if (rc == Store::Status::NOT_FOUND)
  {
    // If this is a PUT request, then not finding the data in the cache yet is
    // expected if the subscriber isn't already registered.
    // We create an empty IRS and pretend we got it from the cache.
    if (_req.method() == htp_method_PUT)
    {
      TRC_DEBUG("No reg data found - creating empty IRS");
      SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_NOT_FOUND, 0);
      SAS::report_event(event);
      _irs = _cache->create_implicit_registration_set();
      _irs->set_reg_state(RegistrationState::NOT_REGISTERED);
      process_received_reg_data();
    }
    else
    {
      TRC_DEBUG("No IMS subscription found for public ID %s - reject", _impu.c_str());
      SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_FAIL, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_NOT_FOUND);
      delete this;
    }
  }
  else
  {
    // Send a 504 in all other cases (the request won't be retried)
    TRC_ERROR("Cache query failed with rc %d - reject with 504", rc);
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_FAIL, 0);
    SAS::report_event(event);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
    delete this;
  }
}

void ImpuRegDataTask::process_received_reg_data()
{
  // Common processing for when we either got an IRS from the cache or created a
  // blank one
  int32_t ttl = _irs->get_ttl();
  std::string service_profile = _irs->get_ims_sub_xml();
  _cached_reg_state = _irs->get_reg_state();
  std::vector<std::string> associated_impis = _irs->get_associated_impis();
  ChargingAddresses charging_addrs = _irs->get_charging_addresses();

  // GET requests shouldn't change the state - just respond with what we have in
  // the cache
  if (_req.method() == htp_method_GET)
  {
    send_reply();
    delete this;
    return;
  }

  bool new_binding = false;

  // If Sprout didn't specify a private Id on the request, we may have one
  // embedded in the cached User-Data which we can retrieve.
  // If Sprout did specify a private Id on the request, check whether we have a
  // record of this binding.
  if (_impi.empty())
  {
    _impi = XmlUtils::get_private_id(service_profile);
  }
  else if ((!service_profile.empty()) &&
           ((associated_impis.empty()) ||
            (std::find(associated_impis.begin(), associated_impis.end(), _impi) == associated_impis.end())))
  {
    TRC_DEBUG("Subscriber registering with new binding");
    new_binding = true;
  }

  // Work out whether we're allowed to answer only using the cache. If not we
  // will have to contact the HSS.
  bool cache_not_allowed = (_req.header("Cache-control") == "no-cache");

  if (_type == RequestType::REG)
  {
    // This message was based on a REGISTER request from Sprout. We need to
    // send an SAR to the HSS unless ALL of the following are true...
    // - this is a re-registration
    // - this is not a new binding
    // - the request hasn't forbidden us from using cached data
    // - the existing record hasn't timed out.
    if ((_cached_reg_state == RegistrationState::REGISTERED) && (!new_binding))
    {
      int record_age = _cfg->record_ttl - ttl;
      TRC_DEBUG("Handling re-registration with binding age of %d", record_age);
      _irs->set_reg_state(RegistrationState::REGISTERED);

      // We refresh the record's TTL everytime we receive an SAA from
      // the HSS. As such once the record is older than the HSS Reregistration
      // time, we need to send a new SAR to the HSS.
      //
      // Alternatively we need to notify the HSS if the HTTP request does not
      // allow cached responses.
      if (record_age >= _cfg->hss_reregistration_time)
      {
        TRC_DEBUG("Sending re-registration to HSS as %d seconds have passed",
                  record_age, _cfg->hss_reregistration_time);
        send_server_assignment_request(Cx::ServerAssignmentType::RE_REGISTRATION);
      }
      else if (cache_not_allowed)
      {
        TRC_DEBUG("Sending re-registration to HSS as cached responses are not allowed");
        send_server_assignment_request(Cx::ServerAssignmentType::RE_REGISTRATION);
      }
      else
      {
        // No state changes are required for a re-register if we're not
        // notifying a HSS - just respond.
        send_reply();
        delete this;
        return;
      }
    }
    else
    {
      // Set the registration state in the IRS to registered, add the new IMPI
      // and send a Server-Assignment-Request
      TRC_DEBUG("Handling initial registration");
      _irs->set_reg_state(RegistrationState::REGISTERED);
      _irs->add_associated_impi(_impi);
      send_server_assignment_request(Cx::ServerAssignmentType::REGISTRATION);
    }
  }
  else if (_type == RequestType::CALL)
  {
    // This message was based on an initial non-REGISTER request
    // (INVITE, PUBLISH, MESSAGE etc.).
    TRC_DEBUG("Handling call");

    if (_cached_reg_state == RegistrationState::NOT_REGISTERED)
    {
      // We don't know anything about this subscriber. Send a
      // Server-Assignment-Request to provide unregistered service for
      // this subscriber.
      TRC_DEBUG("Moving to unregistered state");
      _irs->set_reg_state(RegistrationState::UNREGISTERED);
      send_server_assignment_request(Cx::ServerAssignmentType::UNREGISTERED_USER);
    }
    else
    {
      // We're already assigned to handle this subscriber - respond with the
      // iFCs and whether they're in registered state or not.
      send_reply();
      delete this;
      return;
    }
  }
  else if (is_deregistration_request(_type))
  {
    // Sprout wants to deregister this subscriber (because of a
    // REGISTER with Expires: 0, a timeout of all bindings, a failed
    // app server, etc.).
    if (_cached_reg_state != RegistrationState::NOT_REGISTERED)
    {
      // Forget about this subscriber entirely and send an appropriate SAR.
      TRC_DEBUG("Handling deregistration");
      _irs->set_reg_state(RegistrationState::NOT_REGISTERED);
      send_server_assignment_request(sar_type_for_request(_type));
    }
    else
    {
      // We treat a deregistration for a deregistered user as an error
      // - this is useful for preventing loops, where we try and continually
      // deregister a user.
      TRC_DEBUG("Rejecting deregistration for user who was not registered");
      SAS::Event event(this->trail(), SASEvent::SUB_NOT_REG, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_BAD_REQUEST);
      delete this;
      return;
    }
  }
  else if (is_auth_failure_request(_type))
  {
    // Authentication failures don't change our state (if a user's already
    // registered, failing to log in with a new binding shouldn't deregister
    // them - if they're not registered and fail to log in, they're already in
    // the right state).

    // Notify the HSS, so that it removes the Auth-Pending flag.
    TRC_DEBUG("Handling authentication failure/timeout");
    send_server_assignment_request(sar_type_for_request(_type));
  }
  else
  {
    // LCOV_EXCL_START - unreachable
    TRC_ERROR("Invalid type %d", _type);
    delete this;
    return;
    // LCOV_EXCL_STOP - unreachable
  }
}

void ImpuRegDataTask::send_reply()
{
  std::string xml_str;
  int rc;

  // Check whether we have a saved failure return code
  if (_http_rc != HTTP_OK)
  {
    rc = _http_rc;
  }
  else
  {
    // If this is a PUT of type REG or CALL then include the previous
    // registration state on the response.
    if ((_type == RequestType::REG) || (_type == RequestType::CALL))
    {
      rc = XmlUtils::build_ClearwaterRegData_xml(_irs, xml_str, _cached_reg_state);
    }
    else
    {
      // Don't signal a previous registration state.
      rc = XmlUtils::build_ClearwaterRegData_xml(_irs, xml_str);
    }

    if (rc == HTTP_OK)
    {
      _req.add_content(xml_str);
    }
    else
    {
      SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_INVALID, 0);
      event.add_compressed_param(_irs->get_ims_sub_xml(), &SASEvent::PROFILE_SERVICE_PROFILE);
      SAS::report_event(event);
    }
  }

  TRC_DEBUG("Sending %d response (body was %s)", rc, _req.get_rx_body().c_str());
  send_http_reply(rc);
}

void ImpuRegDataTask::send_server_assignment_request(Cx::ServerAssignmentType type)
{
  // Create the SAR to send to the hss
  HssConnection::ServerAssignmentRequest request = {
    _impi,
    _impu,
    (_provided_server_name == "" ? _configured_server_name :
    _provided_server_name),
    type,
    _cfg->support_shared_ifcs,
    (_hss_wildcard.empty() ? _sprout_wildcard : _hss_wildcard)
  };

  // Create the callback
  HssConnection::saa_cb callback =
    std::bind(&ImpuRegDataTask::on_sar_response, this, _1);

  // Send the request
  _hss->send_server_assignment_request(callback, request, this->trail(), _req.get_stopwatch());
}

void ImpuRegDataTask::put_in_cache()
{
  std::string default_public_id = "";
  std::vector<std::string> public_ids =
    XmlUtils::get_public_and_default_ids(_irs->get_ims_sub_xml(), default_public_id);

  if (!public_ids.empty())
  {
    TRC_DEBUG("Attempting to cache IMS subscription for default public ID %s",
              default_public_id.c_str());

    // If we're caching an IMS subscription from the HSS we should check
    // the IRS contains a SIP URI and throw an error log if it doesn't.
    // We continue as normal even if it doesn't.
    if (_cfg->hss_configured)
    {
      bool found_sip_uri = false;

      for (std::vector<std::string>::iterator it = public_ids.begin();
           (it != public_ids.end()) && (!found_sip_uri);
           ++it)
      {
        if ((*it).compare(0, SIP_URI_PRE.length(), SIP_URI_PRE) == 0)
        {
          found_sip_uri = true;
        }
      }

      if (!found_sip_uri)
      {
        // LCOV_EXCL_START - This is essentially tested in the PPR UTs
        TRC_ERROR("No SIP URI in Implicit Registration Set");
        SAS::Event event(this->trail(), SASEvent::NO_SIP_URI_IN_IRS, 0);
        event.add_compressed_param(_irs->get_ims_sub_xml(), &SASEvent::PROFILE_SERVICE_PROFILE);
        SAS::report_event(event);
        // LCOV_EXCL_STOP
      }
    }

    {
      SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA, 0);
      std::string public_ids_str = boost::algorithm::join(public_ids, ", ");
      event.add_var_param(public_ids_str);
      event.add_compressed_param(_irs->get_ims_sub_xml(), &SASEvent::PROFILE_SERVICE_PROFILE);
      event.add_static_param(_irs->get_reg_state());
      std::string associated_private_ids_str = boost::algorithm::join(_irs->get_associated_impis(), ", ");
      event.add_var_param(associated_private_ids_str);
      event.add_var_param(_irs->get_charging_addresses().log_string());
      SAS::report_event(event);
    }

    // Create the callbacks
    void_success_cb success_cb =
      std::bind(&ImpuRegDataTask::on_put_reg_data_success, this);

    progress_callback progress_cb =
      std::bind(&ImpuRegDataTask::on_put_reg_data_progress, this);

    failure_callback failure_cb =
      std::bind(&ImpuRegDataTask::on_put_reg_data_failure, this, _1);

    // Cache the IRS
    _cache->put_implicit_registration_set(success_cb, progress_cb, failure_cb, _irs, this->trail(), _req.get_stopwatch());
  }
  else
  {
    // No need to wait for a cache write.  Just reply inline.
    send_reply();
    delete this;
  }
}

void ImpuRegDataTask::on_put_reg_data_progress()
{
  SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA_SUCCESS, 0);
  SAS::report_event(event);

  send_reply();
}

void ImpuRegDataTask::on_put_reg_data_success()
{
  // Just tidy up
  delete this;
}

void ImpuRegDataTask::on_put_reg_data_failure(Store::Status rc)
{
  SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA_FAIL, 0);
  event.add_static_param(rc);
  SAS::report_event(event);

  // Failed to cache Reg Data.  Return an error in the hope that the client might try again
  send_http_reply(HTTP_SERVER_UNAVAILABLE);

  delete this;
}

void ImpuRegDataTask::on_sar_response(const HssConnection::ServerAssignmentAnswer& saa)
{
  HssConnection::ResultCode rc = saa.get_result();
  TRC_DEBUG("Received Server-Assignment answer with result code %d", rc);

  if (rc == HssConnection::ResultCode::SUCCESS)
  {
    // The success case is handled below, this just exists so we can catch other
    // errors with our final "else"
  }
  else if (rc == HssConnection::ResultCode::SERVER_UNAVAILABLE)
  {
    // This may mean we can't access the Hss. Another Homestead node might be
    // able to, and we should return a 503 so that Sprout tries a different
    // Homestead.

    // SAS logging for the errors is the responsibility of the HssConnection
    _http_rc = HTTP_SERVER_UNAVAILABLE;
  }
  else if (rc == HssConnection::ResultCode::NOT_FOUND)
  {
    TRC_INFO("Server-Assignment answer - not found");

    // SAS logging for the errors is the responsibility of the HssConnection
    _http_rc = HTTP_NOT_FOUND;
  }
  else if (rc == HssConnection::ResultCode::NEW_WILDCARD)
  {
    // An error has been recieved in the SAA, and the wildcard has been
    // provided on the response
    std::string current_wildcard = wildcard_id();
    _hss_wildcard = saa.get_wildcard_impu();

    if (current_wildcard == _hss_wildcard)
    {
      // The wildcard has not actually been updated. Return an error rather than
      // retrying to avoid looping.
      // SAS logging for the errors is the responsibility of the HssConnection
      _http_rc = HTTP_SERVER_ERROR;
    }
    else
    {
      // We have an updated wildcard, so perform a new lookup with the new one.

      // Log this to SAS
      SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_UPDATED_WILDCARD, 0);
      event.add_var_param(current_wildcard);
      event.add_var_param(_hss_wildcard);
      SAS::report_event(event);

      // We need to delete the old IRS before we ask the cache for a new one
      delete _irs; _irs = NULL;

      get_reg_data();

      // Since processing has been redone, we can stop processing this SAA now.
      return;
    }
  }
  else if (rc == HssConnection::ResultCode::TIMEOUT)
  {
    TRC_ERROR("Timeout error at HSS when attempting SAR - reject with 504");

    // We also record a penalty for the purposes of overload control
    record_penalty();

    // SAS logging for the errors is the responsibility of the HssConnection
    _http_rc = HTTP_GATEWAY_TIMEOUT;
  }
  else
  {
    TRC_INFO("Server-Assignment answer with result code %d - reject", rc);

    // SAS logging for the errors is the responsibility of the HssConnection
    _http_rc = HTTP_SERVER_ERROR;
  }

  // Update the cache if required.
  bool pending_cache_op = false;
  if ((rc == HssConnection::ResultCode::SUCCESS) &&
      (!is_deregistration_request(_type)) &&
      (!is_auth_failure_request(_type)))
  {
    // This request assigned the user to us (i.e. it was successful and wasn't
    // triggered by a deregistration or auth failure) so cache the User-Data.

    // Get the charging addresses and user data.
    _irs->set_charging_addresses(saa.get_charging_addresses());
    _irs->set_ims_sub_xml(saa.get_service_profile());

    // We need to update the TTL on receiving an SAA
    _irs->set_ttl(_cfg->record_ttl);

    put_in_cache();
    pending_cache_op = true;
  }
  else if ((is_deregistration_request(_type)) &&
           (rc != HssConnection::ResultCode::SERVER_UNAVAILABLE))
  {
    // We're deregistering, so clear the cache.
    //
    // Even if the HSS rejects our deregistration request, we should
    // still delete our cached data - this reflects the fact that Sprout
    // has no bindings for it. If we were unable to deliver the Diameter
    // message, we might retry to a new Homestead node, and in this case we
    // don't want to delete the data (since the new Homestead node will receive
    // the request, not find the subscriber registered in the cache and reject
    // the request without trying to notify the HSS).
    SAS::Event event(this->trail(), SASEvent::CACHE_DELETE_REG_DATA, 0);
    event.add_var_param(_irs->get_default_impu());
    SAS::report_event(event);

    void_success_cb success_cb =
      std::bind(&ImpuRegDataTask::on_del_impu_success, this);

    progress_callback progress_cb =
      std::bind(&ImpuRegDataTask::on_del_impu_progress, this);

    failure_callback failure_cb =
      std::bind(&ImpuRegDataTask::on_del_impu_failure, this, _1);

    _cache->delete_implicit_registration_set(success_cb, progress_cb, failure_cb, _irs, this->trail(), _req.get_stopwatch());
    pending_cache_op = true;
  }

  // If we're not pending a cache operation, send a reply and delete the task.
  if (!pending_cache_op)
  {
    send_reply();
    delete this;
  }

  return;
}

// Returns the public id to use - priorities any wildcarded public id.
std::string ImpuRegDataTask::public_id()
{
  std::string wildcard_id_str = wildcard_id();
  return (wildcard_id_str.empty() ? _impu : wildcard_id_str);
}

// Returns the wildcarded public id to use - prioritises the wildcard returned by
// the HSS over the one sent by sprout as it is more up to date.
std::string ImpuRegDataTask::wildcard_id()
{
  return (_hss_wildcard.empty() ? _sprout_wildcard : _hss_wildcard);
}

void ImpuRegDataTask::on_del_impu_benign(bool not_found)
{
  SAS::Event event(this->trail(), (not_found) ? SASEvent::CACHE_DELETE_REG_DATA_NOT_FOUND : SASEvent::CACHE_DELETE_REG_DATA_SUCCESS, 0);
  SAS::report_event(event);

  send_reply();
}

void ImpuRegDataTask::on_del_impu_progress()
{
  on_del_impu_benign(false);
}

void ImpuRegDataTask::on_del_impu_success()
{
  // Just tidy up
  delete this;
}

void ImpuRegDataTask::on_del_impu_failure(Store::Status status)
{
  // Failed to delete IMPUs. If the error was "Not Found", just pass back the
  // stored error code.  "Not Found" errors are benign on deletion
  if (status == Store::Status::NOT_FOUND)
  {
    on_del_impu_benign(true);
    delete this;
  }
  else
  {
    // Not benign.  Return the original error if it wasn't OK
    SAS::Event event(this->trail(), SASEvent::CACHE_DELETE_REG_DATA_FAIL, 0);
    event.add_static_param(status);
    SAS::report_event(event);

    send_http_reply((_http_rc == HTTP_OK) ? HTTP_SERVER_UNAVAILABLE : _http_rc);
    delete this;
  }
}

//
// Version of the reg-data task that is read only (for use on the management
// interface).
//
void ImpuReadRegDataTask::run()
{
  if (_req.method() != htp_method_GET)
  {
    TRC_DEBUG("Reject non-GET for ImpuReadRegDataTask");
    send_http_reply(HTTP_BADMETHOD);
    delete this;
    return;
  }

  ImpuRegDataTask::run();
}
