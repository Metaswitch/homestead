/**
 * @file subscriber_data.h structs to represent subscriber data.
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef IMPLICIT_REG_SET_H__
#define IMPLICIT_REG_SET_H__

#include <string>
#include <vector>
#include "charging_addresses.h"
#include "reg_state.h"


class ImplicitRegistrationSet
{
public:
  ImplicitRegistrationSet(const std::string& default_impu) :
    default_impu(default_impu),
    _ttl(0)
  {

  }

  //TODO implementation, constructor, destructor, getters etc.
public:

  void add_impi(std::string impi);
  void remove_impi(std::string impi);

  std::string get_ims_sub_xml()
  {
    return _ims_sub_xml;
  }

  RegistrationState get_reg_state()
  {
    return _reg_state;
  }

  std::vector<std::string> get_associated_impis()
  {
    return _associated_impis;
  }

  const ChargingAddresses& get_charging_addresses()
  {
    return _charging_addresses;
  }

  int32_t get_ttl()
  {
    return _ttl;
  }

  void set_ims_sub_xml(std::string xml)
  {
    _ims_sub_xml = xml;
  }

  void set_reg_state(RegistrationState state)
  {
    _reg_state = state;
  }

  void set_associated_impis (std::vector<std::string> impis)
  {
    _associated_impis = impis;
  }

  void set_charging_addresses(ChargingAddresses addresses)
  {
    _charging_addresses = addresses;
  }

  void set_ttl(int32_t ttl)
  {
    _ttl = ttl;
  }

  const std::string default_impu;

private:
  std::string _ims_sub_xml;
  RegistrationState _reg_state;
  std::vector<std::string> _associated_impis;
  ChargingAddresses _charging_addresses;
  int32_t _ttl;
};

#endif
