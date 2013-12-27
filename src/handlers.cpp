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

void ImpiHandler::run()
{
  if (parse_request())
  {
    // TODO: Decide how this is enabled - we never actually cache this, so the only reason for a lookup is for non-HSS operation.
    if (true)
    {
      query_cache();
    }
    else
    {
      send_mar();
    }
  }
  else
  {
    _req.send_reply(404);
    delete this;
  }
}

void ImpiHandler::query_cache()
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
    send_mar();
  }
  else
  {
    _req.send_reply(502);
    delete this;
  }
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
        if (sip_auth_scheme == "SIP Digest")
        {
          send_reply(maa.digest_auth_vector());
        }
        else if (sip_auth_scheme == "Digest-AKAv1-MD5")
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
  _scheme = "SIP Digest";
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
  _scheme = _req.file();
  if (_scheme == "av")
  {
    _scheme = "unknown";
  }
  else if (_scheme == "digest")
  {
    _scheme = "SIP Digest";
  }
  else if (_scheme == "aka")
  {
    _scheme = "Digest-AKAv1-MD5";
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
// IMPU handling
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

        std::vector<std::string> public_ids = get_public_ids(user_data);
        if (!public_ids.empty())
        {
          // TODO: Make TTL configurable.
          Cache::Request* put_ims_sub = new Cache::PutIMSSubscription(public_ids, user_data, Cache::generate_timestamp(), 3600);
          CacheTransaction* tsx = new CacheTransaction(put_ims_sub, NULL);
          _cache->send(tsx);
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
