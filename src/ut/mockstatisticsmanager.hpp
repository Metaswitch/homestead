/**
 * @file mockstatisticsmanager.hpp Mock statistics manager for UT.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKSTATISTICSMANAGER_HPP__
#define MOCKSTATISTICSMANAGER_HPP__

#include "gmock/gmock.h"
#include "statisticsmanager.h"

class MockStatisticsManager : public StatisticsManager
{
public:
  // Short poll timeout to not slowdown test shutdown.
  MockStatisticsManager() : StatisticsManager() {}
  virtual ~MockStatisticsManager() {}

  MOCK_METHOD1(update_H_latency_us, void(unsigned long sample));
  MOCK_METHOD1(update_H_hss_latency_us, void(unsigned long sample));
  MOCK_METHOD1(update_H_hss_digest_latency_us, void(unsigned long sample));
  MOCK_METHOD1(update_H_hss_subscription_latency_us, void(unsigned long sample));
  MOCK_METHOD1(update_H_cache_latency_us, void(unsigned long sample));

  MOCK_METHOD0(incr_H_incoming_requests, void());
  MOCK_METHOD0(incr_H_rejected_overload, void());

  MOCK_METHOD1(update_http_latency_us, void(unsigned long sample));
  MOCK_METHOD0(incr_http_incoming_requests, void());
  MOCK_METHOD0(incr_http_rejected_overload, void());
};

#endif
