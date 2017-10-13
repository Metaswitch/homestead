/**
 * @file homestead_xml_utils.cpp class containing XML utilities
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
// Will also include a PreviousRegistrationState element if a known
// previous registration state is provided.
int build_ClearwaterRegData_xml(ImplicitRegistrationSet* irs,
                                std::string& xml_str)
{
  return build_ClearwaterRegData_xml(irs,
                                     xml_str,
                                     RegistrationState::UNKNOWN);
}

// As above, but will also include a PreviousRegistrationState element if a known
// previous registration state is provided.
int build_ClearwaterRegData_xml(ImplicitRegistrationSet* irs,
                                std::string& xml_str,
                                RegistrationState prev_reg_state)
{
  rapidxml::xml_document<> doc;
  rapidxml::xml_node<>* root = doc.allocate_node(rapidxml::node_type::node_element,
                                                 RegDataXMLUtils::CLEARWATER_REG_DATA);

  // Add the registration state - note we need to pass in a string that can be
  // used to store the registration state and whose life time is the same as
  // that of the XML doc.
  std::string state_string;
  add_reg_state_node(irs->get_reg_state(),
                     doc,
                     root,
                     RegDataXMLUtils::REGISTRATION_STATE,
                     state_string);

  // If we have been provided with a prev_reg_state then add a
  // prev_reg_state node.
  std::string previous_statestring;
  if (prev_reg_state != RegistrationState::UNKNOWN)
  {
    add_reg_state_node(prev_reg_state,
                       doc,
                       root,
                       RegDataXMLUtils::prev_reg_state,
                       previous_regtype);
  }

  std::string xml = irs->get_ims_sub_xml();
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

  ChargingAddresses charging_addrs = irs->get_charging_addresses();
  if (!charging_addrs.empty())
  {
    add_charging_addr_node(charging_addrs, doc, root);
  }

  doc.append_node(root);
  rapidxml::print(std::back_inserter(xml_str), doc, 0);

  return HTTP_OK;
}

// Builds a RegistrationState or PreviousRegistrationState node, and adds it to
// the passed in XML doc.
//
// A string must be provided (state_string) that can be used by this function o
// store the value that is written to the XML: it must have a life time as long
// as that of doc.
void add_reg_state_node(RegistrationState state,
                        rapidxml::xml_document<> &doc,
                        rapidxml::xml_node<>* root,
                        const char* node_name,
                        std::string& state_string)
{
  if (state == RegistrationState::REGISTERED)
  {
    state_string = RegDataXMLUtils::STATE_REGISTERED;
  }
  else if (state == RegistrationState::UNREGISTERED)
  {
    state_string = RegDataXMLUtils::STATE_UNREGISTERED;
  }
  else
  {
    if (state != RegistrationState::NOT_REGISTERED)
    {
      TRC_DEBUG("Invalid registration state %d", state);
    }

    state_string = RegDataXMLUtils::STATE_NOT_REGISTERED;
  }

  rapidxml::xml_node<>* reg = doc.allocate_node(rapidxml::node_type::node_element,
                                                node_name,
                                                state_string.c_str());
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
