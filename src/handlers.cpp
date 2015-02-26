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
#include "homesteadsasevent.h"

#include "log.h"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidxml/rapidxml.hpp"
#include "boost/algorithm/string/join.hpp"

const std::string SIP_URI_PRE = "sip:";

Diameter::Stack* HssCacheTask::_diameter_stack = NULL;
std::string HssCacheTask::_dest_realm;
std::string HssCacheTask::_dest_host;
std::string HssCacheTask::_server_name;
Cx::Dictionary* HssCacheTask::_dict;
Cache* HssCacheTask::_cache = NULL;
StatisticsManager* HssCacheTask::_stats_manager = NULL;
HealthChecker* HssCacheTask::_health_checker = NULL;

const static HssCacheTask::StatsFlags DIGEST_STATS =
  static_cast<HssCacheTask::StatsFlags>(
    HssCacheTask::STAT_HSS_LATENCY |
    HssCacheTask::STAT_HSS_DIGEST_LATENCY);

const static HssCacheTask::StatsFlags SUBSCRIPTION_STATS =
  static_cast<HssCacheTask::StatsFlags>(
    HssCacheTask::STAT_HSS_LATENCY |
    HssCacheTask::STAT_HSS_SUBSCRIPTION_LATENCY);


void HssCacheTask::configure_diameter(Diameter::Stack* diameter_stack,
                                      const std::string& dest_realm,
                                      const std::string& dest_host,
                                      const std::string& server_name,
                                      Cx::Dictionary* dict)
{
  LOG_STATUS("Configuring HssCacheTask");
  LOG_STATUS("  Dest-Realm:  %s", dest_realm.c_str());
  LOG_STATUS("  Dest-Host:   %s", dest_host.c_str());
  LOG_STATUS("  Server-Name: %s", server_name.c_str());
  _diameter_stack = diameter_stack;
  _dest_realm = dest_realm;
  _dest_host = dest_host;
  _server_name = server_name;
  _dict = dict;
}

void HssCacheTask::configure_cache(Cache* cache)
{
  _cache = cache;
}

void HssCacheTask::configure_health_checker(HealthChecker* hc)
{
  _health_checker = hc;
}

void HssCacheTask::configure_stats(StatisticsManager* stats_manager)
{
  _stats_manager = stats_manager;
}

void HssCacheTask::on_diameter_timeout()
{
  send_http_reply(HTTP_GATEWAY_TIMEOUT);
  delete this;
}

// Common SAS log function

static void sas_log_get_reg_data_success(Cache::GetRegData* get_reg_data, SAS::TrailId trail)
{
  std::string xml;
  int32_t ttl;
  RegistrationState state;
  std::vector<std::string> associated_impis;
  ChargingAddresses charging_addrs;

  get_reg_data->get_xml(xml, ttl);
  get_reg_data->get_registration_state(state, ttl);
  get_reg_data->get_associated_impis(associated_impis);
  get_reg_data->get_charging_addrs(charging_addrs);

  SAS::Event event(trail, SASEvent::CACHE_GET_REG_DATA_SUCCESS, 0);
  event.add_compressed_param(xml, &SASEvent::PROFILE_SERVICE_PROFILE);
  event.add_static_param(state);
  std::string associated_impis_str = boost::algorithm::join(associated_impis, ", ");
  event.add_var_param(associated_impis_str);
  event.add_var_param(charging_addrs.log_string());
  SAS::report_event(event);
}

// General IMPI handling.

void ImpiTask::run()
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
    send_http_reply(HTTP_NOT_FOUND);
    delete this;
  }
}

void ImpiTask::query_cache_av()
{
  LOG_DEBUG("Querying cache for authentication vector for %s/%s", _impi.c_str(), _impu.c_str());
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_AV, 0);
  event.add_var_param(_impi);
  event.add_var_param(_impu);
  SAS::report_event(event);
  CassandraStore::Operation* get_av = _cache->create_GetAuthVector(_impi, _impu);
  CassandraStore::Transaction* tsx =
    new CacheTransaction(this,
                         &ImpiTask::on_get_av_success,
                         &ImpiTask::on_get_av_failure);
  _cache->do_async(get_av, tsx);
}

void ImpiTask::on_get_av_success(CassandraStore::Operation* op)
{
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_AV_SUCCESS, 0);
  SAS::report_event(event);
  Cache::GetAuthVector* get_av = (Cache::GetAuthVector*)op;
  DigestAuthVector av;
  get_av->get_result(av);
  LOG_DEBUG("Got authentication vector with digest %s from cache", av.ha1.c_str());
  send_reply(av);
  delete this;
}

void ImpiTask::on_get_av_failure(CassandraStore::Operation* op,
                                 CassandraStore::ResultCode error,
                                 std::string& text)
{
  LOG_DEBUG("Cache query failed - reject request");
  SAS::Event event(this->trail(), SASEvent::NO_AV_CACHE, 0);
  SAS::report_event(event);
  if (error == CassandraStore::NOT_FOUND)
  {
    LOG_DEBUG("No cached av found for private ID %s, public ID %s - reject", _impi.c_str(), _impu.c_str());
    send_http_reply(HTTP_NOT_FOUND);
  }
  else
  {
    LOG_DEBUG("Cache query failed with rc %d", error);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  delete this;
}

void ImpiTask::get_av()
{
  if (_impu.empty())
  {
    if (_scheme == _cfg->scheme_aka)
    {
      // If the requested scheme is AKA, there's no point in looking up the cached public ID.
      // Even if we find it, we can't use it due to restrictions in the AKA protocol.
      LOG_INFO("Public ID unknown and requested scheme AKA - reject");
      SAS::Event event(this->trail(), SASEvent::NO_IMPU_AKA, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_NOT_FOUND);
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

void ImpiTask::query_cache_impu()
{
  LOG_DEBUG("Querying cache to find public IDs associated with %s", _impi.c_str());
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU, 0);
  event.add_var_param(_impi);
  SAS::report_event(event);
  CassandraStore::Operation* get_public_ids = _cache->create_GetAssociatedPublicIDs(_impi);
  CassandraStore::Transaction* tsx =
    new CacheTransaction(this,
                         &ImpiTask::on_get_impu_success,
                         &ImpiTask::on_get_impu_failure);
  _cache->do_async(get_public_ids, tsx);
}

void ImpiTask::on_get_impu_success(CassandraStore::Operation* op)
{
  Cache::GetAssociatedPublicIDs* get_public_ids = (Cache::GetAssociatedPublicIDs*)op;
  std::vector<std::string> ids;
  get_public_ids->get_result(ids);
  if (!ids.empty())
  {
    _impu = ids[0];
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_SUCCESS, 0);
    event.add_var_param(_impu);
    SAS::report_event(event);
    LOG_DEBUG("Found cached public ID %s for private ID %s - now send Multimedia-Auth request",
              _impu.c_str(), _impi.c_str());
    send_mar();
  }
  else
  {
    LOG_INFO("No cached public ID found for private ID %s - reject", _impi.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_FAIL, 0);
    SAS::report_event(event);
    send_http_reply(HTTP_NOT_FOUND);
    delete this;
  }
}

void ImpiTask::on_get_impu_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text)
{
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_FAIL, 0);
  SAS::report_event(event);
  if (error == CassandraStore::NOT_FOUND)
  {
    LOG_DEBUG("No cached public ID found for private ID %s - reject", _impi.c_str());
    send_http_reply(HTTP_NOT_FOUND);
  }
  else
  {
    LOG_DEBUG("Cache query failed with rc %d", error);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  delete this;
}

void ImpiTask::send_mar()
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
    new DiameterTransaction(_dict, this, DIGEST_STATS, &ImpiTask::on_mar_response);
  mar.send(tsx, _cfg->diameter_timeout_ms);
}

