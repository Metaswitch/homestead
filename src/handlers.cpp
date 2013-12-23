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
Cx::Dictionary HssCacheHandler::_dict;

void HssCacheHandler::configure_diameter(Diameter::Stack* diameter_stack,
                                         const std::string& dest_realm,
                                         const std::string& dest_host,
                                         const std::string& server_name)
{
  _diameter_stack = diameter_stack;
  _dest_realm = dest_realm;
  _dest_host = dest_host;
  _server_name = server_name;
}


void HssCacheHandler::on_diameter_timeout()
{
  _req.send_reply(503);
  delete this;
}

//
// IMPI digest handling.
//

void ImpiDigestHandler::run()
{
  const std::string prefix = "/impi/";
  std::string path = _req.path();

  _impi = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());
  _impu = _req.param("public_id");

  Cx::MultimediaAuthRequest* mar =
    new Cx::MultimediaAuthRequest(&_dict,
                                  _dest_realm,
                                  _dest_host,
                                  _impi,
                                  _impu,
                                  _server_name,
                                  "SIP Digest");
  DiameterTransaction* tsx = new DiameterTransaction(&_dict, this);
  tsx->set_response_clbk(&ImpiDigestHandler::on_mar_response);
  mar->send(tsx, 200);
}


void ImpiDigestHandler::on_mar_response(Diameter::Message& rsp)
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

  delete this;
}

//
// IMPI AV handling.
//

void ImpiAvHandler::run()
{
  bool request_handled = false;
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
    _req.send_reply(404);
    request_handled = true;
  }
  _impu = _req.param("impu");
  _authorization = _req.param("autn");

  if (!request_handled)
  {
    Cx::MultimediaAuthRequest* mar =
      new Cx::MultimediaAuthRequest(&_dict,
                                    _dest_realm,
                                    _dest_host,
                                    _impi,
                                    _impu,
                                    _server_name,
                                    _scheme,
                                    _authorization);
    DiameterTransaction* tsx = new DiameterTransaction(&_dict, this);
    tsx->set_response_clbk(&ImpiAvHandler::on_mar_response);
    mar->send(tsx, 200);
  }
  else
  {
    delete this;
  }
}

void ImpiAvHandler::on_mar_response(Diameter::Message& rsp)
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

  delete this;
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

  Cx::ServerAssignmentRequest* sar =
    new Cx::ServerAssignmentRequest(&_dict,
                                    _dest_host,
                                    _dest_realm,
                                    _impi,
                                    _impu,
                                    _server_name);
  DiameterTransaction* tsx = new DiameterTransaction(&_dict, this);
  tsx->set_response_clbk(&ImpuIMSSubscriptionHandler::on_sar_response);
  sar->send(tsx, 200);
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
