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

void PingHandler::handle(HttpStack::Request& req)
{
  req.add_content("OK");
  req.send_reply(200);
}

void DiameterHttpHandler::Transaction::on_timeout()
{
  _req.send_reply(503);
}

void ImpiDigestHandler::handle(HttpStack::Request& req)
{
  const std::string prefix = "/impi/";
  std::string path = req.path();
  std::string impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  std::string impu = req.param("public_id");
  Cx::MultimediaAuthRequest* mar = new Cx::MultimediaAuthRequest(&_dict, _dest_realm, _dest_host, impi, impu, _server_name, "SIP Digest");
  Transaction* tsx = new Transaction(&_dict, req);
  mar->send(tsx, 200);
}

void ImpiDigestHandler::Transaction::on_response(Diameter::Message& rsp)
{
  Cx::MultimediaAuthAnswer maa(rsp);
  switch (maa.result_code())
  {
    case 2001:
      if (maa.sip_auth_scheme() == "SIP Digest")
      {
        DigestAuthVector digest_auth_vector = maa.digest_auth_vector();
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        writer.StartObject();
        writer.String("digest_ha1");
        writer.String(digest_auth_vector.ha1.c_str());
        writer.EndObject();
        _req.add_content(sb.GetString());
        _req.send_reply(200);
      }
      else
      {
        _req.send_reply(404);
      }
      break;
    case 5001:
      _req.send_reply(404);
      break;
    default:
      _req.send_reply(500);
      break;
  }
}

void ImpiAvHandler::handle(HttpStack::Request& req)
{
  const std::string prefix = "/impi/";
  std::string path = req.path();
  std::string impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  std::string scheme = req.file();
  if (scheme == "av")
  {
    scheme = "unknown";
  }
  else if (scheme == "digest")
  {
    scheme = "SIP Digest";
  }
  else if (scheme == "aka")
  {
    scheme = "Digest-AKAv1-MD5";
  }
  else
  {
    req.send_reply(404);
    return;
  }
  std::string impu = req.param("impu");
  std::string authorization = req.param("autn");
  Cx::MultimediaAuthRequest* mar = new Cx::MultimediaAuthRequest(&_dict, _dest_realm, _dest_host, impi, impu, _server_name, scheme, authorization);
  Transaction* tsx = new Transaction(&_dict, req);
  mar->send(tsx, 200);
}

void ImpiAvHandler::Transaction::on_response(Diameter::Message& rsp)
{
  Cx::MultimediaAuthAnswer maa(rsp);
  switch (maa.result_code())
  {
    case 2001:
      {
        std::string sip_auth_scheme = maa.sip_auth_scheme();
        if (sip_auth_scheme == "SIP Digest")
        {
          DigestAuthVector digest_auth_vector = maa.digest_auth_vector();
          digest_auth_vector.qop = (!digest_auth_vector.qop.empty()) ? digest_auth_vector.qop : "auth";

          rapidjson::StringBuffer sb;
          rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

          writer.StartObject();
          {
            writer.String("digest");
            writer.StartObject();
            {
              writer.String("ha1");
              writer.String(digest_auth_vector.ha1.c_str());
              writer.String("realm");
              writer.String(digest_auth_vector.realm.c_str());
              writer.String("qop");
              writer.String(digest_auth_vector.qop.c_str());
            }
            writer.EndObject();
          }
          writer.EndObject();

          _req.add_content(sb.GetString());
          _req.send_reply(200);
        }
        else if (sip_auth_scheme == "Digest-AKAv1-MD5")
        {
          AKAAuthVector aka_auth_vector = maa.aka_auth_vector();
          rapidjson::StringBuffer sb;
          rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

          writer.StartObject();
          {
            writer.String("aka");
            writer.StartObject();
            {
              writer.String("challenge");
              writer.String(aka_auth_vector.challenge.c_str());
              writer.String("response");
              writer.String(aka_auth_vector.response.c_str());
              writer.String("cryptkey");
              writer.String(aka_auth_vector.crypt_key.c_str());
              writer.String("integritykey");
              writer.String(aka_auth_vector.integrity_key.c_str());
            }
            writer.EndObject();
          }
          writer.EndObject();

          _req.add_content(sb.GetString());
          _req.send_reply(200);
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
}
