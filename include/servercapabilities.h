/**
 * @file servercapabilities.h defines the ServerCapabilities structure.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef SERVERCAPABILITIES_H__
#define SERVERCAPABILITIES_H__

#include <vector>
#include <sstream>
#include <iterator>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// JSON string constants
const std::string JSON_MAN_CAP = "mandatory-capabilities";
const std::string JSON_OPT_CAP = "optional-capabilities";

struct ServerCapabilities
{
  inline ServerCapabilities(std::vector<int32_t> man_caps,
                            std::vector<int32_t> opt_caps,
                            std::string server_name)
    : mandatory_capabilities(man_caps), optional_capabilities(opt_caps), server_name(server_name) {}

  std::vector<int32_t> mandatory_capabilities;
  std::vector<int32_t> optional_capabilities;
  std::string server_name;

  // Write the server capabilities contained in this structure into a JSON object.
  // The 2 sets of capabilities are added in 2 arrays. If either set of capabilities
  // is empty, write an empty array.
  void write_capabilities(rapidjson::Writer<rapidjson::StringBuffer>* writer)
  {
    // Mandatory capabilities.
    (*writer).String(JSON_MAN_CAP.c_str());
    (*writer).StartArray();
    if (!mandatory_capabilities.empty())
    {
      for (std::vector<int32_t>::const_iterator it = mandatory_capabilities.begin();
           it != mandatory_capabilities.end();
           ++it)
      {
        (*writer).Int(*it);
      }
    }
    (*writer).EndArray();

    // Optional capabilities.
    (*writer).String(JSON_OPT_CAP.c_str());
    (*writer).StartArray();
    if (!optional_capabilities.empty())
    {
      for (std::vector<int32_t>::const_iterator it = optional_capabilities.begin();
           it != optional_capabilities.end();
           ++it)
      {
        (*writer).Int(*it);
      }
    }
    (*writer).EndArray();
    return;
  }
};

#endif