void ImpiTask::on_mar_response(Diameter::Message& rsp)
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
          SAS::Event event(this->trail(), SASEvent::CACHE_PUT_ASSOC_IMPU, 0);
          event.add_var_param(_impi);
          event.add_var_param(_impu);
          SAS::report_event(event);
          CassandraStore::Operation* put_public_id =
            _cache->create_PutAssociatedPublicID(_impi,
                                                 _impu,
                                                 Cache::generate_timestamp(),
                                                 _cfg->impu_cache_ttl);
          CassandraStore::Transaction* tsx = new CacheTransaction;
          _cache->do_async(put_public_id, tsx);
        }
      }
      else if (sip_auth_scheme == _cfg->scheme_aka)
      {
        send_reply(maa.aka_auth_vector());
      }
      else
      {
        send_http_reply(HTTP_NOT_FOUND);
      }
    }
    break;
    case 5001:
    {
      LOG_INFO("Multimedia-Auth answer with result code %d - reject", result_code);
      SAS::Event event(this->trail(), SASEvent::NO_AV_HSS, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_NOT_FOUND);
    }
    break;
    default:
      LOG_INFO("Multimedia-Auth answer with result code %d - reject", result_code);
      SAS::Event event(this->trail(), SASEvent::NO_AV_HSS, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_SERVER_ERROR);
      break;
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
  LOG_INFO("Digest requested but AKA received - reject");
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
    _scheme = _cfg->scheme_aka;
  }
  else
  {
    LOG_INFO("Couldn't parse scheme %s", scheme.c_str());
    SAS::Event event(this->trail(), SASEvent::INVALID_SCHEME, 0);
    event.add_var_param(scheme);
    SAS::report_event(event);
    return false;
  }
  _impu = _req.param("impu");
  _authorization = _req.param("autn");

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
      new DiameterTransaction(_dict,
                              this,
                              SUBSCRIPTION_STATS,
                              &ImpiRegistrationStatusTask::on_uar_response);
    uar.send(tsx, _cfg->diameter_timeout_ms);
  }
  else
  {
    LOG_DEBUG("No HSS configured - fake response if subscriber exists");
    SAS::Event event(this->trail(), SASEvent::ICSCF_NO_HSS, 0);
    SAS::report_event(event);
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(DIAMETER_SUCCESS);
    writer.String(JSON_SCSCF.c_str());
    writer.String(_server_name.c_str());
    writer.EndObject();
    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);
    if (_health_checker)
    {
      _health_checker->health_check_passed();
    }
    delete this;
  }
}

void ImpiRegistrationStatusTask::on_uar_response(Diameter::Message& rsp)
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

      if (!server_capabilities.server_name.empty())
      {
        LOG_DEBUG("Got Server-Name %s from Capabilities AVP", server_capabilities.server_name.c_str());
        writer.String(JSON_SCSCF.c_str());
        writer.String(server_capabilities.server_name.c_str());
      }

      server_capabilities.write_capabilities(&writer);
    }
    writer.EndObject();
    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);
    if (_health_checker)
    {
      _health_checker->health_check_passed();
    }
  }
  else if ((experimental_result_code == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result_code == DIAMETER_ERROR_IDENTITIES_DONT_MATCH))
  {
    LOG_INFO("User unknown or public/private ID conflict - reject");
    sas_log_hss_failure(experimental_result_code);
    send_http_reply(HTTP_NOT_FOUND);
  }
  else if ((result_code == DIAMETER_AUTHORIZATION_REJECTED) ||
           (experimental_result_code == DIAMETER_ERROR_ROAMING_NOT_ALLOWED))
  {
    LOG_INFO("Authorization rejected due to roaming not allowed - reject");
    sas_log_hss_failure(result_code ? result_code : experimental_result_code);
    send_http_reply(HTTP_FORBIDDEN);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    LOG_INFO("HSS busy - reject");
    sas_log_hss_failure(result_code);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  else
  {
    LOG_INFO("User-Authorization answer with result %d/%d - reject",
             result_code, experimental_result_code);
    sas_log_hss_failure(result_code ? result_code : experimental_result_code);
    send_http_reply(HTTP_SERVER_ERROR);
  }
  delete this;
}

void ImpiRegistrationStatusTask::sas_log_hss_failure(int32_t result_code)
{
  SAS::Event event(this->trail(), SASEvent::REG_STATUS_HSS_FAIL, 0);
  SAS::report_event(event);
}

//
// IMPU Location Information handling
//

