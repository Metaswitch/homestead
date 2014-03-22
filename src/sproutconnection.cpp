/**
 * @file sproutconnection.cpp
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

#include "log.h"
#include "sproutconnection.h"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// JSON string constants
const std::string SproutConnection::JSON_REGISTRATIONS = "registrations";
const std::string SproutConnection::JSON_PRIMARY_IMPU = "primary-impu";
const std::string SproutConnection::JSON_IMPI = "impi";

SproutConnection::SproutConnection(HttpConnection* http) : _http(http)
{
}

SproutConnection::~SproutConnection()
{
  delete _http;
  _http = NULL;
}

HTTPCode SproutConnection::deregister_bindings(const bool& send_notifications,
                                               const std::vector<std::string>& default_public_ids,
                                               const std::vector<std::string>& impis,
                                               SAS::TrailId trail)
{
  std::string path = "/registrations?send-notifications=";
  path += send_notifications ? "true" : "false";

  std::string body = create_body(default_public_ids, impis);

  HTTPCode ret_code = _http->send_delete(path, body, trail);
  LOG_DEBUG("HTTP return code from Sprout: %d", ret_code);
  return ret_code;
}

std::string SproutConnection::create_body(const std::vector<std::string>& default_public_ids,
                                          const std::vector<std::string>& impis)
{
  // Utility function to create HTTP body to send to Sprout when the HSS has
  // sent a Registration-Termination request.
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

  writer.StartObject();
  {
    writer.String(JSON_REGISTRATIONS.c_str());
    writer.StartArray();
    for (std::vector<std::string>::const_iterator i = default_public_ids.begin();
         i != default_public_ids.end();
         i++)
    {
      // If we have any IMPIs specified, we need to send pairs of
      // default public IDs and private IDs. Otherwise just send a list
      // of private IDs.
      if (impis.empty())
      {
        writer.StartObject();
        {
          writer.String(JSON_PRIMARY_IMPU.c_str());
          writer.String((*i).c_str());
        }
        writer.EndObject();
      }
      else
      {
        for (std::vector<std::string>::const_iterator j = impis.begin();
             j != impis.end();
             j++)
        {
          writer.StartObject();
          {
            writer.String(JSON_PRIMARY_IMPU.c_str());
            writer.String((*i).c_str());
            writer.String(JSON_IMPI.c_str());
            writer.String((*j).c_str());
          }
          writer.EndObject();
        }
      }
    }
    writer.EndArray();
  }
  writer.EndObject();
  return sb.GetString();
}
