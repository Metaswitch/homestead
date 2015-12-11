#! /bin/bash
. /etc/clearwater/config

if [[ ! -e /var/lib/cassandra/data/homestead_cache ]];
then
  # Wait for the cassandra cluster to come online
  count=0
  /usr/share/clearwater/bin/poll_cassandra.sh --no-grace-period

  while [ $? -ne 0 ]; do
    ((count++))
    if [ $count -gt 120 ]; then
      echo "Cassandra isn't responsive, unable to add schemas"
      exit 1
    fi

    sleep 1
    /usr/share/clearwater/bin/poll_cassandra.sh --no-grace-period
  done

  replication_str="{'class': 'SimpleStrategy', 'replication_factor': 2}"

  # If local_site_name and remote_site_names are set then this is a GR
  # deployment. Set the replication strategy to NetworkTopologyStrategy and
  # define the sites.
  if [ -n $local_site_name ] && [ -n $remote_site_names ]
  then
    IFS=',' read -a remote_site_names_array <<< "$remote_site_names"
    replication_str="{'class': 'NetworkTopologyStrategy', '$local_site_name': 2"
    for remote_site in "${remote_site_names_array[@]}"
    do
      # Set the replication factor for each site to 2.
      replication_str+=", '$remote_site': 2"
    done
    replication_str+="}"
  fi

  echo "CREATE KEYSPACE homestead_cache WITH REPLICATION =  $replication_str;
        USE homestead_cache;
        CREATE TABLE impi (private_id text PRIMARY KEY, digest_ha1 text, digest_realm text, digest_qop text, known_preferred boolean) WITH COMPACT STORAGE AND read_repair_chance = 1.0;
        CREATE TABLE impu (public_id text PRIMARY KEY, ims_subscription_xml text, is_registered boolean) WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | /usr/share/clearwater/bin/run-in-signaling-namespace cqlsh
fi

echo "USE homestead_cache; DESC TABLE impu" | /usr/share/clearwater/bin/run-in-signaling-namespace cqlsh | grep primary_ccf > /dev/null
if [ $? != 0 ]; then
  echo "USE homestead_cache;
        ALTER TABLE impu ADD primary_ccf text;
        ALTER TABLE impu ADD secondary_ccf text;
        ALTER TABLE impu ADD primary_ecf text;
        ALTER TABLE impu ADD secondary_ecf text;" | /usr/share/clearwater/bin/run-in-signaling-namespace cqlsh
fi

if [[ ! -e /var/lib/cassandra/data/homestead_cache/impi_mapping ]];
then
  echo "USE homestead_cache;
        CREATE TABLE impi_mapping (private_id text PRIMARY KEY, unused text) WITH COMPACT STORAGE AND read_repair_chance = 1.0;" | /usr/share/clearwater/bin/run-in-signaling-namespace cqlsh
fi

if [ -z "$speculative_retry_value" ]
then
  speculative_retry_value="50ms"
fi

echo "USE homestead_cache;
      ALTER TABLE impu WITH speculative_retry = '$speculative_retry_value';
      ALTER TABLE impi_mapping WITH speculative_retry = '$speculative_retry_value';
      ALTER TABLE impi WITH speculative_retry = '$speculative_retry_value';" | /usr/share/clearwater/bin/run-in-signaling-namespace cqlsh
