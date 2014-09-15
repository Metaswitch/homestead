/**
 * @file homesteaddcea.cpp - Craft Description, Cause, Effect, and Action
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
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
#include <string>
#include "craft_dcea.h"
// Chronos syslog identities
/**********************************************************
/ log_id
/ severity
/ Description: (formatted)
/ Cause: 
/ Effect:
/ Action:
**********************************************************/
// Homestead syslog identities
/*************************************************************/
SysLog CL_HOMESTEAD_INVALID_SAS_OPTION
{
  CL_HOMESTEAD_ID + 1,
  SYSLOG_ERR,
  "Invalid --sas option, SAS disabled",
  "",
  "",
  ""
};
SysLog CL_HOMESTEAD_HELP_OPTION_EXIT
{
  CL_HOMESTEAD_ID + 2,
  SYSLOG_ERR,
  "Help(h) option causes program to exit",
  "",
  "",
  ""
};
SysLog1<char> CL_HOMESTEAD_INVALID_OPTION_C
{
  CL_HOMESTEAD_ID + 3,
  SYSLOG_ERR,
  "Fatal - Unknown command line option %c.  Run with --help for options.",
  "",
  "",
  ""
};
SysLog1<const char*> CL_HOMESTEAD_CRASH
{
  CL_HOMESTEAD_ID + 4,
  SYSLOG_ERR,
  "Fatal - Homestead has exited or crashed with signal %s",
  "",
  "",
  ""
};
SysLog CL_HOMESTEAD_STARTED
{
  CL_HOMESTEAD_ID + 5,
  SYSLOG_NOTICE,
  "Homestead started",
  "",
  "",
  ""
};
SysLog1<int> CL_HOMESTEAD_CASSANDRA_CACHE_INIT_FAIL
{
  CL_HOMESTEAD_ID + 6,
  SYSLOG_ERR,
  "Fatal - Failed to initialize the cache for the CassandraStore - error code %d",
  "",
  "",
  ""
};
SysLog2<const char*, int> CL_HOMESTEAD_DIAMETER_INIT_FAIL
{
  CL_HOMESTEAD_ID + 7,
  SYSLOG_ERR,
  "Fatal - Failed to initialize Diameter stack in function %s with error %d",
  "",
  "",
  ""
};
SysLog2<const char*, int> CL_HOMESTEAD_HTTP_INIT_FAIL
{
  CL_HOMESTEAD_ID + 8,
  SYSLOG_ERR,
  "Fatal - Failed to initialize HttpStack stack in function %s with error %d",
  "",
  "",
  ""
};
SysLog CL_HOMESTEAD_ENDED
{
  CL_HOMESTEAD_ID + 9,
  SYSLOG_ERR,
  "Fatal - Termination signal received - terminating",
  "",
  "",
  ""
};
SysLog2<const char*, int> CL_HOMESTEAD_HTTP_STOP_FAIL
{
  CL_HOMESTEAD_ID + 10,
  SYSLOG_ERR,
  "Failed to stop HttpStack stack in function %s with error %d",
  "",
  "",
  ""
};
SysLog2<const char*, int> CL_HOMESTEAD_DIAMETER_STOP_FAIL
{
  CL_HOMESTEAD_ID + 11,
  SYSLOG_ERR,
  "Failed to stop Diameter stack in function %s with error %d",
  "",
  "",
  ""
};


