#!/bin/bash

# @file poll_homestead.sh
#
# Copyright (C) Metaswitch Networks 2016
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

# If the process has not been running for at least the grace period, the script
# returns zero. This is to allow the process some time to initialize.
GRACE_PERIOD=20


. /etc/clearwater/config
http_ip=$(/usr/share/clearwater/bin/bracket-ipv6-address $local_ip)
/usr/share/clearwater/bin/poll-http $http_ip:8888
rc=$?
if /usr/share/clearwater/bin/is-stable /var/run/homestead/homestead.pid $GRACE_PERIOD; then
  exit $rc
else
  echo -n " (failure ($rc) ignored - in grace period) " >&2
  exit 0
fi
