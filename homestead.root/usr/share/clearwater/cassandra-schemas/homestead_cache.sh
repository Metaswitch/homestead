#! /bin/bash

netstat -na | grep -q ":7199[^0-9]"
while [ $? -ne 0 ]; do
    sleep 1
    printf "."
    netstat -na | grep -q ":7199[^0-9]"
done
netstat -na | grep "LISTEN" | awk '{ print $4 }' | grep -q ":9160\$"
while [ $? -ne 0 ]; do
    sleep 1
    printf "+"
    netstat -na | grep "LISTEN" | awk '{ print $4 }' | grep -q ":9160\$"
done

if [[ ! -e /var/lib/cassandra/data/homestead_cache ]];
then
    echo "CREATE KEYSPACE homestead_cache WITH strategy_class = 'SimpleStrategy' AND strategy_options:replication_factor = 2;
USE homestead_cache;
CREATE TABLE impi (private_id text PRIMARY KEY, digest_ha1 text, digest_realm text, digest_qop text, known_preferred boolean) WITH read_repair_chance = 1.0;
CREATE TABLE impu (public_id text PRIMARY KEY, ims_subscription_xml text, is_registered boolean) WITH read_repair_chance = 1.0;" | cqlsh -2
fi

echo "USE homestead_cache;
ALTER TABLE impu ADD primary_ccf text;
ALTER TABLE impu ADD secondary_ccf text;
ALTER TABLE impu ADD primary_ecf text;
ALTER TABLE impu ADD secondary_ecf text;" | cqlsh -2

if [[ ! -e /var/lib/cassandra/data/homestead_cache/impi_mapping ]];
then
    echo "USE homestead_cache;
CREATE TABLE impi_mapping (private_id text PRIMARY KEY, unused text) WITH read_repair_chance = 1.0;" | cqlsh -2
fi
