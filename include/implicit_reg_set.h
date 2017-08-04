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
  //TODO implementation, constructor, destructor, getters etc.
public:

  void add_impi(std::string impi);
  void remove_impi(std::string impi);

  // TODO - get the default imput from the service profile
  std::string get_default_impu()
  {
    //TODO
    return "TODO";
  }

  std::string get_service_profile()
  {
    return _service_profile;
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

  void set_service_profile(std::string profile)
  {
    _service_profile = profile;
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

private:
    std::string _service_profile;
    RegistrationState _reg_state;
    std::vector<std::string> _associated_impis;
    ChargingAddresses _charging_addresses;
    int32_t _ttl;
};

#endif