void ImpuLocationInfoTask::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.path();
  _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());

  if (_cfg->hss_configured)
  {
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
      new DiameterTransaction(_dict,
                              this,
                              SUBSCRIPTION_STATS,
                              &ImpuLocationInfoTask::on_lir_response);
    lir.send(tsx, _cfg->diameter_timeout_ms);
  }
  else
  {
    LOG_DEBUG("No HSS configured - fake up response if subscriber exists");
    SAS::Event event(this->trail(), SASEvent::ICSCF_NO_HSS, 0);
    SAS::report_event(event);
    query_cache_reg_data();
  }
}

void ImpuLocationInfoTask::on_lir_response(Diameter::Message& rsp)
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

      if (!server_capabilities.server_name.empty())
      {
        LOG_DEBUG("Got Server-Name %s from Capabilities AVP", server_capabilities.server_name.c_str());
        writer.String(JSON_SCSCF.c_str());
        writer.String(server_capabilities.server_name.c_str());
      }

      server_capabilities.write_capabilities(&writer);
    }
    writer.EndObject();
    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);
  }
  else if ((experimental_result_code == DIAMETER_ERROR_USER_UNKNOWN) ||
           (experimental_result_code == DIAMETER_ERROR_IDENTITY_NOT_REGISTERED))
  {
    LOG_INFO("User unknown or public/private ID conflict - reject");
    sas_log_hss_failure(experimental_result_code);
    send_http_reply(HTTP_NOT_FOUND);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    LOG_INFO("HSS busy - reject");
    sas_log_hss_failure(result_code);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  else
  {
    LOG_INFO("Location-Info answer with result %d/%d - reject",
             result_code, experimental_result_code);
    sas_log_hss_failure(result_code ? result_code : experimental_result_code);
    send_http_reply(HTTP_SERVER_ERROR);
  }
  delete this;
}

void ImpuLocationInfoTask::sas_log_hss_failure(int32_t result_code)
{
  SAS::Event event(this->trail(), SASEvent::LOC_INFO_HSS_FAIL, 0);
  SAS::report_event(event);
}

void ImpuLocationInfoTask::query_cache_reg_data()
{
  LOG_DEBUG("Querying cache for registration data for %s", _impu.c_str());
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA, 0);
  event.add_var_param(_impu);
  SAS::report_event(event);
  CassandraStore::Operation* get_reg_data = _cache->create_GetRegData(_impu);
  CassandraStore::Transaction* tsx =
    new CacheTransaction(this,
                         &ImpuLocationInfoTask::on_get_reg_data_success,
                         &ImpuLocationInfoTask::on_get_reg_data_failure);
  _cache->do_async(get_reg_data, tsx);
}

void ImpuLocationInfoTask::on_get_reg_data_success(CassandraStore::Operation* op)
{
  sas_log_get_reg_data_success((Cache::GetRegData*)op, trail());

  // GetRegData returns success even if no entry was found, but the XML will be blank in this case.
  std::string xml;
  int32_t ttl;
  ((Cache::GetRegData*)op)->get_xml(xml, ttl);
  if (!xml.empty())
  {
    LOG_DEBUG("Got IMS subscription XML from cache - fake response for server %s", _server_name.c_str());
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(DIAMETER_SUCCESS);
    writer.String(JSON_SCSCF.c_str());
    writer.String(_server_name.c_str());
    writer.EndObject();
    _req.add_content(sb.GetString());
    send_http_reply(HTTP_OK);
  }
  else
  {
    LOG_DEBUG("No IMS subscription XML found for public ID %s - reject", _impu.c_str());
    send_http_reply(HTTP_NOT_FOUND);
  }
  delete this;
}

void ImpuLocationInfoTask::on_get_reg_data_failure(CassandraStore::Operation* op,
                                                   CassandraStore::ResultCode error,
                                                   std::string& text)
{
  LOG_DEBUG("IMS subscription cache query failed: %u, %s", error, text.c_str());
  SAS::Event event(this->trail(), SASEvent::NO_REG_DATA_CACHE, 0);
  SAS::report_event(event);
  send_http_reply(HTTP_GATEWAY_TIMEOUT);
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
      LOG_ERROR("Couldn't produce an appropriate SAR - internal software error'");
      return Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION;
      // LCOV_EXCL_STOP
  }
}

ImpuRegDataTask::RequestType ImpuRegDataTask::request_type_from_body(std::string body)
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

void ImpuRegDataTask::run()
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
    _type = request_type_from_body(_req.get_rx_body());
    if (_type == RequestType::UNKNOWN)
    {
      LOG_ERROR("HTTP request contains invalid value %s for type", _req.get_rx_body().c_str());
      SAS::Event event(this->trail(), SASEvent::INVALID_REG_TYPE, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_BAD_RESULT);
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

  LOG_DEBUG ("Try to find IMS Subscription information in the cache");
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA, 0);
  event.add_var_param(_impu);
  SAS::report_event(event);
  CassandraStore::Operation* get_reg_data = _cache->create_GetRegData(_impu);
  CassandraStore::Transaction* tsx =
    new CacheTransaction(this,
                         &ImpuRegDataTask::on_get_reg_data_success,
                         &ImpuRegDataTask::on_get_reg_data_failure);
  _cache->do_async(get_reg_data, tsx);
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

