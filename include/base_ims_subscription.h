/**
 * Base Implementation of IMS Subscription
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef BASE_IMS_SUBSCRIPTION_H__
#define BASE_IMS_SUBSCRIPTION_H__

#include <map>
#include <string>

#include "ims_subscription.h"

class BaseImsSubscription : public ImsSubscription
{
public:
  BaseImsSubscription(std::vector<ImplicitRegistrationSet*>& irss) :
    ImsSubscription()
  {
    for (ImplicitRegistrationSet*& irs : irss)
    {
      _irss[irs->get_default_impu()] = irs;
    }
  }

  virtual ~BaseImsSubscription()
  {
    for (std::pair<const std::string, ImplicitRegistrationSet*>& irs : _irss)
    {
      delete irs.second;
    }
  }

  typedef std::map<std::string, ImplicitRegistrationSet*> Irs;

  virtual void set_charging_addrs(const ChargingAddresses& new_addresses) override
  {
    for (Irs::value_type& pair : _irss)
    {
      pair.second->set_charging_addresses(new_addresses);
    }
  }

  virtual ImplicitRegistrationSet* get_irs_for_default_impu(const std::string& impu) override
  {
    Irs::const_iterator it =  _irss.find(impu);

    if (it == _irss.end())
    {
      return nullptr;
    }
    else
    {
      return it->second;
    }
  }

  Irs& get_irs()
  {
    return _irss;
  }

private:
  std::map<std::string, ImplicitRegistrationSet*> _irss;
};

#endif
