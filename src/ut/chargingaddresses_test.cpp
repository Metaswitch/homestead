/**
 * @file chargingaddresses_test.cpp UT for ChargingAddresses class.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
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
