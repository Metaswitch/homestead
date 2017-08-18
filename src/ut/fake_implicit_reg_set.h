/**
 * @file fake_implicit_reg_set.h Fake Implicit Registration Set for tests
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef FAKE_IMPLICIT_REG_SET_H__
#define FAKE_IMPLICIT_REG_SET_H__

#include "implicit_reg_set.h"

class FakeImplicitRegistrationSet : public ImplicitRegistrationSet
{
  FakeImplicitRegistrationSet(const std::string& default_impu) : ImplicitRegistrationSet(default_impu), _ttl(0)
  {
  }

  virtual const std::string& get_ims_sub_xml() const override
  {
    return _ims_sub_xml;
  }

  virtual RegistrationState get_reg_state() const override
  {
    return _reg_state;
  }

  virtual std::vector<std::string> get_associated_impis() const override
  {
    return _associated_impis;
  }

  virtual const ChargingAddresses& get_charging_addresses() const override
  {
    return _charging_addresses;
  }

  virtual int32_t get_ttl() const override
  {
    return _ttl;
  }

  virtual void set_ims_sub_xml(const std::string& xml) override
  {
    _ims_sub_xml = xml;
  }

  virtual void set_reg_state(RegistrationState state) override
  {
    _reg_state = state;
  }

  virtual void add_associated_impi (const std::string& impi) override
  {
    _associated_impis.push_back(impi);
  }

  virtual void delete_associated_impi (const std::string& impi) override
  {
    _associated_impis.erase(std::remove(_associated_impis.begin(),
                                        _associated_impis.end(),
                                        impi),
                            _associated_impis.end());
  }

  virtual void set_charging_addresses(const ChargingAddresses& addresses) override
  {
    _charging_addresses = addresses;
  }

  virtual void set_ttl(int32_t ttl) override
  {
    _ttl = ttl;
  }

private:
  std::string _ims_sub_xml;
  RegistrationState _reg_state;
  std::vector<std::string> _associated_impis;
  ChargingAddresses _charging_addresses;
  int32_t _ttl;
};

#endif
