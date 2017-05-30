/**
 * @file fakesnmp.hpp Fake SNMP infrastructure for UT.
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef FAKE_SNMP_H
#define FAKE_SNMP_H

#include "snmp_row.h"
#include "snmp_event_accumulator_table.h"
#include "snmp_counter_table.h"
#include "snmp_cx_counter_table.h"

namespace SNMP
{

class FakeCounterTable: public CounterTable
{
public:
  FakeCounterTable() {};
  void increment() {};
};

class FakeCxCounterTable: public CxCounterTable
{
public:
  FakeCxCounterTable() {};
  void increment(DiameterAppId app_id, int code) {};
};

class FakeEventAccumulatorTable: public EventAccumulatorTable
{
public:
  FakeEventAccumulatorTable() {};
  void accumulate(uint32_t sample) {};
};
}

#endif
