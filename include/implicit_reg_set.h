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
  ImplicitRegistrationSet()
  {
  }

  virtual ~ImplicitRegistrationSet()
  {
  }

public:

  virtual const std::string& get_default_impu() const = 0;

  virtual const std::string& get_ims_sub_xml() const = 0;
  virtual RegistrationState get_reg_state() const = 0;
  virtual std::vector<std::string> get_associated_impis() const = 0;
  virtual const ChargingAddresses& get_charging_addresses() const = 0;
  virtual int32_t get_ttl() const = 0;

  virtual void set_ims_sub_xml(const std::string& xml) = 0;
  virtual void set_reg_state(RegistrationState state) = 0;
  virtual void add_associated_impi(const std::string& impi) = 0;
  virtual void delete_associated_impi(const std::string& impi) = 0;
  virtual void set_charging_addresses(const ChargingAddresses& addresses) = 0;
  virtual void set_ttl(int32_t ttl) = 0;
};

#endif
