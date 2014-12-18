/**
 * @file homestead_ent_definitions.h  Homestead ENT declarations.
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

#ifndef _HOMESTEAD_ENT_DEFINITIONS_H__
#define _HOMESTEAD_ENT_DEFINITIONS_H__

#include <string>
#include "pdlog.h"

// Defines instances of PDLog for Sprout

// Defines common definitions for PDLog (Problem Definition Log) classes

// A PDLogBase defines the base class containing:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice, 
//              and Info.  Directly corresponds to the syslog severity types.
//              Only Error and Notice are used.  See syslog_facade.h for 
//              definitions.
//   Message - Formatted description of the condition.
//   Cause - The cause of the condition.
//   Effect - The effect the condition.
//   Action - A list of one or more actions to take to resolve the condition 
//           if it is an error.
// The elements of the class are used to format a syslog call.
// The call to output to syslog is in the method,  dcealog.
// By default syslog limits a total syslog message size to 2048 bytes.  
// Anything above the limit is truncated.  The formatted message, cause, 
// effect, and action(s) are concatenated into the syslog message.  Note, 
// as an arbitrary convention, for more than a signle action, the actions 
// are numbered as (1)., (2)., ...  to make the actions easier to read within 
// the syslog message.  syslog removes extra whitespace and
// carriage-returns/linefeeds before inserting the complete string into a 
// message.  Note also, the action(s) are a list of strings with all but 
// the last string having a space character at the end.  The space makes the 
// actions more readable.  Most of the derived classes are templates.  
// The paremeterized types being values that are output as a formatted string 
// in the Message field.
// PDLog definitions content
static const PDLog CL_HOMESTEAD_INVALID_SAS_OPTION
(
  PDLogBase::CL_HOMESTEAD_ID + 1,
  PDLOG_INFO,
  "The sas_server option in /etc/clearwater/config is invalid or not "
  "configured.",
  "The interface to the SAS is not specified.",
  "No call traces will appear in the sas",
  "Set the fully qualified SAS hostname for the "
  "sas_server=<hostname>.<zone> option. "
  "Consult the Installation document."
);

// Removed Help option log
static const PDLog1<char> CL_HOMESTEAD_INVALID_OPTION_C
(
  PDLogBase::CL_HOMESTEAD_ID + 3,
  PDLOG_ERR,
  "Fatal - Unknown command line option %c.  Run with --help for options.",
  "There was an invalid command line option in /etc/clearwater/config.",
  "The application will exit and restart until the problem is fixed.",
  "Correct the /etc/clearwater/config file."
);

static const PDLog1<const char*> CL_HOMESTEAD_CRASH
(
  PDLogBase::CL_HOMESTEAD_ID + 4,
  PDLOG_ERR,
  "Fatal - Homestead has exited or crashed with signal %s.",
  "Homestead has encountered a fatal software error or has been terminated.",
  "The application will exit and restart.",
  "This error can occur if Homestead has been terminated by operator command. "
  "Actual crashes such as abort, segment trap, bus error trap, should be "
  "reported as a problem. "
);

static const PDLog CL_HOMESTEAD_STARTED
(
  PDLogBase::CL_HOMESTEAD_ID + 5,
  PDLOG_NOTICE,
  "Homestead started.",
  "The Homestead application is starting.",
  "Normal.",
  "None."
);

static const PDLog1<int> CL_HOMESTEAD_CASSANDRA_CACHE_INIT_FAIL
(
  PDLogBase::CL_HOMESTEAD_ID + 6,
  PDLOG_ERR,
  "Fatal - Failed to initialize the cache for the CassandraStore - "
  "error code %d.",
  "The memory cache used to access Cassandra could not be initialized.",
  "Homestead will exit.",
  "(1). Check to see if Cassandra is running.  Start if not "
  "(2). Try restarting Homestead to see if the problem clears. "
  "(3). Try reinstalling Homestead and starting Homestead to see if "
  "the problem clears. "
  "(4). If there is still a problem report the issue."
);

static const PDLog2<const char*, int> CL_HOMESTEAD_DIAMETER_INIT_FAIL
(
  PDLogBase::CL_HOMESTEAD_ID + 7,
  PDLOG_ERR,
  "Fatal - Failed to initialize Diameter stack in function %s with error %d.",
  "The Diameter interface could not be initialized or encountered an "
  "error while running.",
  "The application will exit and restart until the problem is fixed.",
  "(1). Check the configuration for the Diameter destination hosts. "
  "Consult the Installation document. "
  "(2). Check the connectivity to the to the Diameter host using Wireshark."
);

static const PDLog2<const char*, int> CL_HOMESTEAD_HTTP_INIT_FAIL
(
  PDLogBase::CL_HOMESTEAD_ID + 8,
  PDLOG_ERR,
  "Fatal - Failed to initialize HttpStack stack in function %s with error %d.",
  "The HTTP interfaces could not be initialized.",
  "Call processing will not work.",
  "(1). Check the configuration with the installation instructions for correctness. "
  "(2). Restart Homestead to see if the problem clears. "
  "(3). Report the issue if the problem cannot be resolved."
);

static const PDLog CL_HOMESTEAD_ENDED
(
  PDLogBase::CL_HOMESTEAD_ID + 9,
  PDLOG_ERR,
  "Fatal - Termination signal received - terminating.",
  "Homestead could have been stopped or Homestead could have been restarted "
  "by Monit due to a timeout.",
  "Homestead will exit.",
  "(1). For a command initiated stop the Monit log will indicate a stop "
  "on user request "
  "Consult the installation instructions. "
  "(2). Report the issue to support if there was a timeout. "
);

static const PDLog2<const char*, int> CL_HOMESTEAD_HTTP_STOP_FAIL
(
  PDLogBase::CL_HOMESTEAD_ID + 10,
  PDLOG_ERR,
  "The HTTP interfaces encountered an error when stopping the HTTP stack "
  "in %s with error %d.",
  "When Homestead was exiting it encountered an error when shutting "
  "down the HTTP stack.",
  "Not critical as Homestead is exiting anyway.",
  "Report this issue."
);

static const PDLog2<const char*, int> CL_HOMESTEAD_DIAMETER_STOP_FAIL
(
  PDLogBase::CL_HOMESTEAD_ID + 11,
  PDLOG_ERR,
  "Failed to stop Diameter stack in function %s with error %d.",
  "The Diameter interface encountered an error when shutting "
  "down the Diameter interface.",
  "Not critical as Homestead is exiting anyway.",
  "Report this issue."
);

#endif