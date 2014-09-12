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
#include "rapidxml/rapidxml_print.hpp"

const char* CCF = "CCF";
const char* ECF = "ECF";
const char* PRIORITY = "priority";
const char* PRIORITY_1 = "1";
const char* PRIORITY_2 = "2";

namespace XmlUtils
{

// Builds a ClearwaterRegData XML document for passing to Sprout,
// based on the given registration state and User-Data XML from the HSS.
std::string build_ClearwaterRegData_xml(RegistrationState state,
                                        std::string xml,
                                        const ChargingAddresses& charging_addrs)
{
  rapidxml::xml_document<> doc;

  rapidxml::xml_node<>* root = doc.allocate_node(rapidxml::node_type::node_element, "ClearwaterRegData");
  std::string regtype;
  if (state == RegistrationState::REGISTERED)
  {
    regtype = "REGISTERED";
  }
  else if (state == RegistrationState::UNREGISTERED)
  {
    regtype = "UNREGISTERED";
  }
  else
  {
    if (state != RegistrationState::NOT_REGISTERED)
    {
      LOG_ERROR("Invalid registration state %d", state);
    }
    regtype = "NOT_REGISTERED";
  }

  rapidxml::xml_node<>* reg = doc.allocate_node(rapidxml::node_type::node_element, "RegistrationState", regtype.c_str());

  root->append_node(reg);

  if (xml != "")
  {
    // Parse the XML document, saving off the passed-in string first (as parsing
    // is destructive).

    rapidxml::xml_document<> prev_doc;

    // This doesn't need freeing - prev_doc is on the stack, and this
    // uses its memory pool.
    char* user_data_str = prev_doc.allocate_string(xml.c_str());
    rapidxml::xml_node<>* is = NULL;

    try
    {
      prev_doc.parse<rapidxml::parse_strip_xml_namespaces>(user_data_str);
      is = doc.clone_node(prev_doc.first_node("IMSSubscription"));
    }
    catch (rapidxml::parse_error err)
    {
      LOG_ERROR("Parse error in IMS Subscription document: %s\n\n%s", err.what(), xml.c_str());
      prev_doc.clear();
    }
    if (is != NULL)
    {
      root->append_node(is);
    }
  }

  if (!charging_addrs.empty())
  {
    rapidxml::xml_node<>* cfs = doc.allocate_node(rapidxml::node_type::node_element, "ChargingAddresses");
    if (!charging_addrs.ccfs.empty())
    {
      rapidxml::xml_node<>* pccfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                      CCF,
                                                      charging_addrs.ccfs[0].c_str());
      rapidxml::xml_attribute<>* pccf = doc.allocate_attribute(PRIORITY, PRIORITY_1);
      pccfn->append_attribute(pccf);
      cfs->append_node(pccfn);
    }
    if (charging_addrs.ccfs.size() > 1)
    {
      rapidxml::xml_node<>* sccfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                      CCF,
                                                      charging_addrs.ccfs[1].c_str());
      rapidxml::xml_attribute<>* sccf = doc.allocate_attribute(PRIORITY, PRIORITY_2);
      sccfn->append_attribute(sccf);
      cfs->append_node(sccfn);
    }
    if (!charging_addrs.ecfs.empty())
    {
      rapidxml::xml_node<>* pecfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                      ECF,
                                                      charging_addrs.ecfs[0].c_str());
      rapidxml::xml_attribute<>* pecf = doc.allocate_attribute(PRIORITY, PRIORITY_1);
      pecfn->append_attribute(pecf);
      cfs->append_node(pecfn);
    }
    if (charging_addrs.ecfs.size() > 1)
    {
      rapidxml::xml_node<>* secfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                      ECF,
                                                      charging_addrs.ecfs[1].c_str());
      rapidxml::xml_attribute<>* secf = doc.allocate_attribute(PRIORITY, PRIORITY_2);
      secfn->append_attribute(secf);
      cfs->append_node(secfn);
    }
    root->append_node(cfs);
  }

  doc.append_node(root);
  std::string out;
  rapidxml::print(std::back_inserter(out), doc, 0);
  return out;
}

// Parses the given User-Data XML to retrieve a list of all the public IDs.
std::vector<std::string> get_public_ids(const std::string& user_data)
{
  std::vector<std::string> public_ids;

  // Parse the XML document, saving off the passed-in string first (as parsing
  // is destructive).
  rapidxml::xml_document<> doc;

  // This doesn't need freeing - doc is on the stack, and this
  // uses its memory pool.
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
         sp = sp->next_sibling("ServiceProfile"))
    {
      for (rapidxml::xml_node<>* pi = sp->first_node("PublicIdentity");
           pi;
           pi = pi->next_sibling("PublicIdentity"))
      {
        rapidxml::xml_node<>* id = pi->first_node("Identity");
        if (id)
        {
          public_ids.push_back((std::string)id->value());
        }
        else
        {
          LOG_WARNING("PublicIdentity node was missing Identity child: %s", user_data.c_str());
        }
      }
    }
  }

  if (public_ids.size() == 0)
  {
    LOG_ERROR("Failed to extract any ServiceProfile/PublicIdentity/Identity nodes from %s", user_data.c_str());
  }
  return public_ids;
}

// Parses the given User-Data XML to retrieve the single PrivateID element.
std::string get_private_id(const std::string& user_data)
{
  std::string impi;

  // Parse the XML document, saving off the passed-in string first (as parsing
  // is destructive).
  rapidxml::xml_document<> doc;

  // This doesn't need freeing - prev_doc is on the stack, and this
  // uses its memory pool.
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

  rapidxml::xml_node<>* is = doc.first_node("IMSSubscription");
  if (is)
  {
    rapidxml::xml_node<>* id = is->first_node("PrivateID");
    if (id)
    {
      impi = id->value();
    }
    else
    {
      LOG_ERROR("Missing Private ID in IMS Subscription document: \n\n%s", user_data.c_str());
    }
  }

  if (impi.compare("null") == 0)
  {
    impi = ""; // LCOV_EXCL_LINE
  }

  return impi;
}

}
