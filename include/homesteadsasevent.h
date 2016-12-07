/**
 * @file homesteadsasevent.h Homestead event IDs
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
  const int NO_IMPU_AKA = HOMESTEAD_BASE + 0x0010;
  const int NO_AV_CACHE = HOMESTEAD_BASE + 0x0020;
  const int NO_AV_HSS = HOMESTEAD_BASE + 0x0030;
  const int INVALID_REG_TYPE = HOMESTEAD_BASE + 0x0040;
  const int SUB_NOT_REG = HOMESTEAD_BASE + 0x0050;
  const int NO_SUB_CACHE = HOMESTEAD_BASE + 0x0060;
  const int NO_REG_DATA_CACHE = HOMESTEAD_BASE + 0x0070;
  const int REG_DATA_HSS_SUCCESS = HOMESTEAD_BASE + 0x0080;
  const int REG_DATA_HSS_FAIL = HOMESTEAD_BASE + 0x0090;
  const int REG_DATA_HSS_INVALID = HOMESTEAD_BASE + 0x0091;
  const int ICSCF_NO_HSS = HOMESTEAD_BASE + 0x00A0;
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
  const int CACHE_GET_REG_DATA_SUCCESS = HOMESTEAD_BASE + 0x01A0;
  const int CACHE_ASSOC_IMPI = HOMESTEAD_BASE + 0x01B0;
  const int CACHE_PUT_REG_DATA = HOMESTEAD_BASE + 0x01C0;
  const int CACHE_PUT_REG_DATA_SUCCESS = HOMESTEAD_BASE + 0x01C1;
  const int CACHE_PUT_REG_DATA_FAIL = HOMESTEAD_BASE + 0x01C2;
  const int CACHE_DELETE_IMPUS = HOMESTEAD_BASE + 0x01D0;
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

} // namespace SASEvent

#endif

