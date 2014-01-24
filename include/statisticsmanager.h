/**
 * @file statisticsmanager.h class used for all homestead statistics.
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

#ifndef STATISTICSMANAGER_H__
#define STATISTICSMANAGER_H__

#include "zmq_lvc.h"
#include "counter.h"
#include "accumulator.h"

#define COUNTER_INCR_METHOD(NAME) \
  virtual void incr_##NAME() { (NAME).increment(); }

#define ACCUMULATOR_UPDATE_METHOD(NAME) \
  virtual void update_##NAME(unsigned long sample) { (NAME).accumulate(sample); }

class StatisticsManager
{
public:
  StatisticsManager(long poll_timeout_ms = 1000);
  virtual ~StatisticsManager();

  ACCUMULATOR_UPDATE_METHOD(H_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_digest_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_subscription_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_cache_latency_us);

  COUNTER_INCR_METHOD(H_incoming_requests);
  COUNTER_INCR_METHOD(H_rejected_overload);

private:
  LastValueCache lvc;

  StatisticAccumulator H_latency_us;
  StatisticAccumulator H_hss_latency_us;
  StatisticAccumulator H_hss_digest_latency_us;
  StatisticAccumulator H_hss_subscription_latency_us;
  StatisticAccumulator H_cache_latency_us;

  StatisticCounter H_incoming_requests;
  StatisticCounter H_rejected_overload;
};

#endif
