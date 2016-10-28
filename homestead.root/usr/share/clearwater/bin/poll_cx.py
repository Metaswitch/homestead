#! /usr/bin/python
#
# @file poll_cx.py
#
# Project Clearwater - IMS in the Cloud
# Copyright (C) 2016  Metaswitch Networks Ltd
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version, along with the "Special Exception" for use of
# the program along with SSL, set forth below. This program is distributed
# in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details. You should have received a copy of the GNU General Public
# License along with this program.  If not, see
# <http://www.gnu.org/licenses/>.
#
# The author can be reached by email at clearwater@metaswitch.com or by
# post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
#
# Special Exception
# Metaswitch Networks Ltd  grants you permission to copy, modify,
# propagate, and distribute a work formed by combining OpenSSL with The
# Software, or a work derivative of such a combination, even if such
# copying, modification, propagation, or distribution would otherwise
# violate the terms of the GPL. You must comply with the GPL in all
# respects for all of the code used other than OpenSSL.
# "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
# Project and licensed under the OpenSSL Licenses, or a work based on such
# software and licensed under the OpenSSL Licenses.
# "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
# under which the OpenSSL Project distributes the OpenSSL toolkit software,
# as those licenses appear in the file LICENSE-OPENSSL.


# This poll script determines whether, during the "Previous 5 minutes" stat
# period, more than a configured threshold of Diameter request got rejected
# with "server" errors (i.e. errors which would have prompted Homestead to
# respond to the caller with a 5XX HTTP code) and NO requests succeeded over the
# same period.  monit responds to a positive response from this script by
# restarting homestead.
#
# The rationale for this function is that, if many diameter requests are failing
# with server errors and no requests are succeeding, this _might_ be because
# the Diameter stack is broken (rather than a genuine server or configuration
# issue) perhaps because freeDiameter failed to handle an unsual network
# failure and "hung" the Diameter connections.  In cases such as this, it is
# likely that service can be restored by restarting homestead, and there is no
# harm in trying this, as no requests are succeeding anyway.
#
# Note that the script does the stat lookup and calculation twice (and only
# responds in the positive if both checks suggest that the stack might be
# broken).  The reasoning here is that its not possible to check the "Previous
# 5 minute" value for all statistics atomically and therefore the values of
# some stats might be for a different 5 minute period.  If the script only
# checked once, it would be vulnerable to this "tick rollover" problem and
# might incorrectly identify the stack as needing a reboot, invalidly
# impacting service.

import sys
import re
import netsnmp
import operator
import logging

# OID template for that segment of a Cx stat table covering previous 5 minute
# counts
table_segment_head = '.1.2.826.0.1.1578918.9.5.%d.1.4.3'

#logging.basicConfig(level=logging.DEBUG)
_log = logging.getLogger(__name__)

# Get tolerance to server errors. We will only treat the number of server
# errors at the Diameter interface as serious enough to warrant a restart
# (provided there have been no successes over the same period) if the total
# number of errors exceeds this tolerance.
server_error_tolerance = int(sys.argv[1])
_log.info("Server error tolerance = {}".format(server_error_tolerance))

# Get a connection to the local SNMP agent
ss = netsnmp.Session(DestHost='localhost', Version=2, Community='clearwater')

# Compile the "find last element in an OID" regex
find_last_element = re.compile('([^\.]*)$')

# This routine takes tuples of response codes (successful responses,
# client error responses and server error responses) and returns the number of
# success and client error responses received from Diameter over the previous
# 5 minute interval.
#
# Any responses seen for return codes not in the tuples passed are
# considered server errors
#
# INPUT
#   head_oid         - Pre-indexed OID of the segment of the table we are
#                      searching. This will be the OID of the Cx response
#                      table concatenated with:
#                      -  ".1.4" (".1" says "entry" and ".4" says "count element")
#                      -  ".3" ("Previous 5 minute values")
#                      -  AppID.  This will be ".0" for "Base" and ".1" for _3GPP
#   successes        - The response codes in this segment that correspond to
#                      2XX "success" HTTP codes from a Homestead user's
#                      perspective
#   client_errors    - The response codes in this segment that correspond to
#                      4XX "client failure" HTTP codes from a Homestead user's
#                      perspective

def count_responses(head_oid, successes, client_errors):

    _log.debug("Counting responses for {}".format(head_oid))

    _log.debug("Successes are {}".format(','.join(str(success) for success in successes)))
    _log.debug("Client errors are {}".format(','.join(str(error) for error in client_errors)))

    # Get all values under the head_oid
    vars = netsnmp.VarList(netsnmp.Varbind(head_oid))
    ss.walk(vars)

    # vars now contains a list of all the rows fetched by the above walk
    # Iterate over it
    total_successes = 0
    total_client_errors = 0
    total_server_errors = 0

    for var in vars:
        # Get the tag (i.e. the full OID for this value), e.g.
        # 'iso.2.826.0.1.1578918.9.5.10.1.4.3.0.1001', and crack out the final
        # element of the OID ('1001' here).  This is the returncode to which
        # the count value corresponds
        _log.debug("Got value {} for OID {}".format(var.val, var.tag))
        m = re.search(find_last_element, var.tag)
        retcode = int(m.group(0))
        value = int(var.val)
        _log.debug("Extracted retcode {} and count {}".format(retcode, value))

        if retcode in successes:
            # Add count to success total
            total_successes += value

        elif retcode in client_errors:
            # Add count to client error total
            total_client_errors += value

        else:
            # Anything else is considered a server error
            total_server_errors += value

    _log.debug("Total successes for {} = {}".format(head_oid, total_successes))
    _log.debug("Total client errors for {} = {}".format(head_oid, total_client_errors))
    _log.debug("Total server errors for {} = {}".format(head_oid, total_server_errors))

    return (total_successes, total_client_errors, total_server_errors)