void ImpuRegDataTask::on_get_reg_data_success(CassandraStore::Operation* op)
{
  LOG_DEBUG("Got IMS subscription from cache");
  Cache::GetRegData* get_reg_data = (Cache::GetRegData*)op;
  sas_log_get_reg_data_success(get_reg_data, trail());

  RegistrationState old_state;
  std::vector<std::string> associated_impis;
  int32_t ttl = 0;
  get_reg_data->get_xml(_xml, ttl);
  get_reg_data->get_registration_state(old_state, ttl);
  get_reg_data->get_associated_impis(associated_impis);
  get_reg_data->get_charging_addrs(_charging_addrs);
  bool new_binding = false;
  LOG_DEBUG("TTL for this database record is %d, IMS Subscription XML is %s, registration state is %s, and the charging addresses are %s",
            ttl,
            _xml.empty() ? "empty" : "not empty",
            regstate_to_str(old_state).c_str(),
            _charging_addrs.empty() ? "empty" : _charging_addrs.log_string().c_str());

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

  // If Sprout didn't specify a private Id on the request, we may
  // have one embedded in the cached User-Data which we can retrieve.
  // If Sprout did specify a private Id on the request, check whether
  // we have a record of this binding.
  if (_impi.empty())
  {
    _impi = XmlUtils::get_private_id(_xml);
  }
  else if ((!_xml.empty()) &&
           ((associated_impis.empty()) ||
            (std::find(associated_impis.begin(), associated_impis.end(), _impi) == associated_impis.end())))
  {
    LOG_DEBUG("Subscriber registering with new binding");
    new_binding = true;
  }

  // Split the processing depending on whether an HSS is configured.
  if (_cfg->hss_configured)
  {
    // If the subscriber is registering with a new binding, store
    // the private Id in the cache.
    if (new_binding)
    {
      LOG_DEBUG("Associating private identity %s to IRS for %s",
                _impi.c_str(),
                _impu.c_str());
      std::vector<std::string> public_ids = XmlUtils::get_public_ids(_xml);
      CassandraStore::Operation* put_associated_private_id =
        _cache->create_PutAssociatedPrivateID(public_ids,
                                              _impi,
                                              Cache::generate_timestamp(),
                                              (2 * _cfg->hss_reregistration_time));
      CassandraStore::Transaction* tsx = new CacheTransaction;
      _cache->do_async(put_associated_private_id, tsx);
    }

    if (_type == RequestType::REG)
    {
      // This message was based on a REGISTER request from Sprout. Check
      // the subscriber's state in Cassandra to determine whether this
      // is an initial registration or a re-registration. If this subscriber
      // is already registered but is registering with a new binding, we
      // still need to tell the HSS.
      if ((old_state == RegistrationState::REGISTERED) && (!new_binding))
      {
        LOG_DEBUG("Handling re-registration");
        _new_state = RegistrationState::REGISTERED;

        // We set the record's TTL to be double the --hss-reregistration-time
        // option - once half that time has elapsed, it's time to
        // re-notify the HSS.
        if (ttl < _cfg->hss_reregistration_time)
        {
          LOG_DEBUG("Sending re-registration to HSS as %d seconds have passed",
                    _cfg->hss_reregistration_time);
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
        // Send a Server-Assignment-Request and cache the response.
        LOG_DEBUG("Handling initial registration");
        _new_state = RegistrationState::REGISTERED;
        send_server_assignment_request(Cx::ServerAssignmentType::REGISTRATION);
      }
    }
    else if (_type == RequestType::CALL)
    {
      // This message was based on an initial non-REGISTER request
      // (INVITE, PUBLISH, MESSAGE etc.).
      LOG_DEBUG("Handling call");

      if (old_state == RegistrationState::NOT_REGISTERED)
      {
        // We don't know anything about this subscriber. Send a
        // Server-Assignment-Request to provide unregistered service for
        // this subscriber.
        LOG_DEBUG("Moving to unregistered state");
        _new_state = RegistrationState::UNREGISTERED;
        send_server_assignment_request(Cx::ServerAssignmentType::UNREGISTERED_USER);
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
        // Forget about this subscriber entirely and send an appropriate SAR.
        LOG_DEBUG("Handling deregistration");
        _new_state = RegistrationState::NOT_REGISTERED;
        send_server_assignment_request(sar_type_for_request(_type));
      }
      else
      {
        // We treat a deregistration for a deregistered user as an error
        // - this is useful for preventing loops, where we try and
        // continually deregister a user.
        LOG_DEBUG("Rejecting deregistration for user who was not registered");
        SAS::Event event(this->trail(), SASEvent::SUB_NOT_REG, 0);
        SAS::report_event(event);
        send_http_reply(HTTP_BAD_RESULT);
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

      // Notify the HSS, so that it removes the Auth-Pending flag.
      LOG_DEBUG("Handling authentication failure/timeout");
      send_server_assignment_request(sar_type_for_request(_type));
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
  else
  {
    // No HSS
    if (_type == RequestType::REG)
    {
      // This message was based on a REGISTER request from Sprout. Check
      // the subscriber's state in Cassandra to determine whether this
      // is an initial registration or a re-registration.
      switch (old_state)
      {
      case RegistrationState::REGISTERED:
        // No state changes in the cache are required for a re-register -
        // just respond.
        LOG_DEBUG("Handling re-registration");
        _new_state = RegistrationState::REGISTERED;
        send_reply();
        break;

      case RegistrationState::UNREGISTERED:
        // We have been locally provisioned with this subscriber, so
        // put it into REGISTERED state.
        LOG_DEBUG("Handling initial registration");
        _new_state = RegistrationState::REGISTERED;
        put_in_cache();
        send_reply();
        break;

      default:
        // We have no record of this subscriber, so they don't exist.
        LOG_DEBUG("Unrecognised subscriber");
        SAS::Event event(this->trail(), SASEvent::NO_SUB_CACHE, 0);
        SAS::report_event(event);
        send_http_reply(HTTP_NOT_FOUND);
        break;
      }
    }
    else if (_type == RequestType::CALL)
    {
      // This message was based on an initial non-REGISTER request
      // (INVITE, PUBLISH, MESSAGE etc.).
      LOG_DEBUG("Handling call");

      if (old_state == RegistrationState::NOT_REGISTERED)
      {
        // We don't know anything about this subscriber so reject
        // the request.
        send_http_reply(HTTP_NOT_FOUND);
      }
      else
      {
        // We're already assigned to handle this subscriber - respond
        // with the iFCs and whether they're in registered state or not.
        send_reply();
      }
    }
    else if (is_deregistration_request(_type))
    {
      // Sprout wants to deregister this subscriber (because of a
      // REGISTER with Expires: 0, a timeout of all bindings, a failed
      // app server, etc.).
      if (old_state == RegistrationState::REGISTERED)
      {
        // Move the subscriber into unregistered state (but retain the
        // data, as it's not stored anywhere else).
        LOG_DEBUG("Handling deregistration");
        _new_state = RegistrationState::UNREGISTERED;
        put_in_cache();
        send_reply();
      }
      else
      {
        // We treat a deregistration for a deregistered user as an error
        // - this is useful for preventing loops, where we try and
        // continually deregister a user
        LOG_DEBUG("Rejecting deregistration for user who was not registered");
        send_http_reply(HTTP_BAD_RESULT);
      }
    }
    else if (is_auth_failure_request(_type))
    {
      // Authentication failures don't change our state (if a user's
      // already registered, failing to log in with a new binding
      // shouldn't deregister them - if they're not registered and fail
      // to log in, they're already in the right state).
      LOG_DEBUG("Handling authentication failure/timeout");
      send_reply();
    }
    else
    {
      // LCOV_EXCL_START - unreachable
      LOG_ERROR("Invalid type %d", _type);
      // LCOV_EXCL_STOP - unreachable
    }
    delete this;
  }
}

void ImpuRegDataTask::send_reply()
{
  std::string xml_str;
  int rc = XmlUtils::build_ClearwaterRegData_xml(_new_state,
                                                 _xml,
                                                 _charging_addrs,
                                                 xml_str);

  if (rc == HTTP_OK)
  {
    _req.add_content(xml_str);
  }
  else
  {
    SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_INVALID, 0);
    event.add_compressed_param(_xml, &SASEvent::PROFILE_SERVICE_PROFILE);
    SAS::report_event(event);
  }

  LOG_DEBUG("Sending %d response (body was %s)", rc, _req.get_rx_body().c_str());
  send_http_reply(rc);
}

void ImpuRegDataTask::on_get_reg_data_failure(CassandraStore::Operation* op,
                                              CassandraStore::ResultCode error,
                                              std::string& text)
{
  LOG_DEBUG("IMS subscription cache query failed: %u, %s", error, text.c_str());
  SAS::Event event(this->trail(), SASEvent::NO_REG_DATA_CACHE, 0);
  SAS::report_event(event);
  if (error == CassandraStore::NOT_FOUND)
  {
    LOG_DEBUG("No IMS subscription found for public ID %s - reject", _impu.c_str());
    send_http_reply(HTTP_NOT_FOUND);
  }
  else
  {
    LOG_DEBUG("Cache query failed with rc %d", error);
    send_http_reply(HTTP_GATEWAY_TIMEOUT);
  }
  delete this;
}

void ImpuRegDataTask::send_server_assignment_request(Cx::ServerAssignmentType type)
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
    new DiameterTransaction(_dict,
                            this,
                            SUBSCRIPTION_STATS,
                            &ImpuRegDataTask::on_sar_response);
  sar.send(tsx, _cfg->diameter_timeout_ms);
}

std::vector<std::string> ImpuRegDataTask::get_associated_private_ids()
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

void ImpuRegDataTask::put_in_cache()
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
        LOG_ERROR("No SIP URI in Implicit Registration Set");
        SAS::Event event(this->trail(), SASEvent::NO_SIP_URI_IN_IRS, 0);
        event.add_compressed_param(_xml, &SASEvent::PROFILE_SERVICE_PROFILE);
        SAS::report_event(event);
        // LCOV_EXCL_STOP
      }
    }

    std::vector<std::string> associated_private_ids;
    if (_cfg->hss_configured)
    {
      associated_private_ids = get_associated_private_ids();
    }

    SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA, 0);
    std::string public_ids_str = boost::algorithm::join(public_ids, ", ");
    event.add_var_param(public_ids_str);
    event.add_compressed_param(_xml, &SASEvent::PROFILE_SERVICE_PROFILE);
    event.add_static_param(_new_state);
    std::string associated_private_ids_str = boost::algorithm::join(associated_private_ids, ", ");
    event.add_var_param(associated_private_ids_str);
    event.add_var_param(_charging_addrs.log_string());
    SAS::report_event(event);

    Cache::PutRegData* put_reg_data = _cache->create_PutRegData(public_ids,
                                                                Cache::generate_timestamp(),
                                                                ttl);
    put_reg_data->with_xml(_xml);

    if (_new_state != RegistrationState::UNCHANGED)
    {
      put_reg_data->with_reg_state(_new_state);
    }

    if (!associated_private_ids.empty())
    {
      put_reg_data->with_associated_impis(associated_private_ids);
    }

    // Don't touch the charging addresses if there is no HSS.
    if (_cfg->hss_configured)
    {
      put_reg_data->with_charging_addrs(_charging_addrs);
    }

    CassandraStore::Transaction* tsx = new CacheTransaction;
    CassandraStore::Operation*& op = (CassandraStore::Operation*&)put_reg_data;
    _cache->do_async(op, tsx);
  }
}

