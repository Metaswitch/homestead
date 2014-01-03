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
#include "servercapabilities.h"

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

void HssCacheHandler::configure_diameter(Diameter::Stack* diameter_stack,
                                         const std::string& dest_realm,
                                         const std::string& dest_host,
                                         const std::string& server_name,
                                         Cx::Dictionary* dict)
{
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

void HssCacheHandler::on_diameter_timeout()
{
  _req.send_reply(503);
  delete this;
}

// General IMPI handling.

const std::string ImpiHandler::SCHEME_UNKNOWN = "unknown";
const std::string ImpiHandler::SCHEME_SIP_DIGEST = "SIP Digest";
const std::string ImpiHandler::SCHEME_DIGEST_AKAV1_MD5 = "Digest-AKAv1-MD5";

void ImpiHandler::run()
{
  if (parse_request())
  {
    if (_cfg->query_cache_av)
    {
      query_cache_av();
    }
    else
    {
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
  Cache::Request* get_av = new Cache::GetAuthVector(_impi, _impu);
  CacheTransaction* tsx = new CacheTransaction(get_av, this);
  tsx->set_success_clbk(&ImpiHandler::on_get_av_success);
  tsx->set_failure_clbk(&ImpiHandler::on_get_av_failure);
  _cache->send(tsx);
}

void ImpiHandler::on_get_av_success(Cache::Request* request)
{
  Cache::GetAuthVector* get_av = (Cache::GetAuthVector*)request;
  DigestAuthVector av;
  get_av->get_result(av);
  send_reply(av);
  delete this;
}

void ImpiHandler::on_get_av_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  if (error == Cache::ResultCode::NOT_FOUND)
  {
    get_av();
  }
  else
  {
    _req.send_reply(502);
    delete this;
  }
}

void ImpiHandler::get_av()
{
  if (_impu.empty())
  {
    if (_scheme == SCHEME_DIGEST_AKAV1_MD5)
    {
      // If the requested scheme is AKA, there's no point in looking up the cached public ID.
      // Even if we find it, we can't use it due to restrictions in the AKA protocol.
      _req.send_reply(404);
      delete this;
    }
    else
    {
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
  Cache::Request* get_public_ids = new Cache::GetAssociatedPublicIDs(_impi);
  CacheTransaction* tsx = new CacheTransaction(get_public_ids, this);
  tsx->set_success_clbk(&ImpiHandler::on_get_impu_success);
  tsx->set_failure_clbk(&ImpiHandler::on_get_impu_failure);
  _cache->send(tsx);
}

void ImpiHandler::on_get_impu_success(Cache::Request* request)
{
  Cache::GetAssociatedPublicIDs* get_public_ids = (Cache::GetAssociatedPublicIDs*)request;
  std::vector<std::string> ids;
  get_public_ids->get_result(ids);
  if (!ids.empty())
  {
    _impu = ids[0];
    send_mar();
  }
  else
  {
    _req.send_reply(404);
    delete this;
  }
}

void ImpiHandler::on_get_impu_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  if (error == Cache::ResultCode::NOT_FOUND)
  {
    _req.send_reply(404);
  }
  else
  {
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
  DiameterTransaction* tsx = new DiameterTransaction(_dict, this);
  tsx->set_response_clbk(&ImpiHandler::on_mar_response);
  mar->send(tsx, 200);
}

void ImpiHandler::on_mar_response(Diameter::Message& rsp)
{
  Cx::MultimediaAuthAnswer maa(rsp);
  switch (maa.result_code())
  {
    case 2001:
      {
        std::string sip_auth_scheme = maa.sip_auth_scheme();
        if (sip_auth_scheme == SCHEME_SIP_DIGEST)
        {
          send_reply(maa.digest_auth_vector());
          if (_cfg->impu_cache_ttl != 0)
          {
            Cache::Request* put_public_id = new Cache::PutAssociatedPublicID(_impi, _impu, Cache::generate_timestamp(), _cfg->impu_cache_ttl);
            CacheTransaction* tsx = new CacheTransaction(put_public_id, NULL);
            _cache->send(tsx);
          }
        }
        else if (sip_auth_scheme == SCHEME_DIGEST_AKAV1_MD5)
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
      _req.send_reply(404);
      break;
    default:
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
  _scheme = SCHEME_SIP_DIGEST;
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
    _scheme = SCHEME_UNKNOWN;
  }
  else if (scheme == "digest")
  {
    _scheme = SCHEME_SIP_DIGEST;
  }
  else if (scheme == "aka")
  {
    _scheme = SCHEME_DIGEST_AKAV1_MD5;
  }
  else
  {
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
  const std::string prefix = "/impi/";
  std::string path = _req.path();
  int authorization_type_int = 0;

  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impu = _req.param("impu");
  _visited_network = _req.param("visited-network");
  if (_visited_network.empty())
  {
    _visited_network = _dest_realm;
  }
  _authorization_type = _req.param("auth-type");
  if (_authorization_type == "REG")
  {
    authorization_type_int = 0;
  }
  else if (_authorization_type == "DEREG")
  {
    authorization_type_int = 1;
  }
  else if (_authorization_type == "CAPAB")
  {
    authorization_type_int = 2;
  }

  Cx::UserAuthorizationRequest* uar =
    new Cx::UserAuthorizationRequest(_dict,
                                     _dest_host,
                                     _dest_realm,
                                     _impi,
                                     _impu,
                                     _visited_network,
                                     authorization_type_int);
  DiameterTransaction* tsx = new DiameterTransaction(_dict, this);
  tsx->set_response_clbk(&ImpiRegistrationStatusHandler::on_uar_response);
  uar->send(tsx, 200);
}

void ImpiRegistrationStatusHandler::on_uar_response(Diameter::Message& rsp)
{
  Cx::UserAuthorizationAnswer uaa(rsp);
  int result_code = uaa.result_code();
  int experimental_result_code = uaa.experimental_result_code();
  if ((result_code == DIAMETER_SUCCESS) ||
      (experimental_result_code == DIAMETER_FIRST_REGISTRATION) ||
      (experimental_result_code == DIAMETER_SUBSEQUENT_REGISTRATION))
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(result_code ? result_code : experimental_result_code);
    std::string server_name = uaa.server_name();
    if (server_name.length())
    {
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
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
    _req.send_reply(404);
  }
  else if ((result_code == DIAMETER_AUTHORIZATION_REJECTED) ||
           (experimental_result_code == DIAMETER_ERROR_ROAMING_NOT_ALLOWED))
  {
    _req.send_reply(403);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    _req.send_reply(503);
  }
  else
  {
    _req.send_reply(500);
  }
  delete this;
}

//
// IMPU Location Information handling
//

void ImpuLocationInfoHandler::run()
{
  const std::string prefix = "/impu/";
  std::string path = _req.path();
  int authorization_type_int = 0;

  _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _originating = _req.param("originating");
  _authorization_type = _req.param("auth-type");
  if (_authorization_type == "CAPAB")
  {
    authorization_type_int = 2;
  }

  Cx::LocationInfoRequest* lir =
    new Cx::LocationInfoRequest(_dict,
                                _dest_host,
                                _dest_realm,
                                _originating,
                                _impu,
                                authorization_type_int);
  DiameterTransaction* tsx = new DiameterTransaction(_dict, this);
  tsx->set_response_clbk(&ImpuLocationInfoHandler::on_lir_response);
  lir->send(tsx, 200);
}

void ImpuLocationInfoHandler::on_lir_response(Diameter::Message& rsp)
{
  Cx::LocationInfoAnswer lia(rsp);
  int result_code = lia.result_code();
  int experimental_result_code = lia.experimental_result_code();
  if ((result_code == DIAMETER_SUCCESS) ||
      (experimental_result_code == DIAMETER_UNREGISTERED_SERVICE))
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(result_code ? result_code : experimental_result_code);
    std::string server_name = lia.server_name();
    if ((result_code == DIAMETER_SUCCESS) && (server_name.length()))
    {
      writer.String(JSON_SCSCF.c_str());
      writer.String(server_name.c_str());
    }
    else
    {
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
    _req.send_reply(404);
  }
  else if (result_code == DIAMETER_TOO_BUSY)
  {
    _req.send_reply(503);
  }
  else
  {
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

  Cache::Request* get_ims_sub = new Cache::GetIMSSubscription(_impu);
  CacheTransaction* tsx = new CacheTransaction(get_ims_sub, this);
  tsx->set_success_clbk(&ImpuIMSSubscriptionHandler::on_get_ims_subscription_success);
  tsx->set_failure_clbk(&ImpuIMSSubscriptionHandler::on_get_ims_subscription_failure);
  _cache->send(tsx);
}

void ImpuIMSSubscriptionHandler::on_get_ims_subscription_success(Cache::Request* request)
{
  Cache::GetIMSSubscription* get_ims_sub = (Cache::GetIMSSubscription*)request;
  std::string xml;
  get_ims_sub->get_result(xml);
  _req.add_content(xml);
  _req.send_reply(200);
  delete this;
}

void ImpuIMSSubscriptionHandler::on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text)
{
  if (error == Cache::ResultCode::NOT_FOUND)
  {
    Cx::ServerAssignmentRequest* sar =
      new Cx::ServerAssignmentRequest(_dict,
                                      _dest_host,
                                      _dest_realm,
                                      _impi,
                                      _impu,
                                      _server_name);
    DiameterTransaction* tsx = new DiameterTransaction(_dict, this);
    tsx->set_response_clbk(&ImpuIMSSubscriptionHandler::on_sar_response);
    sar->send(tsx, 200);
  }
  else
  {
    _req.send_reply(502);
    delete this;
  }
}

void ImpuIMSSubscriptionHandler::on_sar_response(Diameter::Message& rsp)
{
  Cx::ServerAssignmentAnswer saa(rsp);
  switch (saa.result_code())
  {
    case 2001:
      {
        std::string user_data = saa.user_data();
        _req.add_content(user_data);
        _req.send_reply(200);

        if (_cfg->ims_sub_cache_ttl != 0)
        {
          std::vector<std::string> public_ids = get_public_ids(user_data);
          if (!public_ids.empty())
          {
            Cache::Request* put_ims_sub = new Cache::PutIMSSubscription(public_ids, user_data, Cache::generate_timestamp(), _cfg->ims_sub_cache_ttl);
            CacheTransaction* tsx = new CacheTransaction(put_ims_sub, NULL);
            _cache->send(tsx);
          }
        }
      }
      break;
    case 5001:
      _req.send_reply(404);
      break;
    default:
      _req.send_reply(500);
      break;
  }
  delete this;
}

std::vector<std::string> ImpuIMSSubscriptionHandler::get_public_ids(const std::string& user_data)
{
  std::vector<std::string> public_ids;

  // Parse the XML document, saving off the passed-in string first (as parsing
  // is destructive).
  rapidxml::xml_document<> doc;
  char* user_data_str = doc.allocate_string(user_data.c_str());

  try
  {
    doc.parse<rapidxml::parse_strip_xml_namespaces>(user_data_str);
  }
  catch (rapidxml::parse_error err)
  {
    LOG_ERROR("Parse error in IMS Subscription document: %s\n\n%s", err.what(), user_data.c_str());
    doc.clear();
  }

  // Walk through all nodes in the hierarchy IMSSubscription->ServiceProfile->PublicIdentity
  // ->Identity.
  rapidxml::xml_node<>* is = doc.first_node("IMSSubscription");
  if (is)
  {
    for (rapidxml::xml_node<>* sp = is->first_node("ServiceProfile");
         sp;
         sp = is->next_sibling("ServiceProfile"))
    {
      for (rapidxml::xml_node<>* pi = sp->first_node("PublicIdentity");
           pi;
           pi = sp->next_sibling("PublicIdentity"))
      {
        for (rapidxml::xml_node<>* id = pi->first_node("Identity");
             id;
             id = pi->next_sibling("Identity"))
        {
          public_ids.push_back((std::string)id->value());
        }
      }
    }
  }
  
  return public_ids;
}
