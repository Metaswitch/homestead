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

void ImpiDigestHandler::handle(HttpStack::Request& req)
{
  std::string impi = req.path().substr(sizeof("/impi/") - 1, req.path().length() - sizeof("/impi/"));
  std::string impu = req.param("impu");
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
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        writer.StartObject();
        writer.String("digest_ha1");
        writer.String(maa.digest_ha1().c_str());
        writer.EndObject();
        _req.add_content(sb.GetString());
      }
      _req.send_reply(200);
      break;
    case 5001:
      _req.send_reply(404);
      break;
    default:
      _req.send_reply(500);
      break;
  }
}

void ImpiDigestHandler::Transaction::on_timeout()
{
  _req.send_reply(503);
}
