#! /bin/bash

keyspace=$(basename $0|sed -e 's#^\(.*\)[.]sh$#\1#')
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

if [[ ! -e /var/lib/cassandra/data/${keyspace} ]]; then
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

    printf "CREATE KEYSPACE ${keyspace} WITH strategy_class = 'SimpleStrategy' AND strategy_options:replication_factor = 2;" > /tmp/$$.cqlsh.in
    $namespace_prefix cqlsh -2 < /tmp/$$.cqlsh.in

    rm -f /tmp/$$.cqlsh.in

    echo "USE ${keyspace};
CREATE TABLE impi (private_id text PRIMARY KEY, digest_ha1 text, digest_realm text, digest_qop text, known_preferred boolean) WITH read_repair_chance = 1.0;
CREATE TABLE impu (public_id text PRIMARY KEY, ims_subscription_xml text, is_registered boolean) WITH read_repair_chance = 1.0;" >> /tmp/$$.cqlsh.in

    $namespace_prefix cqlsh -2 < /tmp/$$.cqlsh.in
    rm -f /tmp/$$.cqlsh.in
fi

echo "USE ${keyspace};
      ALTER TABLE impu ADD primary_ccf text;
      ALTER TABLE impu ADD secondary_ccf text;
      ALTER TABLE impu ADD primary_ecf text;
      ALTER TABLE impu ADD secondary_ecf text;" | $namespace_prefix cqlsh -2

if [[ ! -e /var/lib/cassandra/data/${keyspace}/impi_mapping ]];
then
    echo "USE ${keyspace};
      CREATE TABLE impi_mapping (private_id text PRIMARY KEY, unused text) WITH read_repair_chance = 1.0;" | $namespace_prefix cqlsh -2
fi

yaml=/etc/cassandra/cassandra.yaml
let max_replicas=2
cassandra_nodes=($(cat ${yaml} | grep "[^#]*seeds:" |
        sed -e 's#^.*seeds:[ 	]*"\([^"]*\).*$#\1#'|sed -e 's#,# #g'))
let "num_replicas=${#cassandra_nodes[@]}"
if [ $num_replicas -gt $max_replicas ]; then
    let "num_replicas=$max_replicas"
fi
snitch=$(grep "^[[:space:]]*endpoint_snitch:" ${yaml}|
    sed -e 's#endpoint_snitch:[[:space:]]*\([^[:space:]]*\).*#\1#')

if [ "$snitch" == "PropertyFileSnitch" ]; then
    IFS=$'\r\n' GLOBIGNORE='*' :; topology=(
        $(egrep "^[[:space:]]*([0-9]+[.][0-9]+[.][0-9]+[.][0-9]+|[0-9a-f\\:]+)[[:space:]]*=" /etc/cassandra/cassandra-topology.properties)
    )

    dcs=()
    declare -A dc_num_nodes
    for loc in "${topology[@]}"; do
        loc=( 
            $(echo "$loc"|
                sed -e 's#\\##g'|sed -e 's#^[[:space:]]*\([^[:space:]]*\)[^=]*=[[:space:]]*\([^:]*\):[[:space:]]*\([^[:space:]]*\).*$#\1 \2 \3#' ) 
        )
        
        dc=${loc[1]}
        rack=${loc[2]}
        (for e in ${dcs[@]}; do [[ "$e" == "${dc}" ]] && exit 0; done; exit 1)
        if [ $? -ne 0 ]; then
            dcs=( ${dcs[@]} $dc )
        fi
        if [ -z ${dc_num_nodes[$dc]} ]; then
            let "dc_num_nodes[$dc]=0"
        fi
        let "dc_num_nodes[$dc]=${dc_num_nodes[$dc]} + 1"
    done

    # Generate cqlsh input for creating the keyspace
    (
        printf "ALTER KEYSPACE \"${keyspace}\" WITH REPLICATION = { 'class' : 'NetworkTopologyStrategy', "
        let "i=0"
        while [ $i -lt ${#dcs[@]} ]; do
            let "num_replicas=${dc_num_nodes[${dcs[$i]}]}"
            if [ $num_replicas -gt $max_replicas ]; then
                let "num_replicas=$max_replicas"
            fi
            if [ $i -ne 0 ]; then
                printf ", "
            fi
            printf "'${dcs[$i]}' : $num_replicas"
            let "i=$i + 1"
        done
        printf " };\n"
    ) > /tmp/$$.cqlsh.in
    $namespace_prefix cqlsh -3 < /tmp/$$.cqlsh.in

    rm -f /tmp/$$.cqlsh.in
else
    printf "
ALTER KEYSPACE \"${keyspace}\" WITH REPLICATION =
  { 'class' : 'SimpleStrategy', 'replication_factor' : $num_replicas };" > /tmp/$$.cqlsh.in
    $namespace_prefix cqlsh -3 < /tmp/$$.cqlsh.in

    rm -f /tmp/$$.cqlsh.in
fi
