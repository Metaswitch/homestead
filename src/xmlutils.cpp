/**
 * @file xmlutils.cpp class containing XML utilities
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

#include "xmlutils.h"

#include "log.h"

#include "rapidxml/rapidxml.hpp"

namespace XmlUtils
{
std::vector<std::string> get_public_ids(const std::string& user_data)
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
}
