from metaswitch.clearwater.cluster_manager.plugin_base import \
    SynchroniserPluginBase
from metaswitch.clearwater.cluster_manager.plugin_utils import \
    join_cassandra_cluster, leave_cassandra_cluster, run_command
from metaswitch.clearwater.cluster_manager.alarms import issue_alarm
from metaswitch.clearwater.cluster_manager import pdlogs, constants
import logging

_log = logging.getLogger("homestead_cassandra_plugin")


class HomesteadCassandraPlugin(SynchroniserPluginBase):
    def __init__(self, params):
        self._ip = params.ip
        self._local_site = params.local_site
        self._sig_namespace = params.signaling_namespace
        _log.debug("Raising Cassandra not-clustered alarm")
        issue_alarm(constants.RAISE_CASSANDRA_NOT_YET_CLUSTERED)
        pdlogs.NOT_YET_CLUSTERED_ALARM.log(cluster_desc=self.cluster_description())

    def key(self):
        return "/clearwater/homestead/clustering/cassandra"

    def cluster_description(self):
        return "Cassandra cluster"

    def on_cluster_changing(self, cluster_view):
        pass

    def on_joining_cluster(self, cluster_view):
        join_cassandra_cluster(cluster_view,
                               "/etc/cassandra/cassandra.yaml",
                               "/etc/cassandra/cassandra-rackdc.properties",
                               self._ip,
                               self._local_site)

        if (self._ip == sorted(cluster_view.keys())[0]):
            _log.debug("Adding Homestead schema")
            run_command("/usr/share/clearwater/cassandra-schemas/homestead_cache.sh")
            run_command("/usr/share/clearwater/cassandra-schemas/homestead_provisioning.sh")

        _log.debug("Clearing Cassandra not-clustered alarm")
        issue_alarm(constants.CLEAR_CASSANDRA_NOT_YET_CLUSTERED)

    def on_new_cluster_config_ready(self, cluster_view):
        pass

    def on_stable_cluster(self, cluster_view):
        pass

    def on_leaving_cluster(self, cluster_view):
        issue_alarm(constants.RAISE_CASSANDRA_NOT_YET_DECOMMISSIONED)
        leave_cassandra_cluster(self._sig_namespace)
        issue_alarm(constants.CLEAR_CASSANDRA_NOT_YET_DECOMMISSIONED)
        pass

    def files(self):
        return ["/etc/cassandra/cassandra.yaml"]


def load_as_plugin(params):
    return HomesteadCassandraPlugin(params)