void ImpuRegDataTask::on_sar_response(Diameter::Message& rsp)
{
  Cx::ServerAssignmentAnswer saa(rsp);
  int32_t result_code = 0;
  saa.result_code(result_code);
  LOG_DEBUG("Received Server-Assignment answer with result code %d", result_code);

  // Even if the HSS rejects our deregistration request, we should
  // still delete our cached data - this reflects the fact that Sprout
  // has no bindings for it.
  if (is_deregistration_request(_type))
  {
    SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_SUCCESS, 0);
    SAS::report_event(event);
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

      SAS::Event event(this->trail(), SASEvent::CACHE_DELETE_IMPUS, 0);
      std::string public_ids_str = boost::algorithm::join(public_ids, ", ");
      event.add_var_param(public_ids_str);
      std::vector<std::string> associated_private_ids = get_associated_private_ids();
      std::string associated_private_ids_str = boost::algorithm::join(associated_private_ids, ", ");
      event.add_var_param(associated_private_ids_str);
      SAS::report_event(event);
      CassandraStore::Operation* delete_public_id =
        _cache->create_DeletePublicIDs(public_ids,
                                       associated_private_ids,
                                       Cache::generate_timestamp());
      CassandraStore::Transaction* tsx = new CacheTransaction;
      _cache->do_async(delete_public_id, tsx);
    }
  }

  switch (result_code)
  {
    case 2001:
      // Get the charging addresses.
      saa.charging_addrs(_charging_addrs);

      // If we expect this request to assign the user to us (i.e. it
      // isn't triggered by a deregistration or a failure) we should
      // cache the User-Data.
      if (!is_deregistration_request(_type) && !is_auth_failure_request(_type))
      {
        LOG_DEBUG("Getting User-Data from SAA for cache");
        saa.user_data(_xml);
        put_in_cache();
      }
      send_reply();
      break;
    case 5001:
    {
      LOG_INFO("Server-Assignment answer with result code %d - reject", result_code);
      SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_FAIL, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_NOT_FOUND);
    }
    break;
    default:
      LOG_INFO("Server-Assignment answer with result code %d - reject", result_code);
      SAS::Event event(this->trail(), SASEvent::REG_DATA_HSS_FAIL, 0);
      SAS::report_event(event);
      send_http_reply(HTTP_SERVER_ERROR);
      break;
  }
  delete this;
  return;
}

