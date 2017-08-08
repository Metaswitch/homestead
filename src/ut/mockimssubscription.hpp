/**
 * @file mockimssubscription.hpp Mock ImsSubscription class
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKIMSSUBSCRIPTION_H__
#define MOCKIMSSUBSCRIPTION_H__

#include "ims_subscription.h"

class MockImsSubscription : public ImsSubscription
{
public:
  MockImsSubscription() : ImsSubscription(){};
  virtual ~MockImsSubscription() {};

  MOCK_METHOD1(set_charging_addrs, void(ChargingAddresses new_addresses));
  MOCK_METHOD1(get_irs_for_default_impu, ImplicitRegistrationSet*(std::string impu));
};

#endif