/**
 * @file fakestatisticsmanager.hpp Fake statistics manager for UT.
 *
 * project clearwater - ims in the cloud
 * copyright (c) 2013  metaswitch networks ltd
 *
 * this program is free software: you can redistribute it and/or modify it
 * under the terms of the gnu general public license as published by the
 * free software foundation, either version 3 of the license, or (at your
 * option) any later version, along with the "special exception" for use of
 * the program along with ssl, set forth below. this program is distributed
 * in the hope that it will be useful, but without any warranty;
 * without even the implied warranty of merchantability or fitness for
 * a particular purpose.  see the gnu general public license for more
 * details. you should have received a copy of the gnu general public
 * license along with this program.  if not, see
 * <http://www.gnu.org/licenses/>.
 *
 * the author can be reached by email at clearwater@metaswitch.com or by
 * post at metaswitch networks ltd, 100 church st, enfield en2 6bq, uk
 *
 * special exception
 * metaswitch networks ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining openssl with the
 * software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the gpl. you must comply with the gpl in all
 * respects for all of the code used other than openssl.
 * "openssl" means openssl toolkit software distributed by the openssl
 * project and licensed under the openssl licenses, or a work based on such
 * software and licensed under the openssl licenses.
 * "openssl licenses" means the openssl license and original ssleay license
 * under which the openssl project distributes the openssl toolkit software,
 * as those licenses appear in the file license-openssl.
 */

#ifndef FAKESTATISTICSMANAGER_HPP__
#define FAKESTATISTICSMANAGER_HPP__

#include "gmock/gmock.h"
#include "statisticsmanager.h"
#include "fakestatisticsmanager.hpp"

namespace SNMP
{
class FakeAccumulatorTable: public AccumulatorTable
{
  public:
    int _count;
    FakeAccumulatorTable() { _count = 0; };
    void accumulate(uint32_t sample) { _count += sample; };
    void reset_count() { _count = 0; };
};

class FakeCounterTable: public CounterTable
{
  public:
    int _count;
    FakeCounterTable() { _count = 0; };
    void increment() { _count++; };
    void reset_count() { _count = 0; };
};

extern FakeAccumulatorTable FAKE_H_LATENCY_US_TABLE;
extern FakeAccumulatorTable FAKE_H_HSS_LATENCY_US_TABLE;
extern FakeAccumulatorTable FAKE_H_CACHE_LATENCY_US_TABLE;
extern FakeAccumulatorTable FAKE_H_HSS_DIGEST_LATENCY_US_TABLE;
extern FakeAccumulatorTable FAKE_H_HSS_SUBSCRIPTION_LATENCY_US_TABLE;
extern FakeCounterTable FAKE_H_INCOMING_REQUESTS_TABLE;
extern FakeCounterTable FAKE_H_REJECTED_OVERLOAD_TABLE;

} // Namespace SNMP ends.

class FakeStatisticsManager: public StatisticsManager
{
  FakeStatisticsManager();
};

extern FakeStatisticsManager FAKE_STATISTICS_MANAGER;

#endif
