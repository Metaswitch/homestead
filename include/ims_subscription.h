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

#ifndef IMS_SUBSCRIPTION_H__
#define IMS_SUBSCRIPTION_H__

#include <set>

#include "charging_addresses.h"
#include "implicit_reg_set.h"

class ImsSubscription
{
public:
  // Updates the charging addresses for all the IRSs 
  void update_charging_addresses(ChargingAddresses new_addresses);

private:
  std::set<ImplicitRegistrationSet> _irs_map;
};

#endif