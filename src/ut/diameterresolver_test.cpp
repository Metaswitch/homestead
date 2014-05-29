/**
 * @file diameterresolver_test.cpp UT for DiameterResolver class.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
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

#include <string>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "utils.h"
#include "dnscachedresolver.h"
#include "diameterresolver.h"
#include "fakelogger.hpp"
#include "test_utils.hpp"

using namespace std;

/// Fixture for DiamterResolverTest.
class DiameterResolverTest : public ::testing::Test
{
  FakeLogger _log;
  DnsCachedResolver _dnsresolver;
  DiameterResolver _diameterresolver;

  // DNS Resolver is created with server address 0.0.0.0 to disable server
  // queries.
  DiameterResolverTest() :
    _dnsresolver("0.0.0.0"),
    _diameterresolver(&_dnsresolver, AF_INET)
  {
    Log::setLoggingLevel(99);
  }

  virtual ~DiameterResolverTest()
  {
  }

  DnsRRecord* a(const std::string& name,
                int ttl,
                const std::string& address)
  {
    struct in_addr addr;
    inet_pton(AF_INET, address.c_str(), &addr);
    return (DnsRRecord*)new DnsARecord(name, ttl, addr);
  }

  DnsRRecord* aaaa(const std::string& name,
                   int ttl,
                   const std::string& address)
  {
    struct in6_addr addr;
    inet_pton(AF_INET6, address.c_str(), &addr);
    return (DnsRRecord*)new DnsAAAARecord(name, ttl, addr);
  }

  DnsRRecord* srv(const std::string& name,
                  int ttl,
                  int priority,
                  int weight,
                  int port,
                  const std::string& target)
  {
    return (DnsRRecord*)new DnsSrvRecord(name, ttl, priority, weight, port, target);
  }

  DnsRRecord* naptr(const std::string& name,
                    int ttl,
                    int order,
                    int preference,
                    const std::string& flags,
                    const std::string& service,
                    const std::string& regex,
                    const std::string& replacement)
  {
    return (DnsRRecord*)new DnsNaptrRecord(name, ttl, order, preference, flags,
                                           service, regex, replacement);
  }
};

/// A single resolver operation.
class RT
{
public:
  RT(DiameterResolver& resolver, const std::string& realm) :
    _resolver(resolver),
    _realm(realm),
    _host(""),
    _max_targets(2)
  {
  }

  RT& set_host(std::string host)
  {
    _host = host;
    return *this;
  }

  RT& set_max_targets(int max_targets)
  {
    _max_targets = max_targets;
    return *this;
  }

  std::string resolve()
  {
    SCOPED_TRACE(_realm);
    std::vector<AddrInfo> targets;
    int ttl;
    std::string output;

    _resolver.resolve(_realm, _host, _max_targets, targets, ttl);
    if (!targets.empty())
    {
      // Successful, so render AddrInfo as a string.
      output = addrinfo_to_string(targets[0]);
    }
    return output;
  }

private:
  std::string addrinfo_to_string(const AddrInfo& ai) const
  {
    ostringstream oss;
    char buf[100];
    if (ai.address.af == AF_INET6)
    {
      oss << "[";
    }
    oss << inet_ntop(ai.address.af, &ai.address.addr, buf, sizeof(buf));
    if (ai.address.af == AF_INET6)
    {
      oss << "]";
    }
    oss << ":" << ai.port;
    oss << ";transport=";
    if (ai.transport == IPPROTO_UDP)
    {
      oss << "UDP";
    }
    else if (ai.transport == IPPROTO_TCP)
    {
      oss << "TCP";
    }
    else
    {
      oss << "Unknown (" << ai.transport << ")";
    }
    return oss.str();
  }

  /// Reference to the DiameterResolver.
  DiameterResolver& _resolver;

  /// Input parameters to request.
  std::string _realm;
  std::string _host;
  int _max_targets;
};

TEST_F(DiameterResolverTest, IPv4AddressResolution)
{
  // Test defaulting of port and transport when target is IP address
  EXPECT_EQ("3.0.0.1:5060;transport=UDP",
            RT(_diameterresolver, "3.0.0.1").resolve());
}

TEST_F(DiameterResolverTest, SimpleNAPTRSRVTCPResolution)
{
  // Test selection of TCP transport and port using NAPTR and SRV records.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout.cw-ngv.com", 3600, 0, 0, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=TCP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SimpleNAPTRSRVUDPResolution)
{
  // Test selection of UDP transport and port using NAPTR and SRV records.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout.cw-ngv.com", 3600, 0, 0, "S", "Diameter+D2U", "", "_diameter._sctp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._sctp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._sctp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=UDP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SimpleSRVTCPResolution)
{
  // Test selection of TCP transport and port using SRV records only
  std::vector<DnsRRecord*> records;
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=TCP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SimpleSRVUDPResolution)
{
  // Test selection of UDP transport and port using SRV records only
  std::vector<DnsRRecord*> records;
  records.push_back(srv("_diameter._sctp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._sctp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=UDP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SimpleSRVUDPPreference)
{
  // Test preference for UDP transport over TCP transport if both configure in SRV.
  std::vector<DnsRRecord*> records;
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(srv("_diameter._sctp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._sctp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=UDP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SimpleAResolution)
{
  // Test resolution using A records only.
  std::vector<DnsRRecord*> records;
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Test default port/transport.
  EXPECT_EQ("3.0.0.1:5060;transport=UDP",
            RT(_diameterresolver, "sprout.cw-ngv.com").resolve());
}

// This unit test doesn't assert anything - it tests for a bug where
// DNS expiry triggered invalid memory accesses, which will show up in
// the Valgrind output
// TEST_F(DiameterResolverTest, Expiry)
// {
//   cwtest_completely_control_time();
//   std::vector<DnsRRecord*> udp_records;
//   std::vector<DnsRRecord*> tcp_records;
//   udp_records.push_back(a("sprout.cw-ngv.com", 5, "3.0.0.1"));
//   tcp_records.push_back(a("sprout.cw-ngv.com", 2, "3.0.0.1"));
//   _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, udp_records);
//   _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, tcp_records);
//   ASSERT_NE("", _dnsresolver.display_cache());
// 
//   cwtest_advance_time_ms(1000);
//   _dnsresolver.expire_cache();
//   ASSERT_NE("", _dnsresolver.display_cache());
// 
//   cwtest_advance_time_ms(2000);
//   _dnsresolver.expire_cache();
//   ASSERT_EQ("", _dnsresolver.display_cache());
// 
//   cwtest_reset_time();
// }
// 
// 
// // This unit test doesn't assert anything - it tests for a bug where
// // DNS expiry triggered invalid memory accesses, which will show up in
// // the Valgrind output
// TEST_F(DiameterResolverTest, ExpiryNoInvalidRead)
// {
//   cwtest_completely_control_time();
//   // Test resolution using A records only.
//   std::vector<DnsRRecord*> udp_records;
//   std::vector<DnsRRecord*> tcp_records;
//   udp_records.push_back(a("sprout.cw-ngv.com", 2, "3.0.0.1"));
//   tcp_records.push_back(a("sprout.cw-ngv.com", 2, "3.0.0.1"));
//   _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, udp_records);
//   _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, tcp_records);
// 
//   LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());
//   cwtest_advance_time_ms(3000);
//   _dnsresolver.expire_cache();
//   LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());
//   cwtest_reset_time();
// }
// 
// TEST_F(DiameterResolverTest, SimpleAAAAResolution)
// {
//   // Test resolution using AAAA records only.
//   std::vector<DnsRRecord*> records;
//   records.push_back(aaaa("sprout.cw-ngv.com", 3600, "3::1"));
//   _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_aaaa, records);
// 
//   LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());
// 
//   // Test default port/transport.
//   EXPECT_EQ("[3::1]:5060;transport=UDP",
//             RT(_diameterresolver, "sprout.cw-ngv.com").set_af(AF_INET6).resolve());
// }

TEST_F(DiameterResolverTest, NAPTROrderPreference)
{
  // Test NAPTR selection according to order - select TCP as first in order.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout-1.cw-ngv.com", 3600, 1, 0, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  records.push_back(naptr("sprout-1.cw-ngv.com", 3600, 2, 0, "S", "AAA+D2S", "", "_diameter._sctp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(srv("_diameter._sctp.sprout.cw-ngv.com", 3600, 0, 0, 5054, "sprout-1.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._sctp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=TCP",
            RT(_diameterresolver, "sprout-1.cw-ngv.com").resolve());

  // Test NAPTR selection according to preference - select UDP as first in preference.
  records.push_back(naptr("sprout-2.cw-ngv.com", 3600, 0, 2, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  records.push_back(naptr("sprout-2.cw-ngv.com", 3600, 0, 1, "S", "AAA+D2S", "", "_diameter._sctp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout-2.cw-ngv.com", ns_t_naptr, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  EXPECT_EQ("3.0.0.1:5054;transport=UDP",
            RT(_diameterresolver, "sprout-2.cw-ngv.com").resolve());
}

TEST_F(DiameterResolverTest, SRVPriority)
{
  // Test SRV selection according to priority.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout.cw-ngv.com", 3600, 0, 0, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 1, 0, 5054, "sprout-1.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 2, 0, 5054, "sprout-2.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-2.cw-ngv.com", 3600, "3.0.0.2"));
  _dnsresolver.add_to_cache("sprout-2.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Do 100 resolutions and check that sprout-1 is picked every time.
  std::map<std::string, int> counts;

  for (int ii = 0; ii < 100; ++ii)
  {
    counts[RT(_diameterresolver, "sprout.cw-ngv.com").resolve()]++;
  }

  EXPECT_EQ(100, counts["3.0.0.1:5054;transport=TCP"]);
  EXPECT_EQ(0, counts["3.0.0.2:5054;transport=TCP"]);
}

TEST_F(DiameterResolverTest, SRVWeight)
{
  // Test SRV selection according to weight.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout.cw-ngv.com", 3600, 0, 0, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 100, 5054, "sprout-1.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 300, 5054, "sprout-2.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 200, 5054, "sprout-3.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 400, 5054, "sprout-4.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-2.cw-ngv.com", 3600, "3.0.0.2"));
  _dnsresolver.add_to_cache("sprout-2.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-3.cw-ngv.com", 3600, "3.0.0.3"));
  _dnsresolver.add_to_cache("sprout-3.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-4.cw-ngv.com", 3600, "3.0.0.4"));
  _dnsresolver.add_to_cache("sprout-4.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Do 1000 resolutions and check that the proportions are roughly as
  // expected.  The error bound is chosen to be 5 standard deviations.
  std::map<std::string, int> counts;

  for (int ii = 0; ii < 1000; ++ii)
  {
    counts[RT(_diameterresolver, "sprout.cw-ngv.com").resolve()]++;
  }

  EXPECT_LT(100-5*9, counts["3.0.0.1:5054;transport=TCP"]);
  EXPECT_GT(100+5*9, counts["3.0.0.1:5054;transport=TCP"]);
  EXPECT_LT(300-5*14, counts["3.0.0.2:5054;transport=TCP"]);
  EXPECT_GT(300+5*14, counts["3.0.0.2:5054;transport=TCP"]);
  EXPECT_LT(200-5*13, counts["3.0.0.3:5054;transport=TCP"]);
  EXPECT_GT(200+5*13, counts["3.0.0.3:5054;transport=TCP"]);
  EXPECT_LT(400-5*15, counts["3.0.0.4:5054;transport=TCP"]);
  EXPECT_GT(400+5*15, counts["3.0.0.4:5054;transport=TCP"]);
}

TEST_F(DiameterResolverTest, ARecordLoadBalancing)
{
  // Test load balancing across multiple A records.
  std::vector<DnsRRecord*> records;
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.1"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.2"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.3"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.4"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Do 10000 resolutions and check that the proportions are roughly even
  // The error bound is chosen to be 5 standard deviations.
  std::map<std::string, int> counts;

  for (int ii = 0; ii < 1000; ++ii)
  {
    counts[RT(_diameterresolver, "sprout.cw-ngv.com").resolve()]++;
  }

  EXPECT_LT(250-5*14, counts["3.0.0.1:5060;transport=UDP"]);
  EXPECT_GT(250+5*14, counts["3.0.0.1:5060;transport=UDP"]);
  EXPECT_LT(250-5*14, counts["3.0.0.2:5060;transport=UDP"]);
  EXPECT_GT(250+5*14, counts["3.0.0.2:5060;transport=UDP"]);
  EXPECT_LT(250-5*14, counts["3.0.0.3:5060;transport=UDP"]);
  EXPECT_GT(250+5*14, counts["3.0.0.3:5060;transport=UDP"]);
  EXPECT_LT(250-5*14, counts["3.0.0.4:5060;transport=UDP"]);
  EXPECT_GT(250+5*14, counts["3.0.0.4:5060;transport=UDP"]);
}

TEST_F(DiameterResolverTest, BlacklistSRVRecords)
{
  // Test blacklist of SRV selections.
  std::vector<DnsRRecord*> records;
  records.push_back(naptr("sprout.cw-ngv.com", 3600, 0, 0, "S", "AAA+D2T", "", "_diameter._tcp.sprout.cw-ngv.com"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_naptr, records);

  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 100, 5054, "sprout-1.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 300, 5054, "sprout-2.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 200, 5054, "sprout-3.cw-ngv.com"));
  records.push_back(srv("_diameter._tcp.sprout.cw-ngv.com", 3600, 0, 400, 5054, "sprout-4.cw-ngv.com"));
  _dnsresolver.add_to_cache("_diameter._tcp.sprout.cw-ngv.com", ns_t_srv, records);

  records.push_back(a("sprout-1.cw-ngv.com", 3600, "3.0.0.1"));
  _dnsresolver.add_to_cache("sprout-1.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-2.cw-ngv.com", 3600, "3.0.0.2"));
  _dnsresolver.add_to_cache("sprout-2.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-3.cw-ngv.com", 3600, "3.0.0.3"));
  _dnsresolver.add_to_cache("sprout-3.cw-ngv.com", ns_t_a, records);
  records.push_back(a("sprout-4.cw-ngv.com", 3600, "3.0.0.4"));
  _dnsresolver.add_to_cache("sprout-4.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Blacklist 3.0.0.4.
  AddrInfo ai;
  ai.address.af = AF_INET;
  inet_pton(AF_INET, "3.0.0.4", &ai.address.addr.ipv4);
  ai.port = 5054;
  ai.transport = IPPROTO_TCP;
  _diameterresolver.blacklist(ai, 300);

  // Do 1000 resolutions and check that 3.0.0.4 is never selected and the
  // proportions of the other addresses are as expected.  The error bounds are
  // chosen to be 5 standard deviations.
  std::map<std::string, int> counts;

  for (int ii = 0; ii < 1000; ++ii)
  {
    counts[RT(_diameterresolver, "sprout.cw-ngv.com").resolve()]++;
  }

  EXPECT_EQ(0, counts["3.0.0.4:5054;transport=TCP"]);
  EXPECT_LT(167-5*12, counts["3.0.0.1:5054;transport=TCP"]);
  EXPECT_GT(167+5*12, counts["3.0.0.1:5054;transport=TCP"]);
  EXPECT_LT(500-5*16, counts["3.0.0.2:5054;transport=TCP"]);
  EXPECT_GT(500+5*16, counts["3.0.0.2:5054;transport=TCP"]);
  EXPECT_LT(333-5*15, counts["3.0.0.3:5054;transport=TCP"]);
  EXPECT_GT(333+5*15, counts["3.0.0.3:5054;transport=TCP"]);
}

TEST_F(DiameterResolverTest, BlacklistARecord)
{
  // Test blacklisting of an A record.
  std::vector<DnsRRecord*> records;
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.1"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.2"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.3"));
  records.push_back(a("sprout.cw-ngv.com", 3600, "3.0.0.4"));
  _dnsresolver.add_to_cache("sprout.cw-ngv.com", ns_t_a, records);

  LOG_DEBUG("Cache status\n%s", _dnsresolver.display_cache().c_str());

  // Blacklist 3.0.0.3.
  AddrInfo ai;
  ai.address.af = AF_INET;
  inet_pton(AF_INET, "3.0.0.3", &ai.address.addr.ipv4);
  ai.port = 5060;
  ai.transport = IPPROTO_UDP;
  _diameterresolver.blacklist(ai, 300);

  // Do 1000 resolutions and check that 3.0.0.3 is not selected, and that
  // the other addresses are selected roughly equally.
  std::map<std::string, int> counts;

  for (int ii = 0; ii < 1000; ++ii)
  {
    counts[RT(_diameterresolver, "sprout.cw-ngv.com").resolve()]++;
  }

  EXPECT_EQ(0, counts["3.0.0.3:5060;transport=UDP"]);
  EXPECT_LT(333-5*15, counts["3.0.0.1:5060;transport=UDP"]);
  EXPECT_GT(333+5*15, counts["3.0.0.1:5060;transport=UDP"]);
  EXPECT_LT(333-5*15, counts["3.0.0.2:5060;transport=UDP"]);
  EXPECT_GT(333+5*15, counts["3.0.0.2:5060;transport=UDP"]);
  EXPECT_LT(333-5*15, counts["3.0.0.4:5060;transport=UDP"]);
  EXPECT_GT(333+5*15, counts["3.0.0.4:5060;transport=UDP"]);
}

