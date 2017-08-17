/**
 * @file ims_subscription.h Abstract class that represents an entire IMS Subscription.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef IMS_SUBSCRIPTION_H__
#define IMS_SUBSCRIPTION_H__

#include <set>

#include "charging_addresses.h"
#include "implicit_reg_set.h"

class ImsSubscription
{
public:
  ImsSubscription() {};
  virtual ~ImsSubscription() {};

  virtual void set_charging_addrs(ChargingAddresses new_addresses) = 0;

  virtual ImplicitRegistrationSet* get_irs_for_default_impu(std::string impu) = 0;
};

#endif