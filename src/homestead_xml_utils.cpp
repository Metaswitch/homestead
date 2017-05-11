/**
 * @file homestead_xml_utils.cpp class containing XML utilities
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

#include <algorithm>

#include "homestead_xml_utils.h"
#include "xml_utils.h"
#include "httpclient.h"
#include "log.h"

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"

namespace XmlUtils
{

// Builds a ClearwaterRegData XML document for passing to Sprout,
// based on the given registration state and User-Data XML from the HSS.
int build_ClearwaterRegData_xml(RegistrationState state,
                                std::string xml,
                                const ChargingAddresses& charging_addrs,
                                std::string& xml_str)
{
  rapidxml::xml_document<> doc;
  rapidxml::xml_node<>* root = doc.allocate_node(rapidxml::node_type::node_element,
                                                 RegDataXMLUtils::CLEARWATER_REG_DATA);

  // Add the registration state - note we need to pass in the regtype string
  // here to ensure it isn't destroyed before the XML is printed.
  std::string regtype;
  add_reg_state_node(state, doc, root, regtype);

  if (xml != "")
  {
    // Parse the XML document - note we need to pass in the prev_doc here to
    // ensure it isn't destroyed before the XML is printed.
    rapidxml::xml_document<> prev_doc;
    int rc = add_ims_subscription_node(xml, doc, root, prev_doc);

    if (rc == HTTP_SERVER_ERROR)
    {
      return HTTP_SERVER_ERROR;
    }
  }

  if (!charging_addrs.empty())
  {
    add_charging_addr_node(charging_addrs, doc, root);
  }

  doc.append_node(root);
  rapidxml::print(std::back_inserter(xml_str), doc, 0);

  return HTTP_OK;
}

// Builds the ResistrationState node, and adds it to the passed in XML doc.
void add_reg_state_node(RegistrationState state,
                        rapidxml::xml_document<> &doc,
                        rapidxml::xml_node<>* root,
                        std::string& regtype)
{
  if (state == RegistrationState::REGISTERED)
  {
    regtype = RegDataXMLUtils::STATE_REGISTERED;
  }
  else if (state == RegistrationState::UNREGISTERED)
  {
    regtype = RegDataXMLUtils::STATE_UNREGISTERED;
  }
  else
  {
    if (state != RegistrationState::NOT_REGISTERED)
    {
      TRC_DEBUG("Invalid registration state %d", state);
    }

    regtype = RegDataXMLUtils::STATE_NOT_REGISTERED;
  }

  rapidxml::xml_node<>* reg = doc.allocate_node(rapidxml::node_type::node_element,
                                                RegDataXMLUtils::REGISTRATION_STATE,
                                                regtype.c_str());
  root->append_node(reg);
}

// Builds the IMSSubscription node, and adds it to the passed in XML doc.
int add_ims_subscription_node(std::string xml,
                              rapidxml::xml_document<> &doc,
                              rapidxml::xml_node<>* root,
                              rapidxml::xml_document<> &prev_doc)
{
  rapidxml::xml_node<>* is = NULL;

  // Parse the XML document, saving off the passed-in string first (as parsing
  // is destructive). This doesn't need freeing - prev_doc is on the stack,
  // and this uses its memory pool.
  char* user_data_str = prev_doc.allocate_string(xml.c_str());

  try
  {
    prev_doc.parse<rapidxml::parse_strip_xml_namespaces>(user_data_str);

    if (prev_doc.first_node(RegDataXMLUtils::IMS_SUBSCRIPTION))
    {
      is = doc.clone_node(prev_doc.first_node(RegDataXMLUtils::IMS_SUBSCRIPTION));
    }
    else
    {
      TRC_DEBUG("Missing IMS Subscription in XML");
      prev_doc.clear();
      return HTTP_SERVER_ERROR;
    }
  }
  catch (rapidxml::parse_error err)
  {
    TRC_DEBUG("Parse error in IMS Subscription document: %s\n\n%s", err.what(), xml.c_str());
    prev_doc.clear();
    return HTTP_SERVER_ERROR;
  }

  if (is != NULL)
  {
    root->append_node(is);
  }

  return HTTP_OK;
}

// Builds the ChargingAddresses node, and adds it to the xml doc passed in.
void add_charging_addr_node(const ChargingAddresses& charging_addrs,
                            rapidxml::xml_document<> &doc,
                            rapidxml::xml_node<>* root)
{
  rapidxml::xml_node<>* cfs = doc.allocate_node(rapidxml::node_type::node_element,
                                                RegDataXMLUtils::CHARGING_ADDRESSES);

  if (!charging_addrs.ccfs.empty())
  {
    rapidxml::xml_node<>* pccfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                    RegDataXMLUtils::CCF,
                                                    charging_addrs.ccfs[0].c_str());
    rapidxml::xml_attribute<>* pccf = doc.allocate_attribute(RegDataXMLUtils::CCF_ECF_PRIORITY,
                                                             RegDataXMLUtils::CCF_PRIORITY_1);
    pccfn->append_attribute(pccf);
    cfs->append_node(pccfn);
  }

  if (charging_addrs.ccfs.size() > 1)
  {
    rapidxml::xml_node<>* sccfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                    RegDataXMLUtils::CCF,
                                                    charging_addrs.ccfs[1].c_str());
    rapidxml::xml_attribute<>* sccf = doc.allocate_attribute(RegDataXMLUtils::CCF_ECF_PRIORITY,
                                                             RegDataXMLUtils::CCF_PRIORITY_2);
    sccfn->append_attribute(sccf);
    cfs->append_node(sccfn);
  }

  if (!charging_addrs.ecfs.empty())
  {
    rapidxml::xml_node<>* pecfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                    RegDataXMLUtils::ECF,
                                                    charging_addrs.ecfs[0].c_str());
    rapidxml::xml_attribute<>* pecf = doc.allocate_attribute(RegDataXMLUtils::CCF_ECF_PRIORITY,
                                                             RegDataXMLUtils::ECF_PRIORITY_1);
    pecfn->append_attribute(pecf);
    cfs->append_node(pecfn);
  }

  if (charging_addrs.ecfs.size() > 1)
  {
    rapidxml::xml_node<>* secfn = doc.allocate_node(rapidxml::node_type::node_element,
                                                    RegDataXMLUtils::ECF,
                                                    charging_addrs.ecfs[1].c_str());
    rapidxml::xml_attribute<>* secf = doc.allocate_attribute(RegDataXMLUtils::CCF_ECF_PRIORITY,
                                                             RegDataXMLUtils::ECF_PRIORITY_2);
    secfn->append_attribute(secf);
    cfs->append_node(secfn);
  }

  root->append_node(cfs);
}

// Parses the given User-Data XML to retrieve a list of all the public IDs.
std::vector<std::string> get_public_ids(const std::string& user_data)
{
  std::string unused_default_id;
  return get_public_and_default_ids(user_data, unused_default_id);
}

// Parses the given User-Data XML to retrieve the default public ID (the first
// unbarred public ID).
void get_default_id(const std::string& user_data,
                    std::string &default_id)
{
  std::vector<std::string> unused_public_ids;
  unused_public_ids = get_public_and_default_ids(user_data, default_id);
}

// Parses the given User-Data XML to retrieve a list of all the public IDs, and
// the default public ID (the first unbarred public ID).
std::vector<std::string> get_public_and_default_ids(const std::string &user_data,
                                                    std::string &default_id)
{
  std::vector<std::string> public_ids;
  std::vector<std::string> unbarred_public_ids;

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
    TRC_DEBUG("Parse error in IMS Subscription document: %s\n\n%s", err.what(), user_data.c_str());
    doc.clear();
  }

  // Walk through all nodes in the hierarchy IMSSubscription->ServiceProfile->PublicIdentity
  // ->Identity.
  rapidxml::xml_node<>* is = doc.first_node(RegDataXMLUtils::IMS_SUBSCRIPTION);
  if (is)
  {
    for (rapidxml::xml_node<>* sp = is->first_node(RegDataXMLUtils::SERVICE_PROFILE);
         sp;
         sp = sp->next_sibling(RegDataXMLUtils::SERVICE_PROFILE))
    {
      for (rapidxml::xml_node<>* pi = sp->first_node(RegDataXMLUtils::PUBLIC_IDENTITY);
           pi;
           pi = pi->next_sibling(RegDataXMLUtils::PUBLIC_IDENTITY))
      {
        rapidxml::xml_node<>* id = pi->first_node(RegDataXMLUtils::IDENTITY);
        std::string barring_value = RegDataXMLUtils::STATE_UNBARRED;
        rapidxml::xml_node<>* barring_indication = pi->first_node(RegDataXMLUtils::BARRING_INDICATION);
        if (barring_indication)
        {
          barring_value = barring_indication->value();
        }

        if (id)
        {
          std::string uri = std::string(id->value());

          rapidxml::xml_node<>* extension = pi->first_node(RegDataXMLUtils::EXTENSION);
          if (extension)
          {
            RegDataXMLUtils::parse_extension_identity(uri, extension);
          }

          if (std::find(public_ids.begin(), public_ids.end(), uri) ==
              public_ids.end())
          {
            public_ids.push_back(uri);
            if (barring_value == RegDataXMLUtils::STATE_UNBARRED)
            {
              unbarred_public_ids.push_back(uri);
            }
          }
        }
        else
        {
          TRC_WARNING("PublicIdentity node was missing Identity child: %s", user_data.c_str());
        }
      }
    }
  }

  if (public_ids.size() == 0)
  {
    TRC_ERROR("Failed to extract any ServiceProfile/PublicIdentity/Identity nodes from %s", user_data.c_str());
  }

  // Set the default id - this is the first unbarred public identity.
  if (unbarred_public_ids.size() != 0)
  {
    default_id = unbarred_public_ids.front();
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
    TRC_ERROR("Parse error in IMS Subscription document: %s\n\n%s", err.what(), user_data.c_str());
    doc.clear();
  }

  rapidxml::xml_node<>* is = doc.first_node(RegDataXMLUtils::IMS_SUBSCRIPTION);
  if (is)
  {
    rapidxml::xml_node<>* id = is->first_node(RegDataXMLUtils::PRIVATE_ID);
    if (id)
    {
      impi = id->value();
    }
    else
    {
      TRC_ERROR("Missing Private ID in IMS Subscription document: \n\n%s", user_data.c_str());
    }
  }

  if (impi.compare("null") == 0)
  {
    impi = ""; // LCOV_EXCL_LINE
  }

  return impi;
}

}
