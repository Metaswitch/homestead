/**
 * @file chargingaddresses_test.cpp UT for ChargingAddresses class.
 *
 * Copyright (C) Metaswitch Networks 2014
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"

#include "charging_addresses.h"

/// Fixture for ChargingAddressesTest.
class ChargingAddressesTest : public testing::Test
{
public:
  ChargingAddressesTest() {}

  ~ChargingAddressesTest() {}
};

TEST_F(ChargingAddressesTest, Constructor)
{
  std::deque<std::string> ccfs = {"ccf1", "ccf2"};
  std::deque<std::string> ecfs = {"ecf"};
  ChargingAddresses charging_addrs(ccfs, ecfs);
  EXPECT_EQ(ccfs, charging_addrs.ccfs);
  EXPECT_EQ(ecfs, charging_addrs.ecfs);
}

TEST_F(ChargingAddressesTest, LogString)
{
  ChargingAddresses charging_addrs;
  EXPECT_EQ("", charging_addrs.log_string());

  charging_addrs.ecfs.push_back("ecf1");
  EXPECT_EQ("Primary ECF: ecf1", charging_addrs.log_string());

  charging_addrs.ecfs.push_back("ecf2");
  EXPECT_EQ("Primary ECF: ecf1, Secondary ECF: ecf2",
            charging_addrs.log_string());

  charging_addrs.ccfs.push_back("ccf1");
  EXPECT_EQ("Primary CCF: ccf1, Primary ECF: ecf1, Secondary ECF: ecf2",
            charging_addrs.log_string());

  charging_addrs.ccfs.push_back("ccf2");
  EXPECT_EQ("Primary CCF: ccf1, Secondary CCF: ccf2, Primary ECF: ecf1, Secondary ECF: ecf2",
            charging_addrs.log_string());
}

TEST_F(ChargingAddressesTest, Empty)
{
  ChargingAddresses charging_addrs;
  EXPECT_TRUE(charging_addrs.empty());

  charging_addrs.ccfs.push_back("ccf");
  EXPECT_FALSE(charging_addrs.empty());

  charging_addrs.ecfs.push_back("ecf");
  EXPECT_FALSE(charging_addrs.empty());

  charging_addrs.ccfs.clear();
  EXPECT_FALSE(charging_addrs.empty());
}
