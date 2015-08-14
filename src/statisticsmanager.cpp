/**
 * @file statisticsmanager.cpp class used for all homestead statistics.
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

#include <statisticsmanager.h>

StatisticsManager::StatisticsManager()
{
  H_latency_us = SNMP::AccumulatorTable::create("H_latency_us",
                                                ".1.2.826.0.1.1578918.9.5.1");
  H_hss_latency_us = SNMP::AccumulatorTable::create("H_hss_latency_us",
                                                    ".1.2.826.0.1.1578918.9.5.2");
  H_cache_latency_us = SNMP::AccumulatorTable::create("H_cache_latency_us",
                                                      ".1.2.826.0.1.1578918.9.5.3");
  H_hss_digest_latency_us = SNMP::AccumulatorTable::create("H_hss_digest_latency_us",
                                                           ".1.2.826.0.1.1578918.9.5.4");
  H_hss_subscription_latency_us = SNMP::AccumulatorTable::create("H_hss_subscription_latency_us",
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