//
// IMPU IMS Subscription handling for URLs of the form
// "/impu/<public ID>". Deprecated.
//

void ImpuIMSSubscriptionTask::run()
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
  CassandraStore::Operation* get_reg_data = _cache->create_GetRegData(_impu);
  CassandraStore::Transaction* tsx =
    new CacheTransaction(this,
                         &ImpuRegDataTask::on_get_reg_data_success,
                         &ImpuRegDataTask::on_get_reg_data_failure);
  _cache->do_async(get_reg_data, tsx);
}

void ImpuIMSSubscriptionTask::send_reply()
{
  if (_xml != "")
  {
    LOG_DEBUG("Building 200 OK response to send");
    _req.add_content(_xml);
    send_http_reply(HTTP_OK);
  }
  else
  {
    LOG_DEBUG("No XML User-Data available, returning 404");
    send_http_reply(HTTP_NOT_FOUND);
  }
}

void RegistrationTerminationTask::run()
{
  // Save off the deregistration reason and all private and public
  // identities on the request.
  _deregistration_reason = _rtr.deregistration_reason();
  std::string impi = _rtr.impi();
  _impis.push_back(impi);
  std::vector<std::string> associated_identities = _rtr.associated_identities();
  _impis.insert(_impis.end(), associated_identities.begin(), associated_identities.end());
  if ((_deregistration_reason != SERVER_CHANGE) &&
      (_deregistration_reason != NEW_SERVER_ASSIGNED))
  {
    // We're not interested in the public identities on the request
    // if deregistration reason is SERVER_CHANGE or NEW_SERVER_ASSIGNED.
    // We'll find some public identities later, and we want _impus to be empty
    // for now.
    _impus = _rtr.impus();
  }

  LOG_INFO("Received Regestration-Termination request with dereg reason %d",
           _deregistration_reason);

  if ((_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                           (_deregistration_reason == REMOVE_SCSCF) ||
                           (_deregistration_reason == SERVER_CHANGE) ||
                           (_deregistration_reason == NEW_SERVER_ASSIGNED)))
  {
    // Find all the default public identities associated with the
    // private identities specified on the request.
    std::string impis_str = boost::algorithm::join(_impis, ", ");
    LOG_DEBUG("Finding associated default public identities for impis %s", impis_str.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_PRIMARY_IMPUS, 0);
    event.add_var_param(impis_str);
    SAS::report_event(event);
    CassandraStore::Operation* get_associated_impus = _cfg->cache->create_GetAssociatedPrimaryPublicIDs(_impis);
    CassandraStore::Transaction* tsx =
      new CacheTransaction(this,
                           &RegistrationTerminationTask::get_assoc_primary_public_ids_success,
                           &RegistrationTerminationTask::get_assoc_primary_public_ids_failure);
    _cfg->cache->do_async(get_associated_impus, tsx);
  }
  else if ((!_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                                 (_deregistration_reason == REMOVE_SCSCF)))
  {
    // Find information about the registration sets for the public
    // identities specified on the request.
    get_registration_sets();
  }
  else
  {
    // This is either an invalid deregistration reason.
    LOG_ERROR("Registration-Termination request received with invalid deregistration reason %d",
              _deregistration_reason);
    SAS::Event event(this->trail(), SASEvent::INVALID_DEREG_REASON, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_FAILURE);
    delete this;
  }
}

void RegistrationTerminationTask::get_assoc_primary_public_ids_success(CassandraStore::Operation* op)
{
  // Get the default public identities returned by the cache.
  Cache::GetAssociatedPrimaryPublicIDs* get_associated_impus_result =
    (Cache::GetAssociatedPrimaryPublicIDs*)op;
  get_associated_impus_result->get_result(_impus);

  if (_impus.empty())
  {
    LOG_DEBUG("No registered IMPUs to deregister found");
    SAS::Event event(this->trail(), SASEvent::NO_IMPU_DEREG, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_SUCCESS);
    delete this;
  }
  else
  {
    // We have all the default public identities. Find their registration sets.
    // Remove any duplicates first. We do this by sorting the vector, using unique
    // to move the unique values to the front and erasing everything after the last
    // unique value.
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_PRIMARY_IMPUS_SUCCESS, 0);
    std::string impus_str = boost::algorithm::join(_impus, ", ");
    event.add_var_param(impus_str);
    SAS::report_event(event);
    sort(_impus.begin(), _impus.end());
    _impus.erase(unique(_impus.begin(), _impus.end()), _impus.end());
    get_registration_sets();
  }
}

void RegistrationTerminationTask::get_assoc_primary_public_ids_failure(CassandraStore::Operation* op,
                                                                       CassandraStore::ResultCode error,
                                                                       std::string& text)
{
  LOG_DEBUG("Failed to get associated default public identities");
  SAS::Event event(this->trail(), SASEvent::DEREG_SUCCESS, 0);
  SAS::report_event(event);
  send_rta(DIAMETER_REQ_FAILURE);
  delete this;
}

void RegistrationTerminationTask::get_registration_sets()
{
  // This function issues a GetRegData cache request for a public identity
  // on the list of IMPUs and then removes that public identity from the list. It
  // should get called again after the cache response by the callback functions.
  // Once there are no public identities remaining, it deletes the registrations.
  if (!_impus.empty())
  {
    std::string impu = _impus.back();
    _impus.pop_back();
    LOG_DEBUG("Finding registration set for public identity %s", impu.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA, 0);
    event.add_var_param(impu);
    SAS::report_event(event);
    CassandraStore::Operation* get_reg_data = _cfg->cache->create_GetRegData(impu);
    CassandraStore::Transaction* tsx =
      new CacheTransaction(this,
                           &RegistrationTerminationTask::get_registration_set_success,
                           &RegistrationTerminationTask::get_registration_set_failure);
    _cfg->cache->do_async(get_reg_data, tsx);
  }
  else if (_registration_sets.empty())
  {
    LOG_DEBUG("No registered IMPUs to deregister found");
    SAS::Event event(this->trail(), SASEvent::NO_IMPU_DEREG, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_SUCCESS);
    delete this;
  }
  else
  {
    // We now have all the registration sets, and we can delete the registrations.
    // First remove any duplicates in the list of _impis. We do this
    // by sorting the vector, using unique to move the unique values to the front
    // and erasing everything after the last unique value.
    sort(_impis.begin(), _impis.end());
    _impis.erase(unique(_impis.begin(), _impis.end()), _impis.end());

    delete_registrations();
  }
}

