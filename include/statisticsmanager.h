/**
 * @file statisticsmanager.h class used for all homestead statistics.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
