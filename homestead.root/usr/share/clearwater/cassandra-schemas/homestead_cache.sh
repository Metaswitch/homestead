#! /bin/bash
if [[ ! -e /var/lib/cassandra/data/homestead_cache ]];
then
    echo "CREATE KEYSPACE homestead_cache WITH REPLICATION =  {'class': 'SimpleStrategy', 'replication_factor': 2};
USE homestead_cache;
CREATE TABLE impi (private_id text PRIMARY KEY, digest_ha1 text, digest_realm text, digest_qop text, known_preferred boolean) WITH read_repair_chance = 1.0;
CREATE TABLE impu (public_id text PRIMARY KEY, ims_subscription_xml text) WITH read_repair_chance = 1.0;" | cqlsh -3
fi