void RegistrationTerminationTask::get_registration_set_success(CassandraStore::Operation* op)
{
  Cache::GetRegData* get_reg_data_result = (Cache::GetRegData*)op;
  sas_log_get_reg_data_success(get_reg_data_result, trail());
  std::string ims_sub;
  int32_t temp;
  get_reg_data_result->get_xml(ims_sub, temp);

  // Add the list of public identities in the IMS subscription to
  // the list of registration sets..
  std::vector<std::string> public_ids = XmlUtils::get_public_ids(ims_sub);
  if (!public_ids.empty())
  {
    _registration_sets.push_back(public_ids);
  }

  if ((_deregistration_reason == SERVER_CHANGE) ||
      (_deregistration_reason == NEW_SERVER_ASSIGNED))
  {
    // GetRegData also returns a list of associated private
    // identities. Save these off.
    std::vector<std::string> associated_impis;
    get_reg_data_result->get_associated_impis(associated_impis);
    std::string associated_impis_str = boost::algorithm::join(associated_impis, ", ");
    LOG_DEBUG("GetRegData returned associated identites: %s",
              associated_impis_str.c_str());
    _impis.insert(_impis.end(),
                  associated_impis.begin(),
                  associated_impis.end());
  }

  // Call back into get_registration_sets
  get_registration_sets();
}

void RegistrationTerminationTask::get_registration_set_failure(CassandraStore::Operation* op,
                                                               CassandraStore::ResultCode error,
                                                               std::string& text)
{
  LOG_DEBUG("Failed to get a registration set - report failure to HSS");
  SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
  SAS::report_event(event);
  send_rta(DIAMETER_REQ_FAILURE);
  delete this;
}

void RegistrationTerminationTask::delete_registrations()
{
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
    ret_code = _cfg->sprout_conn->deregister_bindings(false,
                                                      default_public_identities,
                                                      _impis,
                                                      this->trail());
    break;

  case REMOVE_SCSCF:
  case SERVER_CHANGE:
    ret_code = _cfg->sprout_conn->deregister_bindings(true,
                                                      default_public_identities,
                                                      empty_vector,
                                                      this->trail());
    break;

  case NEW_SERVER_ASSIGNED:
    ret_code = _cfg->sprout_conn->deregister_bindings(false,
                                                      default_public_identities,
                                                      empty_vector,
                                                      this->trail());
    break;

  default:
    // LCOV_EXCL_START - We can't get here because we've already filtered these out.
    LOG_ERROR("Unexpected deregistration reason %d on RTR", _deregistration_reason);
    break;
    // LCOV_EXCL_STOP
  }

  switch (ret_code)
  {
  case HTTP_OK:
  {
    LOG_DEBUG("Send Registration-Termination answer indicating success");
    SAS::Event event(this->trail(), SASEvent::DEREG_SUCCESS, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_SUCCESS);
  }
  break;

  case HTTP_BADMETHOD:
  case HTTP_BAD_RESULT:
  case HTTP_SERVER_ERROR:
  {
    LOG_DEBUG("Send Registration-Termination answer indicating failure");
    SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_FAILURE);
  }
  break;

  default:
  {
    LOG_ERROR("Unexpected HTTP return code, send Registration-Termination answer indicating failure");
    SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_FAILURE);
  }
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

void RegistrationTerminationTask::dissociate_implicit_registration_sets()
{
  // Dissociate the private identities from each registration set.
  for (std::vector<std::vector<std::string>>::iterator i = _registration_sets.begin();
       i != _registration_sets.end();
       i++)
  {
    SAS::Event event(this->trail(), SASEvent::CACHE_DISASSOC_REG_SET, 0);
    std::string reg_set_str = boost::algorithm::join(*i, ", ");
    event.add_var_param(reg_set_str);
    std::string impis_str = boost::algorithm::join(_impis, ", ");
    event.add_var_param(impis_str);
    SAS::report_event(event);
    CassandraStore::Operation* dissociate_reg_set =
      _cfg->cache->create_DissociateImplicitRegistrationSetFromImpi(*i, _impis, Cache::generate_timestamp());
    CassandraStore::Transaction* tsx = new CacheTransaction;
    _cfg->cache->do_async(dissociate_reg_set, tsx);
  }
}

void RegistrationTerminationTask::delete_impi_mappings()
{
  // Delete rows from the IMPI table for all associated IMPIs.
  std::string _impis_str = boost::algorithm::join(_impis, ", ");
  LOG_DEBUG("Deleting IMPI mappings for the following IMPIs: %s",
            _impis_str.c_str());
  SAS::Event event(this->trail(), SASEvent::CACHE_DELETE_IMPI_MAP, 0);
  event.add_var_param(_impis_str);
  SAS::report_event(event);
  CassandraStore::Operation* delete_impis =
    _cfg->cache->create_DeleteIMPIMapping(_impis, Cache::generate_timestamp());
  CassandraStore::Transaction* tsx = new CacheTransaction;
  _cfg->cache->do_async(delete_impis, tsx);
}

void RegistrationTerminationTask::send_rta(const std::string result_code)
{
  // Use our Cx layer to create a RTA object and add the correct AVPs. The RTA is
  // created from the RTR.
  Cx::RegistrationTerminationAnswer rta(_rtr,
                                        _cfg->dict,
                                        result_code,
                                        _msg.auth_session_state(),
                                        _impis);

  // Send the RTA back to the HSS.
  LOG_INFO("Ready to send RTA");
  rta.send(trail());
}

