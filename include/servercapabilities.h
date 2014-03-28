/**
 * @file servercapabilities.h defines the ServerCapabilities structure.
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
                            std::vector<int32_t> opt_caps) : mandatory_capabilities(man_caps), optional_capabilities(opt_caps) {}

  std::vector<int32_t> mandatory_capabilities;
  std::vector<int32_t> optional_capabilities;

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
