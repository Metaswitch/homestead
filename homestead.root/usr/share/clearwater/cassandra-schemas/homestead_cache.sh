#! /bin/bash

. /etc/clearwater/config
if [ ! -z $signaling_namespace ]
then
  if [ $EUID -ne 0 ]
  then
    echo "When using multiple networks, schema creation must be run as root"
    exit 2
  fi
  namespace_prefix="ip netns exec $signaling_namespace"
fi

if [[ ! -e /var/lib/cassandra/data/homestead_cache ]];
then
  header="Waiting for Cassandra"
  let "cnt=0"
  $namespace_prefix netstat -na | grep -q ":7199[^0-9]"
  while [ $? -ne 0 ]; do
    sleep 1
    printf "${header}."
    header=""
    let "cnt=$cnt + 1"
    if [ $cnt -gt 120 ]; then
      printf "*** ERROR: Cassandra did not come online!\n"
      exit 1
    fi
    $namespace_prefix netstat -na | grep -q ":7199[^0-9]"
  done
  let "cnt=0"
  $namespace_prefix netstat -na | grep "LISTEN" | awk '{ print $4 }' | grep -q ":9160\$"
  while [ $? -ne 0 ]; do
    sleep 1
    printf "${header}+"
    header=""
    let "cnt=$cnt + 1"
    if [ $cnt -gt 120 ]; then
      printf "*** ERROR: Cassandra did not come online!\n"
      exit 1
    fi
    $namespace_prefix netstat -na | grep "LISTEN" | awk '{ print $4 }' | grep -q ":9160\$"
  done

  echo "CREATE KEYSPACE homestead_cache WITH REPLICATION =  {'class': 'SimpleStrategy', 'replication_factor': 2};
        USE homestead_cache;
        CREATE TABLE impi (private_id text PRIMARY KEY, digest_ha1 text, digest_realm text, digest_qop text, known_preferred boolean) WITH COMPACT STORAGE AND read_repair_chance = 1.0;
        CREATE TABLE impu (public_id text PRIMARY KEY, ims_subscription_xml text, is_registered boolean) WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | $namespace_prefix cqlsh
fi

echo "USE homestead_cache;
      ALTER TABLE impu ADD primary_ccf text;
      ALTER TABLE impu ADD secondary_ccf text;
      ALTER TABLE impu ADD primary_ecf text;
      ALTER TABLE impu ADD secondary_ecf text;" | $namespace_prefix cqlsh

if [[ ! -e /var/lib/cassandra/data/homestead_cache/impi_mapping ]];
then
  echo "USE homestead_cache;
        CREATE TABLE impi_mapping (private_id text PRIMARY KEY, unused text) WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | $namespace_prefix cqlsh
fi
