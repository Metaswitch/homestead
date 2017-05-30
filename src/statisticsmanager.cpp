/**
 * @file statisticsmanager.cpp class used for all homestead statistics.
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <statisticsmanager.h>

StatisticsManager::StatisticsManager()
{
  H_latency_us = SNMP::EventAccumulatorTable::create("H_latency_us",
                                                ".1.2.826.0.1.1578918.9.5.1");
  H_hss_latency_us = SNMP::EventAccumulatorTable::create("H_hss_latency_us",
                                                    ".1.2.826.0.1.1578918.9.5.2");
  H_cache_latency_us = SNMP::EventAccumulatorTable::create("H_cache_latency_us",
                                                      ".1.2.826.0.1.1578918.9.5.3");
  H_hss_digest_latency_us = SNMP::EventAccumulatorTable::create("H_hss_digest_latency_us",
                                                           ".1.2.826.0.1.1578918.9.5.4");
  H_hss_subscription_latency_us = SNMP::EventAccumulatorTable::create("H_hss_subscription_latency_us",
                                                                 ".1.2.826.0.1.1578918.9.5.5");
  H_incoming_requests = SNMP::CounterTable::create("H_incoming_requests",
                                                   ".1.2.826.0.1.1578918.9.5.6");
  H_rejected_overload = SNMP::CounterTable::create("H_rejected_overload",
                                                   ".1.2.826.0.1.1578918.9.5.7");
}

StatisticsManager::~StatisticsManager()
{
  delete H_latency_us; H_latency_us = NULL;
  delete H_hss_latency_us; H_hss_latency_us = NULL;
  delete H_cache_latency_us; H_cache_latency_us = NULL;
  delete H_hss_digest_latency_us; H_hss_digest_latency_us = NULL;
  delete H_hss_subscription_latency_us; H_hss_subscription_latency_us = NULL;
  delete H_incoming_requests; H_incoming_requests = NULL;
  delete H_rejected_overload; H_rejected_overload = NULL;
}
