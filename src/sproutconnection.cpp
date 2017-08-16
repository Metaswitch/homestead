/**
 * @file sproutconnection.cpp
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "log.h"
#include "sproutconnection.h"

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// JSON string constants
const std::string SproutConnection::JSON_REGISTRATIONS = "registrations";
const std::string SproutConnection::JSON_PRIMARY_IMPU = "primary-impu";
const std::string SproutConnection::JSON_IMPI = "impi";
const std::string SproutConnection::JSON_ASSOCIATED_IDENTITIES = "associated-identities";

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

  HTTPCode ret_code = _http->send_delete(path, trail, body);
  TRC_DEBUG("HTTP return code from Sprout: %d", ret_code);
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

HTTPCode SproutConnection::change_associated_identities(const std::string& default_id,
							const std::vector<std::string>& impus,
							SAS::TrailId trail)
{
  std::string path = "/registrations/" + default_id;
  std::string body = change_ids_create_body(impus);
  TRC_DEBUG("JSON Body is %s", body.c_str());
  HTTPCode ret_code = _http->send_post(path, body, trail);
  TRC_DEBUG("HTTP return code from Sprout: %d", ret_code);
  return ret_code;
}

std::string SproutConnection::change_ids_create_body(const std::vector<std::string>& impus)
{
  // Utility function to create HTTP body to send to Sprout
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

  writer.StartObject();
  {
    writer.String(JSON_ASSOCIATED_IDENTITIES.c_str());
    writer.StartArray();
    for (std::vector<std::string>::const_iterator i = impus.begin();
	 i != impus.end();
         i++)
    {
      writer.String((*i).c_str());
    }
    writer.EndArray();
  }
  writer.EndObject();
  return sb.GetString();
}

