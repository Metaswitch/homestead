/**
 * @file homestead_xml_utils.h class for XML utilities
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef XMLUTILS_H__
#define XMLUTILS_H__

#include <string>
#include <vector>
#include "reg_state.h"
#include "charging_addresses.h"
#include "rapidxml/rapidxml.hpp"
#include "implicit_reg_set.h"

namespace XmlUtils
{
  std::vector<std::string> get_public_ids(const std::string& user_data);
  void get_default_id(const std::string& user_data,
                      std::string& default_id);
  std::vector<std::string> get_public_and_default_ids(const std::string& user_data,
                                                      std::string& default_id);
  std::string get_private_id(const std::string& user_data);
  int build_ClearwaterRegData_xml(ImplicitRegistrationSet* irs, 
                                  std::string& xml_str);
  void add_reg_state_node(RegistrationState state,
                          rapidxml::xml_document<> &doc,
                          rapidxml::xml_node<>* root,
                          std::string& regtype);
  int add_ims_subscription_node(std::string xml,
                                rapidxml::xml_document<> &doc,
                                rapidxml::xml_node<>* root,
                                rapidxml::xml_document<> &prev_doc);
  void add_charging_addr_node(const ChargingAddresses& charging_addrs,
                              rapidxml::xml_document<> &doc,
                              rapidxml::xml_node<>* root);
}

#endif
