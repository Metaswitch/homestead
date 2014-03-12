/**
 * @file handlers.cpp handlers for homestead
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

#include "handlers.h"
#include "xmlutils.h"
#include "servercapabilities.h"

#include "log.h"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidxml/rapidxml.hpp"


// The poll_homestead script pings homestead to check it's still alive.
// Handle the ping.
void PingHandler::run()
{
  _req.add_content("OK");
  _req.send_reply(200);
  delete this;
}

Diameter::Stack* HssCacheHandler::_diameter_stack = NULL;
std::string HssCacheHandler::_dest_realm;
std::string HssCacheHandler::_dest_host;
std::string HssCacheHandler::_server_name;
Cx::Dictionary* HssCacheHandler::_dict;
Cache* HssCacheHandler::_cache = NULL;
StatisticsManager* HssCacheHandler::_stats_manager = NULL;

const static HssCacheHandler::StatsFlags DIGEST_STATS =
  static_cast<HssCacheHandler::StatsFlags>(
    HssCacheHandler::STAT_HSS_LATENCY |
    HssCacheHandler::STAT_HSS_DIGEST_LATENCY);

const static HssCacheHandler::StatsFlags SUBSCRIPTION_STATS =
  static_cast<HssCacheHandler::StatsFlags>(
    HssCacheHandler::STAT_HSS_LATENCY |
    HssCacheHandler::STAT_HSS_SUBSCRIPTION_LATENCY);


void HssCacheHandler::configure_diameter(Diameter::Stack* diameter_stack,
                                         const std::string& dest_realm,
                                         const std::string& dest_host,
                                         const std::string& server_name,
                                         Cx::Dictionary* dict)
{
  LOG_STATUS("Configuring HssCacheHandler");
  LOG_STATUS("  Dest-Realm:  %s", dest_realm.c_str());
  LOG_STATUS("  Dest-Host:   %s", dest_host.c_str());
  LOG_STATUS("  Server-Name: %s", server_name.c_str());
  _diameter_stack = diameter_stack;
  _dest_realm = dest_realm;
  _dest_host = dest_host;
  _server_name = server_name;
  _dict = dict;
}

void HssCacheHandler::configure_cache(Cache* cache)
{
  _cache = cache;
}

void HssCacheHandler::configure_stats(StatisticsManager* stats_manager)
{
  _stats_manager = stats_manager;
}

void HssCacheHandler::on_diameter_timeout()
{
  _req.send_reply(503);
  delete this;
}

// General IMPI handling.

void ImpiHandler::run()
{
  if (parse_request())
  {
    LOG_DEBUG("Parsed HTTP request: private ID %s, public ID %s, scheme %s, authorization %s",
              _impi.c_str(), _impu.c_str(), _scheme.c_str(), _authorization.c_str());
    if (_cfg->query_cache_av)
    {
      query_cache_av();
    }
    else
    {
      LOG_DEBUG("Authentication vector cache query disabled - query HSS");
      get_av();
    }
  }
  else
  {
    _req.send_reply(404);
    delete this;
  }
}

void ImpiHandler::query_cache_av()
{
  LOG_DEBUG("Querying cache for authentication vector for %s/%s", _impi.c_str(), _impu.c_str());
  Cache::Request* get_av = _cache->create_GetAuthVector(_impi, _impu);
  CacheTransaction* tsx = new CacheTransaction(this);
  tsx->set_success_clbk(&ImpiHandler::on_get_av_success);
  tsx->set_failure_clbk(&ImpiHandler::on_get_av_failure);
  _cache->send(tsx, get_av);
}

void ImpiHandler::on_get_av_success(Cache::Request* request)
{
  Cache::GetAuthVector* get_av = (Cache::GetAuthVector*)request;
  DigestAuthVector av;
  get_av->get_result(av);
  LOG_DEBUG("Got authentication vector with digest %s from cache", av.ha1.c_str());
  send_reply(av);
  delete this;
}

void ImpiHandler::on_get_av_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  LOG_DEBUG("Cache query failed - reject request");
  _req.send_reply(502);
  delete this;
}

void ImpiHandler::get_av()
{
  if (_impu.empty())
  {
    if (_scheme == _cfg->scheme_aka)
    {
      // If the requested scheme is AKA, there's no point in looking up the cached public ID.
      // Even if we find it, we can't use it due to restrictions in the AKA protocol.
      LOG_INFO("Public ID unknown and requested scheme AKA - reject");
      _req.send_reply(404);
      delete this;
    }
    else
    {
      LOG_DEBUG("Public ID unknown - look up in cache");
      query_cache_impu();
    }
  }
  else
  {
    send_mar();
  }
}

void ImpiHandler::query_cache_impu()
{
  LOG_DEBUG("Querying cache to find public IDs associated with %s", _impi.c_str());
  Cache::Request* get_public_ids = _cache->create_GetAssociatedPublicIDs(_impi);
  CacheTransaction* tsx = new CacheTransaction(this);
  tsx->set_success_clbk(&ImpiHandler::on_get_impu_success);
  tsx->set_failure_clbk(&ImpiHandler::on_get_impu_failure);
  _cache->send(tsx, get_public_ids);
}

void ImpiHandler::on_get_impu_success(Cache::Request* request)
{
  Cache::GetAssociatedPublicIDs* get_public_ids = (Cache::GetAssociatedPublicIDs*)request;
  std::vector<std::string> ids;
  get_public_ids->get_result(ids);
  if (!ids.empty())
  {
    _impu = ids[0];
    LOG_DEBUG("Found cached public ID %s for private ID %s - now send Multimedia-Auth request",
              _impu.c_str(), _impi.c_str());
    send_mar();
  }
  else
  {
    LOG_INFO("No cached public ID found for private ID %s - reject", _impi.c_str());
    _req.send_reply(404);
    delete this;
  }
}

void ImpiHandler::on_get_impu_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  if (error == Cache::NOT_FOUND)
  {
    LOG_DEBUG("No cached public ID found for private ID %s - reject", _impi.c_str());
    _req.send_reply(404);
  }
  else
  {
    LOG_DEBUG("Cache query failed with rc %d", error);
    _req.send_reply(502);
  }
  delete this;
}

void ImpiHandler::send_mar()
{
  Cx::MultimediaAuthRequest mar(_dict,
                                _diameter_stack,
                                _dest_realm,
                                _dest_host,
                                _impi,
                                _impu,
                                _server_name,
                                _scheme,
                                _authorization);
  DiameterTransaction* tsx =
    new DiameterTransaction(_dict, this, DIGEST_STATS);

  tsx->set_response_clbk(&ImpiHandler::on_mar_response);
  mar.send(tsx, 200);
}

void ImpiHandler::on_mar_response(Diameter::Message& rsp)
{
  Cx::MultimediaAuthAnswer maa(rsp);
  int32_t result_code = 0;
  maa.result_code(result_code);
  LOG_DEBUG("Received Multimedia-Auth answer with result code %d", result_code);
  switch (result_code)
  {
    case 2001:
    {
      std::string sip_auth_scheme = maa.sip_auth_scheme();
      if (sip_auth_scheme == _cfg->scheme_digest)
      {
        send_reply(maa.digest_auth_vector());
        if (_cfg->impu_cache_ttl != 0)
        {
          LOG_DEBUG("Caching that private ID %s includes public ID %s",
                    _impi.c_str(), _impu.c_str());
          Cache::Request* put_public_id =
            _cache->create_PutAssociatedPublicID(_impi,
                                                 _impu,
                                                 Cache::generate_timestamp(),
                                                 _cfg->impu_cache_ttl);
          CacheTransaction* tsx = new CacheTransaction(NULL);
          _cache->send(tsx, put_public_id);
        }
      }
      else if (sip_auth_scheme == _cfg->scheme_aka)
      {
        send_reply(maa.aka_auth_vector());
      }
      else
      {
        _req.send_reply(404);
      }
    }
    break;
    case 5001:
      LOG_INFO("Multimedia-Auth answer with result code %d - reject", result_code);
      _req.send_reply(404);
      break;
    default:
      LOG_INFO("Multimedia-Auth answer with result code %d - reject", result_code);
      _req.send_reply(500);
      break;
  }

  delete this;
}

//
// IMPI digest handling.
//

bool ImpiDigestHandler::parse_request()
{
  const std::string prefix = "/impi/";
  std::string path = _req.path();

  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impu = _req.param("public_id");
  _scheme = _cfg->scheme_digest;
  _authorization = "";

  return true;
}

void ImpiDigestHandler::send_reply(const DigestAuthVector& av)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  writer.String(JSON_DIGEST_HA1.c_str());
  writer.String(av.ha1.c_str());
  writer.EndObject();
  _req.add_content(sb.GetString());
  _req.send_reply(200);
}

void ImpiDigestHandler::send_reply(const AKAAuthVector& av)
{
  // It is an error to request AKA authentication through the digest URL.
  LOG_INFO("Digest requested but AKA received - reject");
  _req.send_reply(404);
}

//
// IMPI AV handling.
//

bool ImpiAvHandler::parse_request()
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
    _scheme = _cfg->scheme_digest; // LCOV_EXCL_LINE - digests are handled by the ImpiDigestHandler so we can't get here.
  }
  else if (scheme == "aka")
  {
    _scheme = _cfg->scheme_aka;
  }
  else
  {
    LOG_INFO("Couldn't parse scheme %s", scheme.c_str());
    return false;
  }
  _impu = _req.param("impu");
  _authorization = _req.param("autn");

  return true;
}

void ImpiAvHandler::send_reply(const DigestAuthVector& av)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

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
      writer.String(av.qop.c_str());
    }
    writer.EndObject();
  }
  writer.EndObject();

  _req.add_content(sb.GetString());
  _req.send_reply(200);
}

void ImpiAvHandler::send_reply(const AKAAuthVector& av)
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
    }
    writer.EndObject();
  }
  writer.EndObject();

  _req.add_content(sb.GetString());
  _req.send_reply(200);
}

//
// IMPI Registration Status handling
//

void ImpiRegistrationStatusHandler::run()
{
  if (_cfg->hss_configured)
  {
    const std::string prefix = "/impi/";
    std::string path = _req.path();
    _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
    _impu = _req.param("impu");
    _visited_network = _req.param("visited-network");
    if (_visited_network.empty())
    {
      _visited_network = _dest_realm;
    }
    _authorization_type = _req.param("auth-type");
    LOG_DEBUG("Parsed HTTP request: private ID %s, public ID %s, visited network %s, authorization type %s",
              _impi.c_str(), _impu.c_str(), _visited_network.c_str(), _authorization_type.c_str());

    Cx::UserAuthorizationRequest uar(_dict,
                                     _diameter_stack,
                                     _dest_host,
                                     _dest_realm,
                                     _impi,
                                     _impu,
                                     _visited_network,
                                     _authorization_type);
    DiameterTransaction* tsx =
      new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
    tsx->set_response_clbk(&ImpiRegistrationStatusHandler::on_uar_response);
    uar.send(tsx, 200);
  }
  else
  {
    LOG_DEBUG("No HSS configured - fake response for server %s", _server_name.c_str());
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(DIAMETER_SUCCESS);
    writer.String(JSON_SCSCF.c_str());
    writer.String(_server_name.c_str());
    writer.EndObject();
    _req.add_content(sb.GetString());
    _req.send_reply(200);
    delete this;
  }
}

void ImpiRegistrationStatusHandler::on_uar_response(Diameter::Message& rsp)
{
  Cx::UserAuthorizationAnswer uaa(rsp);
  int32_t result_code = 0;
  uaa.result_code(result_code);
  int32_t experimental_result_code = uaa.experimental_result_code();
  LOG_DEBUG("Received User-Authorization answer with result %d/%d",
            result_code, experimental_result_code);
  if ((result_code == DIAMETER_SUCCESS) ||
      (experimental_result_code == DIAMETER_FIRST_REGISTRATION) ||
      (experimental_result_code == DIAMETER_SUBSEQUENT_REGISTRATION))
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(result_code ? result_code : experimental_result_code);
    std::string server_name;
    // If the HSS returned a server_name, return that. If not, return the
    // server capabilities, even if none are returned by the HSS.
    if (uaa.server_name(server_name))
    {
      LOG_DEBUG("Got Server-Name %s", server_name.c_str());
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
      LOG_DEBUG("Got Server-Capabilities");
      ServerCapabilities server_capabilities = uaa.server_capabilities();
      server_capabilities.write_capabilities(&writer);
    }
    writer.EndObject();
    _req.add_content(sb.GetString());
    _req.send_reply(200);
  }
  else if ((experimental_result_code == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result_code == DIAMETER_ERROR_IDENTITIES_DONT_MATCH))
  {
    LOG_INFO("User unknown or public/private ID conflict - reject");
    _req.send_reply(404);
  }
  else if ((result_code == DIAMETER_AUTHORIZATION_REJECTED) ||
           (experimental_result_code == DIAMETER_ERROR_ROAMING_NOT_ALLOWED))
  {
    LOG_INFO("Authorization rejected due to roaming not allowed - reject");
    _req.send_reply(403);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    LOG_INFO("HSS busy - reject");
    _req.send_reply(503);
  }
  else
  {
    LOG_INFO("User-Authorization answer with result %d/%d - reject",
             result_code, experimental_result_code);
    _req.send_reply(500);
  }
  delete this;
}

//
// IMPU Location Information handling
//

void ImpuLocationInfoHandler::run()
{
  if (_cfg->hss_configured)
  {
    const std::string prefix = "/impu/";
    std::string path = _req.path();

    _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
    _originating = _req.param("originating");
    _authorization_type = _req.param("auth-type");
    LOG_DEBUG("Parsed HTTP request: public ID %s, originating %s, authorization type %s",
              _impu.c_str(), _originating.c_str(), _authorization_type.c_str());

    Cx::LocationInfoRequest lir(_dict,
                                _diameter_stack,
                                _dest_host,
                                _dest_realm,
                                _originating,
                                _impu,
                                _authorization_type);
    DiameterTransaction* tsx =
      new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
    tsx->set_response_clbk(&ImpuLocationInfoHandler::on_lir_response);
    lir.send(tsx, 200);
  }
  else
  {
    LOG_DEBUG("No HSS configured - fake response for server %s", _server_name.c_str());
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(DIAMETER_SUCCESS);
    writer.String(JSON_SCSCF.c_str());
    writer.String(_server_name.c_str());
    writer.EndObject();
    _req.add_content(sb.GetString());
    _req.send_reply(200);
    delete this;
  }
}

void ImpuLocationInfoHandler::on_lir_response(Diameter::Message& rsp)
{
  Cx::LocationInfoAnswer lia(rsp);
  int32_t result_code = 0;
  lia.result_code(result_code);
  int32_t experimental_result_code = lia.experimental_result_code();
  LOG_DEBUG("Received Location-Info answer with result %d/%d",
            result_code, experimental_result_code);
  if ((result_code == DIAMETER_SUCCESS) ||
      (experimental_result_code == DIAMETER_UNREGISTERED_SERVICE))
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(result_code ? result_code : experimental_result_code);
    std::string server_name;

    // If the HSS returned a server_name, return that. If not, return the
    // server capabilities, even if none are returned by the HSS.
    if ((result_code == DIAMETER_SUCCESS) && (lia.server_name(server_name)))
    {
      LOG_DEBUG("Got Server-Name %s", server_name.c_str());
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
      LOG_DEBUG("Got Server-Capabilities");
      ServerCapabilities server_capabilities = lia.server_capabilities();
      server_capabilities.write_capabilities(&writer);
    }
    writer.EndObject();
    _req.add_content(sb.GetString());
    _req.send_reply(200);
  }
  else if ((experimental_result_code == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result_code == DIAMETER_ERROR_IDENTITY_NOT_REGISTERED))
  {
    LOG_INFO("User unknown or public/private ID conflict - reject");
    _req.send_reply(404);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    LOG_INFO("HSS busy - reject");
    _req.send_reply(503);
  }
  else
  {
    LOG_INFO("Location-Info answer with result %d/%d - reject",
             result_code, experimental_result_code);
    _req.send_reply(500);
  }
  delete this;
}

//
// IMPU IMS Subscription handling for URLs of the form "/impu/<public ID>/reg-data"
//

// Determines whether an incoming HTTP request indicates deregistration
bool ImpuRegDataHandler::is_deregistration_request(RequestType type)
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
bool ImpuRegDataHandler::is_auth_failure_request(RequestType type)
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
Cx::ServerAssignmentType ImpuRegDataHandler::sar_type_for_request(RequestType type)
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
      LOG_ERROR("Couldn't produce an appropriate SAR - internal software error'");
      return Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION;
      // LCOV_EXCL_STOP
  }
}

ImpuRegDataHandler::RequestType ImpuRegDataHandler::request_type_from_body(std::string body)
{
  LOG_DEBUG("Determining request type from '%s'", body.c_str());
  RequestType ret = RequestType::UNKNOWN;

  std::string reqtype;
  rapidjson::Document document;
  document.Parse<0>(body.c_str());

  if (!document.IsObject() || !document.HasMember("reqtype") || !document["reqtype"].IsString())
  {
    LOG_ERROR("Did not receive valid JSON with a 'reqtype' element");
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
  LOG_DEBUG("New value of _type is %d", ret);
  return ret;
}

void ImpuRegDataHandler::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.full_path();

  _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impi = _req.param("private_id");
  LOG_DEBUG("Parsed HTTP request: private ID %s, public ID %s",
            _impi.c_str(), _impu.c_str());

  htp_method method = _req.method();

  // Police preconditions:
  //    - Method must either be GET or PUT
  //    - PUT requests must have a body of "reg", "call", "dereg-user"
  //   "dereg-admin", "dereg-timeout", "dereg-auth-failed" or
  //   "dereg-auth-timeout"

  if (method == htp_method_PUT)
  {
    _type = request_type_from_body(_req.body());
    if (_type == RequestType::UNKNOWN)
    {
      LOG_ERROR("HTTP request contains invalid value %s for type", _req.body().c_str());
      _req.send_reply(400);
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
    _req.send_reply(405);
    delete this;
    return;
  }

  // We must always get the data from the cache - even if we're doing
  // a deregistration, we'll need to use the existing private ID, and
  // need to return the iFCs to Sprout.

  LOG_DEBUG ("Try to find IMS Subscription information in the cache");
  Cache::Request* get_ims_sub = _cache->create_GetIMSSubscription(_impu);
  CacheTransaction* tsx = new CacheTransaction(this);
  tsx->set_success_clbk(&ImpuRegDataHandler::on_get_ims_subscription_success);
  tsx->set_failure_clbk(&ImpuRegDataHandler::on_get_ims_subscription_failure);
  _cache->send(tsx, get_ims_sub);
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
    return "???";
  }
}

void ImpuRegDataHandler::on_get_ims_subscription_success(Cache::Request* request)
{
  LOG_DEBUG("Got IMS subscription from cache");
  Cache::GetIMSSubscription* get_ims_sub = (Cache::GetIMSSubscription*)request;
  RegistrationState old_state;
  std::vector<std::string> associated_impis;
  int32_t ttl = 0;
  get_ims_sub->get_xml(_xml, ttl);
  get_ims_sub->get_registration_state(old_state, ttl);
  get_ims_sub->get_associated_impis(associated_impis);
  LOG_DEBUG("TTL for this database record is %d, IMS Subscription XML is %s, and registration state is %s",
            ttl,
            _xml.empty() ? "empty" : "not empty",
            regstate_to_str(old_state).c_str());

  // By default, we should remain in the existing state.
  _new_state = old_state;

  // GET requests shouldn't change the state - just respond with what
  // we have in the database
  if (_req.method() == htp_method_GET)
  {
    send_reply();
    delete this;
    return;
  }

  // Sprout didn't specify a private Id on the request, but we may
  // have one embedded in the cached User-Data which we can retrieve.
  if (_impi.empty())
  {
    _impi = XmlUtils::get_private_id(_xml);
  }
  else if ((_cfg->hss_configured) &&
           ((associated_impis.empty()) ||
            (std::find(associated_impis.begin(), associated_impis.end(), _impi) == associated_impis.end())))
  {
    LOG_DEBUG("Associating private identity %s to IRS for %s", _impi.c_str(), _impu.c_str());
    std::vector<std::string> public_ids = XmlUtils::get_public_ids(_xml);
    Cache::Request* put_associated_private_id =
      _cache->create_PutAssociatedPrivateID(public_ids,
                                            _impi,
                                            Cache::generate_timestamp(),
                                            (2 * _cfg->hss_reregistration_time));
    CacheTransaction* tsx = new CacheTransaction(NULL);
    _cache->send(tsx, put_associated_private_id);
  }

  if (_type == RequestType::REG)
  {
    // This message was based on a REGISTER request from Sprout. Check
    // the subscriber's state in Cassandra to determine whether this
    // is an initial registration or a re-registration.
    if (old_state == RegistrationState::REGISTERED)
    {
      _new_state = RegistrationState::REGISTERED;
      LOG_DEBUG("Handling re-registration");

      // We set the record's TTL to be double the --hss-reregistration-time
      // option - once half that time has elapsed, it's time to
      // re-notify the HSS.
      if ((ttl < _cfg->hss_reregistration_time) && _cfg->hss_configured)
      {
        LOG_DEBUG("Sending re-registration to HSS as %d seconds have passed", _cfg->hss_reregistration_time);
        send_server_assignment_request(Cx::ServerAssignmentType::RE_REGISTRATION);
      }
      else
      {
        // No state changes are required for a re-register if we're
        // not notifying a HSS - just respond.
        send_reply();
        delete this;
        return;
      }
    }
    else
    {
      LOG_DEBUG("Handling initial registration");
      if (_cfg->hss_configured)
      {
        // If we have a HSS, we should process an initial registration
        // by sending a Server-Assignment-Request and caching the response.
        _new_state = RegistrationState::REGISTERED;
        send_server_assignment_request(Cx::ServerAssignmentType::REGISTRATION);
      }
      else if (old_state == RegistrationState::UNREGISTERED)
      {
        // No HSS, but we have been locally provisioned with this
        // subscriber, so put it into REGISTERED state.
        _new_state = RegistrationState::REGISTERED;
        put_in_cache();
        send_reply();
        delete this;
        return;
      }
      else
      {
        // We have no HSS and no record of this subscriber, so they
        // don't exist.
        _req.send_reply(404);
        delete this;
        return;
      }
    }
  }
  else if (_type == RequestType::CALL)
  {
    // This message was based on an initial non-REGISTER request
    // (INVITE, PUBLISH, MESSAGE etc.).
    LOG_DEBUG("Handling call");
    if (old_state == RegistrationState::NOT_REGISTERED)
    {
      // We don't know anything about this subscriber. If we have a
      // HSS, this means we should send a Server-Assignment-Request to
      // provide unregistered service; if we don't have a HSS, reject
      // the request.
      if (_cfg->hss_configured)
      {
        LOG_DEBUG("Moving to unregistered state");
        _new_state = RegistrationState::UNREGISTERED;
        send_server_assignment_request(Cx::ServerAssignmentType::UNREGISTERED_USER);
      }
      else
      {
        _req.send_reply(404);
        delete this;
        return;
      }
    }
    else
    {
      // We're already assigned to handle this subscriber - respond
      // with the iFCs anfd whether they're in registered state or not.
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
    if (old_state == RegistrationState::REGISTERED)
    {
      LOG_DEBUG("Handling deregistration");
      if (_cfg->hss_configured)
      {
        // If we have a HSS, we should forget about this subscriber
        // entirely and send an appropriate SAR.
        _new_state = RegistrationState::NOT_REGISTERED;
        send_server_assignment_request(sar_type_for_request(_type));
      }
      else
      {
        // We don't have a HSS - we should just move the subscriber
        // into unregistered state (but retain the data, as it's not
        // stored anywhere else).
        _new_state = RegistrationState::UNREGISTERED;
        put_in_cache();
        send_reply();
        delete this;
        return;
      }
    }
    else
    {
      // We treat a deregistration for a deregistered user as an error
      // - this is useful for preventing loops, where we try and
      // continually deregister a user

      LOG_DEBUG("Rejecting deregistration for user who was not registered");
      _req.send_reply(400);
      delete this;
      return;
    }
  }
  else if (is_auth_failure_request(_type))
  {
    // Authentication failures don't change our state (if a user's
    // already registered, failing to log in with a new binding
    // shouldn't deregister them - if they're not registered and fail
    // to log in, they're already in the right state).

    // If we have a HSS, we do need to notify it so that it removes
    // the Auth-Pending flag.
    LOG_DEBUG("Handling authentication failure/timeout");
    if (_cfg->hss_configured)
    {
      send_server_assignment_request(sar_type_for_request(_type));
    }
  }
  else
  {
    // LCOV_EXCL_START - unreachable
    LOG_ERROR("Invalid type %d", _type);
    delete this;
    return;
    // LCOV_EXCL_STOP - unreachable
  }
}

void ImpuRegDataHandler::send_reply()
{
  LOG_DEBUG("Building 200 OK response to send (body was %s)", _req.body().c_str());
  _req.add_content(XmlUtils::build_ClearwaterRegData_xml(_new_state, _xml));
  _req.send_reply(200);
}

void ImpuRegDataHandler::on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  LOG_DEBUG("IMS subscription cache query failed: %u, %s", error, text.c_str());
  _req.send_reply(502);
  delete this;
}

void ImpuRegDataHandler::send_server_assignment_request(Cx::ServerAssignmentType type)
{
  Cx::ServerAssignmentRequest sar(_dict,
                                  _diameter_stack,
                                  _dest_host,
                                  _dest_realm,
                                  _impi,
                                  _impu,
                                  _server_name,
                                  type);
  DiameterTransaction* tsx =
    new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
  tsx->set_response_clbk(&ImpuRegDataHandler::on_sar_response);
  sar.send(tsx, 200);
}

std::vector<std::string> ImpuRegDataHandler::get_associated_private_ids()
{
  std::vector<std::string> private_ids;
  if (!_impi.empty())
  {
    LOG_DEBUG("Associated private ID %s", _impi.c_str());
    private_ids.push_back(_impi);
  }
  std::string xml_impi = XmlUtils::get_private_id(_xml);
  if ((!xml_impi.empty()) && (xml_impi != _impi))
  {
    LOG_DEBUG("Associated private ID %s", xml_impi.c_str());
    private_ids.push_back(xml_impi);
  }
  return private_ids;
}

void ImpuRegDataHandler::put_in_cache()
{
  int ttl;
  if (_cfg->hss_configured)
  {
    // Set twice the HSS registration time - code elsewhere will check
    // whether the TTL has passed the halfway point and do a
    // RE_REGISTRATION request to the HSS. This is better than just
    // setting the TTL to be the registration time, as it means there
    // are no gaps where the data has expired but we haven't received
    // a REGISTER yet.
    ttl = (2 * _cfg->hss_reregistration_time);
  }
  else
  {
    // No TTL if we don't have a HSS - we should never expire the
    // data because we're the master.
    ttl = 0;
  }

  LOG_DEBUG("Attempting to cache IMS subscription for public IDs");
  std::vector<std::string> public_ids = XmlUtils::get_public_ids(_xml);
  if (!public_ids.empty())
  {
    LOG_DEBUG("Got public IDs to cache against - doing it");
    for (std::vector<std::string>::iterator i = public_ids.begin();
         i != public_ids.end();
         i++)
    {
      LOG_DEBUG("Public ID %s", i->c_str());
    }

    std::vector<std::string> associated_private_ids;
    if (_cfg->hss_configured)
    {
      associated_private_ids = get_associated_private_ids();
    }

    Cache::Request* put_ims_sub =
      _cache->create_PutIMSSubscription(public_ids,
                                        _xml,
                                        _new_state,
                                        associated_private_ids,
                                        Cache::generate_timestamp(),
                                        ttl);
    CacheTransaction* tsx = new CacheTransaction(NULL);
    _cache->send(tsx, put_ims_sub);
  }
}

void ImpuRegDataHandler::on_sar_response(Diameter::Message& rsp)
{
  Cx::ServerAssignmentAnswer saa(rsp);
  int32_t result_code = 0;
  saa.result_code(result_code);
  saa.user_data(_xml);
  LOG_DEBUG("Received Server-Assignment answer with result code %d", result_code);

  // Even if the HSS rejects our deregistration request, we should
  // still delete our cached data - this reflects the fact that Sprout
  // has no bindings for it.
  if (is_deregistration_request(_type))
  {
    std::vector<std::string> public_ids = XmlUtils::get_public_ids(_xml);
    if (!public_ids.empty())
    {
      LOG_DEBUG("Got public IDs to delete from cache - doing it");
      for (std::vector<std::string>::iterator i = public_ids.begin();
           i != public_ids.end();
           i++)
      {
        LOG_DEBUG("Public ID %s", i->c_str());
      }

      Cache::Request* delete_public_id =
        _cache->create_DeletePublicIDs(public_ids,
                                       get_associated_private_ids(),
                                       Cache::generate_timestamp());
      CacheTransaction* tsx = new CacheTransaction(NULL);
      _cache->send(tsx, delete_public_id);
    }
  }

  switch (result_code)
  {
    case 2001:
      // If we expect this request to assign the user to us (i.e. it
      // isn't triggered by a deregistration or a failure) we should
      // cache the User-Data.
      if (!is_deregistration_request(_type) && !is_auth_failure_request(_type))
      {
        LOG_DEBUG("Getting User-Data from SAA for cache");
        put_in_cache();
      }
      send_reply();
      break;
    case 5001:
      LOG_INFO("Server-Assignment answer with result code %d - reject", result_code);
      _req.send_reply(404);
      break;
    default:
      LOG_INFO("Server-Assignment answer with result code %d - reject", result_code);
      _req.send_reply(500);
      break;
  }
  delete this;
  return;
}

//
// IMPU IMS Subscription handling for URLs of the form
// "/impu/<public ID>". Deprecated.
//

void ImpuIMSSubscriptionHandler::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.full_path();

  _impu = path.substr(prefix.length());
  _impi = _req.param("private_id");
  LOG_DEBUG("Parsed HTTP request: private ID %s, public ID %s",
            _impi.c_str(), _impu.c_str());

  if (_impi.empty())
  {
    _type = RequestType::CALL;
  }
  else
  {
    _type = RequestType::REG;
  }

  LOG_DEBUG("Try to find IMS Subscription information in the cache");
  Cache::Request* get_ims_sub = _cache->create_GetIMSSubscription(_impu);
  CacheTransaction* tsx = new CacheTransaction(this);
  tsx->set_success_clbk(&ImpuRegDataHandler::on_get_ims_subscription_success);
  tsx->set_failure_clbk(&ImpuRegDataHandler::on_get_ims_subscription_failure);
  _cache->send(tsx, get_ims_sub);
}

void ImpuIMSSubscriptionHandler::send_reply()
{
  if (_xml != "")
  {
    LOG_DEBUG("Building 200 OK response to send");
    _req.add_content(_xml);
    _req.send_reply(200);
  }
  else
  {
    LOG_DEBUG("No XML User-Data available, returning 404");
    _req.send_reply(404);
  }
}

void RegistrationTerminationHandler::run()
{
  Cx::RegistrationTerminationRequest rtr(_msg);

  // Save off the deregistration reason and all private and public
  // identities on the request.
  _deregistration_reason = rtr.deregistration_reason();
  std::string impi = rtr.impi();
  _impis.push_back(impi);
  std::vector<std::string> associated_identities = rtr.associated_identities();
  _impis.insert(_impis.end(), associated_identities.begin(), associated_identities.end());
  if (_deregistration_reason != SERVER_CHANGE)
  {
    // We're not interested in the public identities on the request
    // if deregistration reason is SERVER_CHANGE. We'll find some
    // public identities later, and we want _impus to be empty for now.
    _impus = rtr.impus();
  }

  LOG_INFO("Received Regestration-Termination request with dereg reason %d",
           _deregistration_reason);

  if (((_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                            (_deregistration_reason == REMOVE_SCSCF))) ||
      (_deregistration_reason == SERVER_CHANGE))
  {
    // Find all the default public identities associated with the
    // private identities specified on the request. Create a copy of
    // our private identities so that we can keep track of which ones
    // we've made cache requests for.
    _impis_copy = _impis;
    get_associated_primary_public_ids(NULL);
  }
  else if ((!_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                                 (_deregistration_reason == REMOVE_SCSCF) ||
                                 (_deregistration_reason == NEW_SERVER_ASSIGNED)))
  {
    // Find information about the registration sets for the public
    // identities specified on the request.
    get_registration_sets(NULL);
  }
  else
  {
    // This is either an invalid deregistration reason, or no public
    // identities specified on a NEW_SERVER_ASSIGNED RTR. Both of these
    // are errors.
    LOG_ERROR("Unexpected Registration-Termination request received with deregistration reason %d",
              _deregistration_reason);
  }
}

void RegistrationTerminationHandler::get_associated_primary_public_ids(Cache::Request* request)
{
  // This is a recursive function. It makes cache requests and sets itself
  // as the callback. The function creates a list of all the default public
  // identities associated with the private identities specified on the RTR.
  if (request != NULL)
  {
    // Add the default public identities returned by the cache to _impus.
    Cache::GetAssociatedPrimaryPublicIDs* get_associated_impus_result =
      (Cache::GetAssociatedPrimaryPublicIDs*)request;
    std::vector<std::string> associated_primary_public_ids;
    get_associated_impus_result->get_result(associated_primary_public_ids);
    _impus.insert(_impus.end(),
                  associated_primary_public_ids.begin(),
                  associated_primary_public_ids.end());

    // We have now finished with the last element of the copy vector, so
    // get rid of it.
     _impis_copy.pop_back();
  }

  if (!_impis_copy.empty())
  {
    std::string impi = _impis_copy.back();
    LOG_DEBUG("Finding associated default public identities for impi %s", impi.c_str());
    Cache::Request* get_associated_impus = _cfg->cache->create_GetAssociatedPrimaryPublicIDs(impi);
    CacheTransaction* tsx = new CacheTransaction(this);
    tsx->set_success_clbk(&RegistrationTerminationHandler::get_associated_primary_public_ids);
    tsx->set_failure_clbk(&RegistrationTerminationHandler::get_associated_primary_public_ids_failure);
    _cfg->cache->send(tsx, get_associated_impus);
  }
  else
  {
    // We now have all the default public identities. Find their registration sets.
    // Remove any duplicates first.
    sort(_impus.begin(), _impus.end());
    _impus.erase(unique(_impus.begin(), _impus.end()), _impus.end());
    get_registration_sets(NULL);
  }
}

void RegistrationTerminationHandler::get_associated_primary_public_ids_failure(Cache::Request* request,
                                                                               Cache::ResultCode error,
                                                                               std::string& text)
{
  // We don't worry too much about this. Just move on to the next private ID.
  LOG_DEBUG("Failed to get associated default public identities for impi %s", _impis_copy.back().c_str());
  _impis_copy.pop_back();
  get_associated_primary_public_ids(NULL);
}

void RegistrationTerminationHandler::get_registration_sets(Cache::Request* request)
{
  // This is a recursive function. It makes cache requests and sets itself
  // as the callback. The function creates a list of all the registration sets
  // associated with this RTR. It can also save off a list of private identities
  // associated with our public identities.
  if (request != NULL)
  {
    Cache::GetIMSSubscription* get_ims_sub_result = (Cache::GetIMSSubscription*)request;
    std::string ims_sub;
    int32_t temp;
    get_ims_sub_result->get_xml(ims_sub, temp);

    // Add the list of public identities in the IMS subscription to
    // the list of registration sets and remove the last element of
    // _impus so that we can keep track of which public identities
    // we've looked up.
    _registration_sets.push_back(XmlUtils::get_public_ids(ims_sub));
    _impus.pop_back();

    if ((_deregistration_reason == SERVER_CHANGE) ||
        (_deregistration_reason == NEW_SERVER_ASSIGNED))
    {
      // GetIMSSubscription also returns a list of associated private
      // identities. Save these off.
      std::vector<std::string> associated_impis;
      get_ims_sub_result->get_associated_impis(associated_impis);
      _associated_impis.insert(_associated_impis.end(),
                               associated_impis.begin(),
                               associated_impis.end());
    }
  }

  if (!_impus.empty())
  {
    std::string impu = _impus.back();
    LOG_DEBUG("Finding registration set for public identity %s", impu.c_str());
    Cache::Request* get_ims_sub = _cfg->cache->create_GetIMSSubscription(impu);
    CacheTransaction* tsx = new CacheTransaction(this);
    tsx->set_success_clbk(&RegistrationTerminationHandler::get_registration_sets);
    tsx->set_failure_clbk(&RegistrationTerminationHandler::get_registration_sets_failure);
    _cfg->cache->send(tsx, get_ims_sub);
  }
  else
  {
    // We now have all the registration sets, and we can delete the registrations.
    delete_registrations();
  }
}

void RegistrationTerminationHandler::get_registration_sets_failure(Cache::Request* request,
                                                                   Cache::ResultCode error,
                                                                   std::string& text)
{
  // We don't worry too much about this. Just move on to the next public ID.
  LOG_DEBUG("Failed to get registration set for public identity %s", _impus.back().c_str());
  _impus.pop_back();
  get_registration_sets(NULL);
}

void RegistrationTerminationHandler::delete_registrations()
{
  // No real SAS implementation yet.
  SAS::TrailId fake_trail = 0;
  HTTPCode ret_code = 0;
  std::vector<std::string> empty_vector;
  std::vector<std::string> default_public_identities;

  // Extract the default public identities from the registration sets. These are the
  // first public identities in the sets.
  for (std::vector<std::vector<std::string>>::iterator i = _registration_sets.begin();
       i != _registration_sets.end();
       i++)
  {
    default_public_identities.push_back((*i)[0]);
  }

  // We need to notify sprout of the deregistrations. What we send to sprout depends
  // on the deregistration reason.
  switch (_deregistration_reason)
  {
  case PERMANENT_TERMINATION:
    ret_code = _cfg->sprout_conn->send_delete(false, default_public_identities, _impis, fake_trail);
    break;

  case REMOVE_SCSCF:
  case SERVER_CHANGE:
    ret_code = _cfg->sprout_conn->send_delete(true, default_public_identities, empty_vector, fake_trail);
    break;

  case NEW_SERVER_ASSIGNED:
    ret_code = _cfg->sprout_conn->send_delete(false, default_public_identities, empty_vector, fake_trail);
    break;

  default:
    LOG_ERROR("Unexpected deregistration reason %d on RTR", _deregistration_reason);
    break;
  }

  switch (ret_code)
  {
  case HTTP_OK:
    LOG_DEBUG("Send Registration-Termination answer indicating success");
    send_rta(DIAMETER_REQ_SUCCESS);
    break;

  case HTTP_BADMETHOD:
  case HTTP_BAD_RESULT:
  case HTTP_SERVER_ERROR:
    LOG_DEBUG("Send Registration-Termination answer indicating failure");
    send_rta(DIAMETER_REQ_FAILURE);
    break;

  default:
    LOG_ERROR("Unexpected HTTP return code, send Registration-Termination answer indicating failure");
    send_rta(DIAMETER_REQ_FAILURE);
    break;
  }

  // Remove the relevant registration information from Cassandra.
  dissociate_implicit_registration_sets();

  if ((_deregistration_reason == SERVER_CHANGE) ||
      (_deregistration_reason == NEW_SERVER_ASSIGNED))
  {
    LOG_DEBUG("Delete IMPI mappings");
    delete_impi_mappings();
  }

  delete this;
}

void RegistrationTerminationHandler::dissociate_implicit_registration_sets()
{
  // Dissociate each private identity from each registration set.
  for (std::vector<std::string>::iterator i = _impis.begin();
       i != _impis.end();
       i++)
  {
    for (std::vector<std::vector<std::string>>::iterator j = _registration_sets.begin();
         j != _registration_sets.end();
         j++)
    {
      Cache::Request* dissociate_reg_set =
        _cfg->cache->create_DissociateImplicitRegistrationSetFromImpi(*j, *i, Cache::generate_timestamp());
      CacheTransaction* tsx = new CacheTransaction(this);
      _cfg->cache->send(tsx, dissociate_reg_set);
    }
  }
}

void RegistrationTerminationHandler::delete_impi_mappings()
{
  // Delete rows from the IMPI table for all associated IMPIs. First remove any
  // duplicates in the list of _associated_impis.
  _associated_impis.erase(unique(_associated_impis.begin(), _associated_impis.end()), _associated_impis.end());
  get_registration_sets(NULL);
  Cache::Request* delete_impis = _cfg->cache->create_DeleteIMPIMapping(_associated_impis, Cache::generate_timestamp());
  CacheTransaction* tsx = new CacheTransaction(this);
  _cfg->cache->send(tsx, delete_impis);
}

void RegistrationTerminationHandler::send_rta(const std::string result_code)
{
  // Use our Cx layer to create a RTA object and add the correct AVPs. The RTA is
  // created from the RTR.
  Cx::RegistrationTerminationAnswer rta(_msg,
                                        _cfg->dict,
                                        result_code,
                                        _msg.auth_session_state(),
                                        _impis);

  // Send the RTA back to the HSS.
  LOG_INFO("Ready to send RTA");
  rta.send();
}

void PushProfileHandler::run()
{
  // Received a Push Profile Request. We may need to update a digest in the cache. We may
  // need to update an IMS subscription in the cache.
  Cx::PushProfileRequest ppr(_msg);

  // If we have a private ID and a digest specified on the PPR, update the digest for this impi
  // in the cache.
  std::string impi = ppr.impi();
  DigestAuthVector digest_auth_vector = ppr.digest_auth_vector();
  if ((!impi.empty()) && (!digest_auth_vector.ha1.empty()))
  {
    LOG_INFO("Updating digest for private ID %s from PPR", impi.c_str());
    Cache::Request* put_auth_vector = _cfg->cache->create_PutAuthVector(impi, digest_auth_vector, Cache::generate_timestamp(), _cfg->impu_cache_ttl);
    HssCacheHandler::CacheTransaction<PushProfileHandler>* tsx = new HssCacheHandler::CacheTransaction<PushProfileHandler>(NULL);
    _cfg->cache->send(tsx, put_auth_vector);
  }

  // If the PPR contains a User-Data AVP containing IMS subscription, update the impu table in the
  // cache with this IMS subscription for each public ID mentioned.
  std::string user_data;
  if (ppr.user_data(user_data))
  {
    LOG_INFO("Updating IMS subscription from PPR");
    std::vector<std::string> impus = XmlUtils::get_public_ids(user_data);
    RegistrationState state = RegistrationState::UNCHANGED;
    Cache::Request* put_ims_subscription = _cfg->cache->create_PutIMSSubscription(impus,
                                                                                  user_data,
                                                                                  state,
                                                                                  Cache::generate_timestamp(),
                                                                                  (2 * _cfg->hss_reregistration_time));
    HssCacheHandler::CacheTransaction<PushProfileHandler>* tsx = new HssCacheHandler::CacheTransaction<PushProfileHandler>(NULL);
    _cfg->cache->send(tsx, put_ims_subscription);
  }

  // Get the Auth-Session-State. PPRs are required to have an Auth-Session-State, so
  // this AVP will be present.
  int32_t auth_session_state = ppr.auth_session_state();

  // Use our Cx layer to create a PPA object and add the correct AVPs. The PPA is
  // created from the PPR. We currently always return DIAMETER_SUCCESS. We may want
  // to return DIAMETER_UNABLE_TO_COMPLY for failures in future.
  Cx::PushProfileAnswer ppa(_msg,
                            _cfg->dict,
                            DIAMETER_REQ_SUCCESS,
                            auth_session_state);

  // Send the PPA back to the HSS.
  LOG_INFO("Ready to send PPA");
  ppa.send();

  delete this;
}
