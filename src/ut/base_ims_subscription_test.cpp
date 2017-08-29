/**
 * @file base_ims_subscription_test.cpp UT for Base IMS Subscription
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "base_ims_subscription.h"
#include "fake_implicit_reg_set.h"
#include "test_utils.hpp"

static const std::string IMPU = "sip:default_impu@example.com";
static const std::deque<std::string> CCFS = { "ccf" };
static const std::deque<std::string> ECFS = { "ecf" };
static const ChargingAddresses CHARGING_ADDRESSES = { CCFS, ECFS };

class BaseImsSubscriptionTest : public testing::Test
{
};

TEST_F(BaseImsSubscriptionTest, BasicIrsHandling)
{
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  std::vector<ImplicitRegistrationSet*> irss = { irs };

  BaseImsSubscription* mis = new BaseImsSubscription(irss);

  EXPECT_NE(nullptr, mis->get_irs_for_default_impu(IMPU));
  EXPECT_EQ(1, mis->get_irs().size());

  delete mis;
}

TEST_F(BaseImsSubscriptionTest, SetChargingAddresses)
{
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  std::vector<ImplicitRegistrationSet*> irss = { irs };

  BaseImsSubscription* mis = new BaseImsSubscription(irss);

  mis->set_charging_addrs(CHARGING_ADDRESSES);

  EXPECT_EQ(CHARGING_ADDRESSES, irs->get_charging_addresses());

  delete mis;
}


