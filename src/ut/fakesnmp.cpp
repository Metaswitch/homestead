/**
 * @file fakesnmp.cpp Fake SNMP infrastructure for UT.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "snmp_internal/snmp_includes.h"
#include "fakesnmp.hpp"
#include "snmp_event_accumulator_table.h"
#include "snmp_counter_table.h"
#include "snmp_cx_counter_table.h"

namespace SNMP
{
CounterTable* CounterTable::create(std::string name, std::string oid)
{
  return new FakeCounterTable();
};

CxCounterTable* CxCounterTable::create(std::string name, std::string oid)
{
  return new FakeCxCounterTable();
};

EventAccumulatorTable* EventAccumulatorTable::create(std::string name, std::string oid)
{
  return new FakeEventAccumulatorTable();
};

}