void PushProfileTask::run()
{
  // Received a Push Profile Request. We may need to update an IMS
  // subscription or charging address information in the cache.
  _ims_sub_present = _ppr.user_data(_ims_subscription);
  _charging_addrs_present = _ppr.charging_addrs(_charging_addrs);

  // If we have charging addresses but no IMS subscription, we need
  // to lookup which public IDs need updating based on the private ID
  // specified in the PPR.
  if ((_charging_addrs_present) && (!_ims_sub_present))
  {
    _impi = _ppr.impi();
    LOG_DEBUG("Querying cache to find public IDs associated with %s", _impi.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU, 0);
    event.add_var_param(_impi);
    SAS::report_event(event);
    CassandraStore::Operation* get_public_ids =
      _cfg->cache->create_GetAssociatedPublicIDs(_impi);
    CassandraStore::Transaction* tsx =
      new CacheTransaction(this,
                           &PushProfileTask::on_get_impus_success,
                           &PushProfileTask::on_get_impus_failure);
    _cfg->cache->do_async(get_public_ids, tsx);
  }
  else
  {
    update_reg_data();
  }
}

void PushProfileTask::on_get_impus_success(CassandraStore::Operation* op)
{
  Cache::GetAssociatedPublicIDs* get_public_ids = (Cache::GetAssociatedPublicIDs*)op;
  get_public_ids->get_result(_impus);
  if (!_impus.empty())
  {
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_SUCCESS, 0);
    event.add_var_param(_impus[0]);
    SAS::report_event(event);
    if (Log::enabled(Log::DEBUG_LEVEL))
    {
      // LCOV_EXCL_START - clearly we only go through this code when debug logging
      // is turned on.
      std::string impus_str = boost::algorithm::join(_impus, ", ");
      LOG_DEBUG("Found cached public IDs %s for private ID %s",
                impus_str.c_str(), _impi.c_str());
      // LCOV_EXCL_STOP
    }
    update_reg_data();
  }
  else
  {
    LOG_INFO("No cached public IDs found for private ID %s - failed to update charging addresses",
             _impi.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_FAIL, 0);
    SAS::report_event(event);
    send_ppa(DIAMETER_REQ_FAILURE);
  }
}

void PushProfileTask::on_get_impus_failure(CassandraStore::Operation* op,
                                           CassandraStore::ResultCode error,
                                           std::string& text)
{
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_ASSOC_IMPU_FAIL, 0);
  SAS::report_event(event);
  LOG_DEBUG("Cache query failed with rc %d", error);
  send_ppa(DIAMETER_REQ_FAILURE);
}

void PushProfileTask::update_reg_data()
{
  if ((_ims_sub_present) || (_charging_addrs_present))
  {
    // If we don't have any public IDs yet, we need to get them
    // out of the IMS subscription.
    if (_impus.empty())
    {
      _impus = XmlUtils::get_public_ids(_ims_subscription);

      // We should check the IRS contains a SIP URI and throw an error log if
      // it doesn't. We continue as normal even if it doesn't.
      bool found_sip_uri = false;

      for (std::vector<std::string>::iterator it = _impus.begin();
           (it != _impus.end()) && (!found_sip_uri);
           ++it)
      {
        if ((*it).compare(0, SIP_URI_PRE.length(), SIP_URI_PRE) == 0)
        {
          found_sip_uri = true;
        }
      }

      if (!found_sip_uri)
      {
        LOG_ERROR("No SIP URI in Implicit Registration Set");
        SAS::Event event(this->trail(), SASEvent::NO_SIP_URI_IN_IRS, 0);
        event.add_compressed_param(_ims_subscription, &SASEvent::PROFILE_SERVICE_PROFILE);
        SAS::report_event(event);
      }
    }

    // Create the cache request object and a SAS event simultaneously.
    Cache::PutRegData* put_reg_data =
      _cfg->cache->create_PutRegData(_impus,
                                     Cache::generate_timestamp(),
                                     (2 * _cfg->hss_reregistration_time));
    SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA, 0);

    std::string impus_str = boost::algorithm::join(_impus, ", ");
    event.add_var_param(impus_str);

    if (_ims_sub_present)
    {
      LOG_INFO("Updating IMS subscription from PPR");
      put_reg_data->with_xml(_ims_subscription);
      event.add_compressed_param(_ims_subscription, &SASEvent::PROFILE_SERVICE_PROFILE);
    }
    else
    {
      event.add_compressed_param("IMS subscription unchanged", &SASEvent::PROFILE_SERVICE_PROFILE);
    }

    event.add_static_param(RegistrationState::UNCHANGED);
    event.add_var_param("");

    if (_charging_addrs_present)
    {
      LOG_INFO("Updating charging addresses from PPR");
      event.add_var_param(_charging_addrs.log_string());
      put_reg_data->with_charging_addrs(_charging_addrs);
    }
    else
    {
      event.add_var_param("Charging addresses unchanged");
    }

    CassandraStore::Transaction* tsx =
      new CacheTransaction(this,
                           &PushProfileTask::update_reg_data_success,
                           &PushProfileTask::update_reg_data_failure);
    CassandraStore::Operation*& op = (CassandraStore::Operation*&)put_reg_data;
    _cfg->cache->do_async(op, tsx);

    SAS::report_event(event);

  }
  else
  {
    send_ppa(DIAMETER_REQ_SUCCESS);
  }
}

void PushProfileTask::update_reg_data_success(CassandraStore::Operation* op)
{
  SAS::Event event(this->trail(), SASEvent::UPDATED_REG_DATA, 0);
  SAS::report_event(event);
  send_ppa(DIAMETER_REQ_SUCCESS);
}

void PushProfileTask::update_reg_data_failure(CassandraStore::Operation* op,
                                              CassandraStore::ResultCode error,
                                              std::string& text)
{
  LOG_DEBUG("Failed to update registration data - report failure to HSS");
  send_ppa(DIAMETER_REQ_FAILURE);
}

void PushProfileTask::send_ppa(const std::string result_code)
{
  // Use our Cx layer to create a PPA object and add the correct AVPs. The PPA is
  // created from the PPR.
  Cx::PushProfileAnswer ppa(_ppr,
                            _cfg->dict,
                            result_code,
                            _msg.auth_session_state());

  // Send the PPA back to the HSS.
  LOG_INFO("Ready to send PPA");
  ppa.send(trail());

  delete this;
}
