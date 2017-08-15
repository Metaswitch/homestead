/**
 * @file homesteadsasevent.h Homestead event IDs
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HOMESTEADSASEVENT_H__
#define HOMESTEADSASEVENT_H__

#include "sasevent.h"

namespace SASEvent
{
  //----------------------------------------------------------------------------
  // Homestead events
  //----------------------------------------------------------------------------
  const int INVALID_SCHEME = HOMESTEAD_BASE + 0x0000;
  const int UNSUPPORTED_SCHEME = HOMESTEAD_BASE + 0x0001;
  const int NO_IMPU = HOMESTEAD_BASE + 0x0010;
  const int NO_AV_CACHE = HOMESTEAD_BASE + 0x0020;
  const int NO_AV_HSS = HOMESTEAD_BASE + 0x0030;
  const int INVALID_REG_TYPE = HOMESTEAD_BASE + 0x0040;
  const int SUB_NOT_REG = HOMESTEAD_BASE + 0x0050;
  const int NO_SUB_CACHE = HOMESTEAD_BASE + 0x0060;
  const int NO_REG_DATA_CACHE = HOMESTEAD_BASE + 0x0070;
  const int REG_DATA_HSS_SUCCESS = HOMESTEAD_BASE + 0x0080;
  const int REG_DATA_HSS_FAIL = HOMESTEAD_BASE + 0x0090;
  const int REG_DATA_HSS_INVALID = HOMESTEAD_BASE + 0x0091;
  const int REG_DATA_HSS_FAIL_ASSIGNMENT_TYPE = HOMESTEAD_BASE + 0x0092;
  const int REG_DATA_HSS_UPDATED_WILDCARD = HOMESTEAD_BASE + 0x0093;
  const int ICSCF_NO_HSS = HOMESTEAD_BASE + 0x00A0;
  const int ICSCF_NO_HSS_CHECK_CASSANDRA = HOMESTEAD_BASE + 0x00A1;
  const int ICSCF_NO_HSS_CASSANDRA_NO_SUBSCRIBER = HOMESTEAD_BASE + 0x00A2;
  const int REG_STATUS_HSS_FAIL = HOMESTEAD_BASE + 0x00B0;
  const int LOC_INFO_HSS_FAIL = HOMESTEAD_BASE + 0x00C0;
  const int INVALID_DEREG_REASON = HOMESTEAD_BASE + 0x00D0;
  const int NO_IMPU_DEREG = HOMESTEAD_BASE + 0x00E0;
  const int DEREG_FAIL = HOMESTEAD_BASE + 0x00F0;
  const int DEREG_SUCCESS = HOMESTEAD_BASE + 0x0100;
  const int UPDATED_REG_DATA = HOMESTEAD_BASE + 0x0110;
  const int CACHE_GET_AV = HOMESTEAD_BASE + 0x0130;
  const int CACHE_GET_AV_SUCCESS = HOMESTEAD_BASE + 0x0140;
  const int CACHE_PUT_ASSOC_IMPU = HOMESTEAD_BASE + 0x0150;
  const int CACHE_PUT_ASSOC_IMPU_SUCCESS = HOMESTEAD_BASE + 0x0151;
  const int CACHE_PUT_ASSOC_IMPU_FAIL = HOMESTEAD_BASE + 0x0152;
  const int CACHE_GET_ASSOC_IMPU = HOMESTEAD_BASE + 0x0160;
  const int CACHE_GET_ASSOC_IMPU_SUCCESS = HOMESTEAD_BASE + 0x0170;
  const int CACHE_GET_ASSOC_IMPU_FAIL = HOMESTEAD_BASE + 0x0180;
  const int CACHE_GET_REG_DATA = HOMESTEAD_BASE + 0x0190;
  const int CACHE_GET_REG_DATA_IMPIS = HOMESTEAD_BASE + 0x0191;
  const int CACHE_GET_REG_DATA_SUCCESS = HOMESTEAD_BASE + 0x01A0;
  const int CACHE_ASSOC_IMPI = HOMESTEAD_BASE + 0x01B0;
  const int CACHE_PUT_REG_DATA = HOMESTEAD_BASE + 0x01C0;
  const int CACHE_PUT_REG_DATA_SUCCESS = HOMESTEAD_BASE + 0x01C1;
  const int CACHE_PUT_REG_DATA_FAIL = HOMESTEAD_BASE + 0x01C2;
  const int CACHE_DELETE_IMPU = HOMESTEAD_BASE + 0x01D0;
  const int CACHE_DELETE_IMPUS_SUCCESS = HOMESTEAD_BASE + 0x01D1;
  const int CACHE_DELETE_IMPUS_FAIL = HOMESTEAD_BASE + 0x01D2;
  const int CACHE_DELETE_IMPUS_NOT_FOUND = HOMESTEAD_BASE + 0x01D3;
  const int CACHE_GET_ASSOC_PRIMARY_IMPUS = HOMESTEAD_BASE + 0x01E0;
  const int CACHE_GET_ASSOC_PRIMARY_IMPUS_SUCCESS = HOMESTEAD_BASE + 0x01F0;
  const int CACHE_DISASSOC_REG_SET = HOMESTEAD_BASE + 0x0200;
  const int CACHE_DELETE_IMPI_MAP = HOMESTEAD_BASE + 0x0210;
  const int NO_SIP_URI_IN_IRS = HOMESTEAD_BASE + 0x220;
  const int PPR_RECEIVED = HOMESTEAD_BASE + 0x230;
  const int RTR_RECEIVED = HOMESTEAD_BASE + 0x240;
  const int CACHE_GET_ASSOC_DEF_IMPU_FAIL = HOMESTEAD_BASE + 0x0250;
  const int PPR_CHANGE_DEFAULT_IMPU = HOMESTEAD_BASE + 0x0260;

} // namespace SASEvent

#endif

