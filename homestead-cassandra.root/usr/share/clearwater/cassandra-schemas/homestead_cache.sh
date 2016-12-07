#! /bin/bash

cassandra_hostname="127.0.0.1"

. /etc/clearwater/config

. /usr/share/clearwater/cassandra_schema_utils.sh

quit_if_no_cassandra

echo "Adding/updating Cassandra schemas..."

# Wait for the cassandra cluster to come online
count=0
/usr/share/clearwater/bin/poll_cassandra.sh --no-grace-period > /dev/null 2>&1

while [ $? -ne 0 ]; do
  ((count++))
  if [ $count -gt 120 ]; then
    echo "Cassandra isn't responsive, unable to add/update schemas yet"
    exit 1
  fi

  sleep 1
  /usr/share/clearwater/bin/poll_cassandra.sh --no-grace-period > /dev/null 2>&1

done

CQLSH="/usr/share/clearwater/bin/run-in-signaling-namespace cqlsh $cassandra_hostname"

if [[ ! -e /var/lib/cassandra/data/homestead_cache ]] || \
   [[ $cassandra_hostname != "127.0.0.1" ]];
then
  # replication_str is set up by
  # /usr/share/clearwater/cassandra-schemas/replication_string.sh
  echo "CREATE KEYSPACE IF NOT EXISTS homestead_cache WITH REPLICATION =  $replication_str;
        USE homestead_cache;
        CREATE TABLE IF NOT EXISTS impi (
          private_id text PRIMARY KEY,
          digest_ha1 text,
          digest_realm text,
          digest_qop text)
        WITH COMPACT STORAGE AND read_repair_chance = 1.0;
        CREATE TABLE IF NOT EXISTS impu (
          public_id text PRIMARY KEY,
          ims_subscription_xml text,
          is_registered boolean)
          WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | $CQLSH
  # We've removed the known_preferred key from newly-created copies of the
  # schema but we can't alter existing copies because there might be downlevel
  # Homestead nodes that still want to read/write it (even though the data
  # stored there is never used).
fi

echo "USE homestead_cache; DESC TABLE impu" | $CQLSH | grep primary_ccf > /dev/null
if [ $? != 0 ]; then
  echo "USE homestead_cache;
        ALTER TABLE impu ADD primary_ccf text;
        ALTER TABLE impu ADD secondary_ccf text;
        ALTER TABLE impu ADD primary_ecf text;
        ALTER TABLE impu ADD secondary_ecf text;" | $CQLSH
fi

if [[ ! -e /var/lib/cassandra/data/homestead_cache/impi_mapping ]];
then
  echo "USE homestead_cache;
        CREATE TABLE IF NOT EXISTS impi_mapping (private_id text PRIMARY KEY, unused text) WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | $CQLSH
fi

if [ -z "$speculative_retry_value" ]
then
  speculative_retry_value="50ms"
fi

echo "USE homestead_cache;
      ALTER TABLE impu WITH speculative_retry = '$speculative_retry_value';
      ALTER TABLE impi_mapping WITH speculative_retry = '$speculative_retry_value';
      ALTER TABLE impi WITH speculative_retry = '$speculative_retry_value';" | $CQLSH
