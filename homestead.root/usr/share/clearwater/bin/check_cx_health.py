#! /usr/bin/python
#
# @file check_cx_health.py
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
ss = netsnmp.Session(DestHost='localhost', Version=2, Community='clearwater-internal')

# This routine returns the number of "success" and "error" responses received
# over Cx over the previous 5 minute interval.
#
# By "success" here, we mean a response code that indicates that the request
# got to the server, not necessarily a success from an application PoV.  So,
# for instance, an experimental code that means "user not found" is actually a
# "success" because the fact that we got it means that the request did get to
# the server OK.
#
# The determination of success or error is done by appID and result code as
# follows
#
# -  Any EXPERIMENTAL (_3GPP) result code is considered a success (because
#    you can't get an experimental return code from the client stack)
# -  Any normal result code of the format 2XXX or 4XXX (e.g. 2001
#    - DIAMETER_SUCCESS or 4001 - DIAMETER_AUTHORIZATION_REJECTED) is also
#    considered a success as you can only get these if the server has responded
# -  Anything else means we had an error code that might have come from the stack
#    having failed to contact the server, so is treated as an error
#
# INPUT
#   head_oid         - Pre-indexed OID of the segment of the table we are
#                      searching. This will be the OID of the Cx response
#                      table concatenated with:
#                      -  ".1.4" (".1" says "entry" and ".4" says "count element")
#                      -  ".3" ("Previous 5 minute values")
#                      -  AppID.  This will be ".0" for "Base" and ".1" for _3GPP

def count_responses(head_oid):

    _log.debug("Counting responses for {}".format(head_oid))

    # Get all values under the head_oid
    vars = netsnmp.VarList(netsnmp.Varbind(head_oid))
    ss.walk(vars)

    # vars now contains a list of all the rows fetched by the above walk
    # Iterate over it
    total_successes = 0
    total_errors = 0

    for var in vars:
        # Get the tag (i.e. the full OID for this value), e.g.
        # 'iso.2.826.0.1.1578918.9.5.10.1.4.3.0.1001', and crack out the final
        # element of the OID ('1001' here).  This is the returncode to which
        # the count value corresponds
        #
        # Also crack out the penultimate element - this is the Appid, which
        # tells us whether this is a result code or an experimental result code
        _log.debug("Got value {} for OID {}".format(var.val, var.tag))
        elements = var.tag.split('.')
        appid = int(elements[-2])
        retcode = int(elements[-1])
        value = int(var.val)
        _log.debug("Extracted AppID {}, retcode {} and count {}".format(appid, retcode, value))

        # Accumulate counts
        if appid == 1:
            # Experimental result code.  Automatically successful
            total_successes += value

        else:
            # Result code.  Check response code class
            response_class = retcode / 1000

            if response_class == 2 or response_class == 4:
                # This is a "success" from the PoV of server contactability
                total_successes += value
            else:
                # Anything else is considered an error
                total_errors += value

    _log.debug("Total successes for {} = {}".format(head_oid, total_successes))
    _log.debug("Total errors for {} = {}".format(head_oid, total_errors))

    return (total_successes, total_errors)

# Get the number of timeouts for the table segment head oid passed
def count_timeouts(table_segment_head):

    # timeouts are always at element ".2.0" below the table segment head
    timeouts = ss.get(netsnmp.VarList(netsnmp.Varbind(table_segment_head + ".2.0")))
    _log.debug("Got value {} for timeout OID {}.2.0".format(timeouts[0], table_segment_head))

    return int(timeouts[0])

# Perform a single check of the statistics to determine whether the Cx Diameter
# stack might be broken.
#
# Note that this is called twice to ensure that we do not make a false
# positive determination due to "tick rollover"
def are_all_requests_failing():

    # Collate all "Previous 5 minute" statistics for the UAR, MAR, SAR and LIR
    # statistic tables
    #
    # These tables are at OIDs 10 thru 13 under the Homestead OID.  For
    # each table, construct the head OID corresponding to the "count" column
    # in the "previous 5 minute" time period and call the "count...()"
    # functions to count the responses of each type.
    total_successes = 0
    total_errors = 0

    for table in [10, 11, 12, 13]:
        head_oid = table_segment_head % table

        # Collate response counts from the "BASE" and "_3GPP" response tables
        # (regular and experimental result codes respectively)
        for appid in ["0","1"]:
            (total_successes, total_errors) = \
                     map(operator.add, (total_successes, total_errors), \
                                       count_responses(head_oid + "." + appid))

        # Add the count of timeouts to the server error count
        total_errors += count_timeouts(head_oid)

    _log.debug("Total successes = {}".format(total_successes))
    _log.debug("Total errors = {}".format(total_errors))

    return (total_successes == 0) and (total_errors > server_error_tolerance)

# Determine whether all requests failed over the previous 5 minute stats
# period by running are_all_requests_failing() twice and only returning true
# if both checks are positive
result = are_all_requests_failing()
_log.info("First check returned {}".format(result))

if result:

    # Don't trust the first run.  Do it again amd only output true if this run
    # also returns true
    result = are_all_requests_failing()
    _log.info("Second check returned {}".format(result))

print "All requests failing? {}".format(result)

# Return a distinctive exit code ("3"), rather than the normal error exit value
# ("1") if all requests are failing.  This is because the monit script will
# restart homestead on the basis of this result and we don't want it do to that
# if this script is just failing to execute for some reason.
if result:
    exitcode = 3
else:
    exitcode = 0

exit(exitcode)
