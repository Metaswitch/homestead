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
  Cx::MultimediaAuthRequest* mar =
    new Cx::MultimediaAuthRequest(_dict,
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
  mar->send(tsx, 200);
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
  writer.String("digest_ha1");
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
    _scheme = _cfg->scheme_digest;
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
    writer.String("digest");
    writer.StartObject();
    {
      writer.String("ha1");
      writer.String(av.ha1.c_str());
      writer.String("realm");
      writer.String(av.realm.c_str());
      writer.String("qop");
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
    writer.String("aka");
    writer.StartObject();
    {
      writer.String("challenge");
      writer.String(av.challenge.c_str());
      writer.String("response");
      writer.String(av.response.c_str());
      writer.String("cryptkey");
      writer.String(av.crypt_key.c_str());
      writer.String("integritykey");
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

    Cx::UserAuthorizationRequest* uar =
      new Cx::UserAuthorizationRequest(_dict,
                                       _dest_host,
                                       _dest_realm,
                                       _impi,
                                       _impu,
                                       _visited_network,
                                       _authorization_type);
    DiameterTransaction* tsx =
      new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
    tsx->set_response_clbk(&ImpiRegistrationStatusHandler::on_uar_response);
    uar->send(tsx, 200);
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

    Cx::LocationInfoRequest* lir =
      new Cx::LocationInfoRequest(_dict,
                                  _dest_host,
                                  _dest_realm,
                                  _originating,
                                  _impu,
                                  _authorization_type);
    DiameterTransaction* tsx =
      new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
    tsx->set_response_clbk(&ImpuLocationInfoHandler::on_lir_response);
    lir->send(tsx, 200);
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
// IMPU IMS Subscription handling
//

void ImpuIMSSubscriptionHandler::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.full_path();

  _impu = path.substr(prefix.length());
  _impi = _req.param("private_id");
  std::string type = _req.param("type");
  LOG_DEBUG("Parsed HTTP request: private ID %s, public ID %s, type %s",
            _impi.c_str(), _impu.c_str(), type.c_str());

  // Map the type parameter from the HTTP request to its corresponding
  // ServerAssignmentType object and save it off. We should always get a match,
  // but if not _type was initialised to default values by the constructor.
  std::map<std::string, ServerAssignmentType>::const_iterator it = SERVER_ASSIGNMENT_TYPES.find(type);
  if (it != SERVER_ASSIGNMENT_TYPES.end())
  {
    _type = it->second;
  }
  else
  {
    // The default _type value assumed we had an IMPI, and therefore that this
    // was a REGISTRATION. If we don't have an IMPI, change the default
    // Server-Assignment-Type to UNREGISTERED_USER.
    if (_impi.empty())
    {
      _type.unregistered_user_default();
    }

    if (!type.empty())
    {
      LOG_WARNING("HTTP request contains invalid value %s for type parameter", type.c_str());
    }
  }

  // The ServerAssignmentType object has a cache_lookup field which
  // determines whether we should look for IMS subscription in the cache.
  if((_type.cache_lookup()) || !(_cfg->hss_configured))
  {
    LOG_DEBUG("Try to find IMS Subscription information in the cache");
    Cache::Request* get_ims_sub = _cache->create_GetIMSSubscription(_impu);
    CacheTransaction* tsx = new CacheTransaction(this);
    tsx->set_success_clbk(&ImpuIMSSubscriptionHandler::on_get_ims_subscription_success);
    tsx->set_failure_clbk(&ImpuIMSSubscriptionHandler::on_get_ims_subscription_failure);
    _cache->send(tsx, get_ims_sub);
  }
  else
  {
    LOG_DEBUG("First time register or deregistration - go to the HSS");
    send_server_assignment_request();
  }
}

void ImpuIMSSubscriptionHandler::on_get_ims_subscription_success(Cache::Request* request)
{
  LOG_DEBUG("Got IMS subscription from cache");
  Cache::GetIMSSubscription* get_ims_sub = (Cache::GetIMSSubscription*)request;
  std::string xml;
  int32_t ttl;
  get_ims_sub->get_xml(xml, ttl);
  _req.add_content(xml);
  _req.send_reply(200);
  delete this;
}

void ImpuIMSSubscriptionHandler::on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  if ((error == Cache::NOT_FOUND) && (_cfg->hss_configured))
  {
    LOG_DEBUG("No cached IMS subscription found, and HSS configured - query it");
    send_server_assignment_request();
  }
  else
  {
    LOG_DEBUG("IMS subscription cache query failed: %u, %s", error, text.c_str());
    _req.send_reply(502);
    delete this;
  }
}

void ImpuIMSSubscriptionHandler::send_server_assignment_request()
{
  Cx::ServerAssignmentRequest* sar =
    new Cx::ServerAssignmentRequest(_dict,
                                    _dest_host,
                                    _dest_realm,
                                    _impi,
                                    _impu,
                                    _server_name,
                                    _type.type());
  DiameterTransaction* tsx =
    new DiameterTransaction(_dict, this, SUBSCRIPTION_STATS);
  tsx->set_response_clbk(&ImpuIMSSubscriptionHandler::on_sar_response);
  sar->send(tsx, 200);
}

void ImpuIMSSubscriptionHandler::on_sar_response(Diameter::Message& rsp)
{
  Cx::ServerAssignmentAnswer saa(rsp);
  int32_t result_code = 0;
  saa.result_code(result_code);
  LOG_DEBUG("Received Server-Assignment answer with result code %d", result_code);
  switch (result_code)
  {
    case 2001:
      {
        if (!_type.deregistration())
        {
          std::string user_data;
          RegistrationState state = RegistrationState::REGISTERED;
          saa.user_data(user_data);
          _req.add_content(user_data);

          if (_cfg->ims_sub_cache_ttl != 0)
          {
            LOG_DEBUG("Attempting to cache IMS subscription for public IDs");
            std::vector<std::string> public_ids = XmlUtils::get_public_ids(user_data);
            if (!public_ids.empty())
            {
              LOG_DEBUG("Got public IDs to cache against - doing it");
              Cache::Request* put_ims_sub =
                _cache->create_PutIMSSubscription(public_ids,
                                                  user_data,
                                                  state,
                                                  Cache::generate_timestamp(),
                                                  _cfg->ims_sub_cache_ttl,
                                                  (2 * _cfg->ims_sub_cache_ttl));
              CacheTransaction* tsx = new CacheTransaction(NULL);
              _cache->send(tsx, put_ims_sub);
            }
          }
        }
        _req.send_reply(200);
      }
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

  // Finally, regardless of the result_code from the HSS, if we are doing a
  // deregistration, we should delete the IMS subscription relating to our public_id.
  if (_type.deregistration())
  {
    LOG_INFO("Delete IMS subscription for %s", _impu.c_str());
    Cache::Request* delete_public_id =
      _cache->create_DeletePublicIDs(_impu, Cache::generate_timestamp());
    CacheTransaction* tsx = new CacheTransaction(NULL);
    _cache->send(tsx, delete_public_id);
  }
  delete this;
}

void RegistrationTerminationHandler::run()
{
  // Received a Registration Termination Request. Delete all private IDs and public IDs
  // associated with this RTR from the cache.
  LOG_INFO("Received Regestration Termination Request");
  Cx::RegistrationTerminationRequest rtr(_msg);

  // The RTR should contain a User-Name AVP with one private ID, and then an
  // Associated-Identities AVP with any number of associated private IDs.
  std::string impi = rtr.impi();
  _impis.push_back(impi);
  std::vector<std::string> associated_identities = rtr.associated_identities();
  _impis.insert(_impis.end(), associated_identities.begin(), associated_identities.end());

  // The RTR may also contain some number of Public-Identity AVPs containing public IDs.
  // If we can't find any, we go to the cache and find any public IDs associated with the
  // private IDs we've got.
  _impus = rtr.impus();

  if (_impus.empty())
  {
    LOG_DEBUG("No public IDs on the RTR - go to the cache");
    Cache::Request* get_public_ids = _cfg->cache->create_GetAssociatedPublicIDs(_impis);
    HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>* tsx = new HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>(this);
    tsx->set_success_clbk(&RegistrationTerminationHandler::on_get_public_ids_success);
    tsx->set_failure_clbk(&RegistrationTerminationHandler::on_get_public_ids_failure);
    _cfg->cache->send(tsx, get_public_ids);
  }
  else
  {
    delete_identities();
  }

  // Get the Auth-Session-State. RTRs are required to have an Auth-Session-State, so
  // this AVP will be present.
  int32_t auth_session_state = rtr.auth_session_state();

  // Use our Cx layer to create a RTA object and add the correct AVPs. The RTA is
  // created from the RTR. We currently always return DIAMETER_SUCCESS. We may want
  // to return DIAMETER_UNABLE_TO_COMPLY for failures in future.
  Cx::RegistrationTerminationAnswer rta(_msg,
                                        _cfg->dict,
                                        DIAMETER_REQ_SUCCESS,
                                        auth_session_state,
                                        _impis);

  // Send the RTA back to the HSS.
  LOG_INFO("Ready to send RTA");
  rta.send();
}

void RegistrationTerminationHandler::on_get_public_ids_success(Cache::Request* request)
{
  // Get any public IDs returned from the Cache.
  LOG_DEBUG("Extract associated public IDs from cache request");
  Cache::GetAssociatedPublicIDs* get_public_ids = (Cache::GetAssociatedPublicIDs*)request;
  get_public_ids->get_result(_impus);
  delete_identities();
}

void RegistrationTerminationHandler::on_get_public_ids_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  LOG_DEBUG("Failed to get any public IDs from the cache");
  delete_identities();
}

void RegistrationTerminationHandler::delete_identities()
{
  // If _impus is empty then we couldn't find any public IDs associated with
  // the private IDs, so there are no public IDs to delete.
  if (!_impus.empty())
  {
    LOG_INFO("Deleting public IDs from RTR");
    Cache::Request* delete_public_ids = _cfg->cache->create_DeletePublicIDs(_impus, Cache::generate_timestamp());
    HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>* public_ids_tsx = new HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>(this);
    _cfg->cache->send(public_ids_tsx, delete_public_ids);
  }

  // Delete the _impis extracted from the RTR.
  LOG_INFO("Deleting private IDs from RTR");
  Cache::Request* delete_private_ids = _cfg->cache->create_DeletePrivateIDs(_impis, Cache::generate_timestamp());
  HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>* private_ids_tsx = new HssCacheHandler::CacheTransaction<RegistrationTerminationHandler>(this);
  _cfg->cache->send(private_ids_tsx, delete_private_ids);
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
    Cache::Request* put_ims_subscription = _cfg->cache->create_PutIMSSubscription(impus, user_data, state, Cache::generate_timestamp(), _cfg->ims_sub_cache_ttl, (2 * _cfg->ims_sub_cache_ttl));
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
}
