/**
 * @file statisticsmanager.h class used for all homestead statistics.
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

#ifndef STATISTICSMANAGER_H__
#define STATISTICSMANAGER_H__

#include "zmq_lvc.h"
#include "snmp_counter_table.h"
#include "snmp_event_accumulator_table.h"
#include "httpstack.h"

#define COUNTER_INCR_METHOD(NAME) \
  virtual void incr_##NAME() { (NAME)->increment(); }

#define ACCUMULATOR_UPDATE_METHOD(NAME) \
  virtual void update_##NAME(unsigned long sample) { (NAME)->accumulate(sample); }

class StatisticsManager : public HttpStack::StatsInterface
{
public:
  StatisticsManager();
  virtual ~StatisticsManager();

  ACCUMULATOR_UPDATE_METHOD(H_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_digest_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_hss_subscription_latency_us);
  ACCUMULATOR_UPDATE_METHOD(H_cache_latency_us);

  COUNTER_INCR_METHOD(H_incoming_requests);
  COUNTER_INCR_METHOD(H_rejected_overload);

  // Methods required to implement the HTTP stack stats interface.
  void update_http_latency_us(unsigned long latency_us)
  {
    update_H_latency_us(latency_us);
  }
  void incr_http_incoming_requests() { incr_H_incoming_requests(); }
  void incr_http_rejected_overload() { incr_H_rejected_overload(); }

private:
  SNMP::EventAccumulatorTable* H_latency_us;
  SNMP::EventAccumulatorTable* H_hss_latency_us;
  SNMP::EventAccumulatorTable* H_hss_digest_latency_us;
  SNMP::EventAccumulatorTable* H_hss_subscription_latency_us;
  SNMP::EventAccumulatorTable* H_cache_latency_us;

  SNMP::CounterTable* H_incoming_requests;
  SNMP::CounterTable* H_rejected_overload;
};

#endif