# Get the number of timeouts for the table segment head oid passed
def count_timeouts(table_segment_head):

    # timeouts are always at element ".2.0" below the table segment head
    timeouts = ss.get(netsnmp.VarList(netsnmp.Varbind(table_segment_head + ".2.0")))
    _log.debug("Got value {} for timeout OID {}.2.0".format(timeouts[0], table_segment_head))

    return int(timeouts[0])

# Count all the successes, client errors and server errors for the LIR table
#
# Returns the counts in a tuple
def LIR_responses():
    # Determine the head of the segment of the table we are going to read
    lir_segment_head = table_segment_head % 13

    # Get the successes and client and server errors for the BASE responses and
    # add them to the successes and client and server errors for the _3GPP responses
    (total_successes, total_client_errors, total_server_errors) = \
         map(operator.add, count_responses(lir_segment_head + ".0", [2001], []), \
                           count_responses(lir_segment_head + ".1", [2003,5003], [5001]))

    # Add the count of timeouts to the server error count
    total_server_errors += count_timeouts(lir_segment_head)

    _log.debug("Total LIR successes = {}".format(total_successes))
    _log.debug("Total LIR client errors = {}".format(total_client_errors))
    _log.debug("Total LIR server errors = {}".format(total_server_errors))

    return (total_successes, total_client_errors, total_server_errors)

# Count all the successes, client errors and server errors for the UAR table
#
# Returns the counts in a tuple
def UAR_responses():
    # Determine the head of the segment of the table we are going to read
    uar_segment_head = table_segment_head % 12

    # Get the successes and client and server errors for the BASE responses and
    # add them to the successes and client and server errors for the _3GPP responses
    (total_successes, total_client_errors, total_server_errors) = \
        map(operator.add, count_responses(uar_segment_head + ".0", [2001], [5003]), \
                          count_responses(uar_segment_head + ".1", [2001,2002], [5001,5002,5004]))

    # Add the count of timeouts to the server error count
    total_server_errors += count_timeouts(uar_segment_head)

    _log.debug("Total UAR successes = {}".format(total_successes))
    _log.debug("Total UAR client errors = {}".format(total_client_errors))
    _log.debug("Total UAR server errors = {}".format(total_server_errors))

    return (total_successes, total_client_errors, total_server_errors)

# Count all the successes, client errors and server errors for the SAR table
#
# Returns the counts in a tuple
def SAR_responses():
    # Determine the head of the segment of the table we are going to read
    sar_segment_head = table_segment_head % 11

    # Get the successes and client and server errors for the BASE responses
    (total_successes, total_client_errors, total_server_errors) = \
                           count_responses(sar_segment_head + ".0", [2001], [])

    # 3GPP response stats aren't currently maintained for SARs

    # Add the count of timeouts to the server error count
    total_server_errors += count_timeouts(sar_segment_head)

    _log.debug("Total SAR successes = {}".format(total_successes))
    _log.debug("Total SAR client errors = {}".format(total_client_errors))
    _log.debug("Total SAR server errors = {}".format(total_server_errors))

    return (total_successes, total_client_errors, total_server_errors)

# Count all the successes, client errors and server errors for the MAR table
#
# Returns the counts in a tuple
def MAR_responses():
    # Determine the head of the segment of the table we are going to read
    mar_segment_head = table_segment_head % 10

    # Get the successes and client and server errors for the BASE responses
    (total_successes, total_client_errors, total_server_errors) = \
                       count_responses(mar_segment_head + ".0", [2001], [5001])

    # 3GPP response stats aren't currently maintained for MARs

    # Add the count of timeouts to the server error count
    total_server_errors += count_timeouts(mar_segment_head)

    _log.debug("Total MAR successes = {}".format(total_successes))
    _log.debug("Total MAR client errors = {}".format(total_client_errors))
    _log.debug("Total MAR server errors = {}".format(total_server_errors))

    return (total_successes, total_client_errors, total_server_errors)

# Perform a single check of the statistics to determine whether the Diameter
# stack might be broken.
#
# Note that this is called twice to ensure that we do not make a false
# positive determination due to "tick rollover"
def are_all_requests_failing():

    # Collate all "Previous 5 minute" statistics for the UAR, MAR, SAR and LIR
    # statistic tables
    (total_successes, total_client_errors, total_server_errors) = \
              map(operator.add, map(operator.add, MAR_responses(), SAR_responses()), \
                                map(operator.add, UAR_responses(), LIR_responses()))

    _log.debug("Total successes = {}".format(total_successes))
    _log.debug("Total client errors = {}".format(total_client_errors))
    _log.debug("Total server errors = {}".format(total_server_errors))

    return (total_successes == 0) and (total_server_errors > server_error_tolerance)

# Determine whether all requests failed over the previous 5 minute stats
# period by running are_all_requests_failing() twice and only returning true
# if both checks are positive
result = are_all_requests_failing()
_log.info("First check returned {}".format(result))

if result:

    # Don't trust the first run.  Do it again amd only return true if this run
    # also returns true
    result = are_all_requests_failing()
    _log.info("Second check returned {}".format(result))

print "All requests failing? {}".format(result)

exit(result)
