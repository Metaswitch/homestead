/**
 * @file main.cpp main function for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <getopt.h>
#include <signal.h>
#include <semaphore.h>
#include <boost/filesystem.hpp>

#include "accesslogger.h"
#include "log.h"
#include "statisticsmanager.h"
#include "load_monitor.h"
#include "diameterstack.h"
#include "diameter_handlers.h"
#include "diameter_hss_connection.h"
#include "httpstack.h"
#include "http_handlers.h"
#include "logger.h"
#include "memcached_cache.h"
#include "memcachedstore.h"
#include "hsprov_hss_connection.h"
#include "hsprov_store.h"
#include "hss_cache_processor.h"
#include "saslogger.h"
#include "sas.h"
#include "sasevent.h"
#include "saslogger.h"
#include "sproutconnection.h"
#include "diameterresolver.h"
#include "realmmanager.h"
#include "homestead_pd_definitions.h"
#include "alarm.h"
#include "communicationmonitor.h"
#include "exception_handler.h"
#include "homestead_alarmdefinition.h"
#include "snmp_counter_table.h"
#include "snmp_cx_counter_table.h"
#include "snmp_agent.h"
#include "namespace_hop.h"
#include "utils.h"

struct options
{
  std::string local_host;
  std::string home_domain;
  std::string diameter_conf;
  std::vector<std::string> dns_servers;
  std::string http_address;
  unsigned short http_port;
  int http_threads;
  std::string cassandra;
  std::vector<std::string> impu_stores;
  std::string local_site_name;
  std::string dest_realm;
  std::string dest_host;
  std::string force_hss_peer;
  int max_peers;
  std::string server_name;
  int impu_cache_ttl;
  int hss_reregistration_time;
  int reg_max_expires;
  std::string sprout_http_name;
  std::string scheme_unknown;
  std::string scheme_digest;
  std::string scheme_akav1;
  std::string scheme_akav2;
  bool access_log_enabled;
  std::string access_log_directory;
  bool log_to_file;
  std::string log_directory;
  int log_level;
  int cache_threads;
  int cassandra_threads;
  std::string sas_server;
  std::string sas_system_name;
  int diameter_timeout_ms;
  int target_latency_us;
  int max_tokens;
  float init_token_rate;
  float min_token_rate;
  int exception_max_ttl;
  int astaire_blacklist_duration;
  int http_blacklist_duration;
  int diameter_blacklist_duration;
  int dns_timeout;
  std::string pidfile;
  bool daemon;
  bool sas_signaling_if;
  bool request_shared_ifcs;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  SCHEME_UNKNOWN = 128, // start after the ASCII set ends to avoid conflicts
  SCHEME_DIGEST,
  SCHEME_AKAV1,
  SCHEME_AKAV2,
  SAS_CONFIG,
  DIAMETER_TIMEOUT_MS,
  ALARMS_ENABLED,
  DNS_SERVER,
  TARGET_LATENCY_US,
  MAX_TOKENS,
  INIT_TOKEN_RATE,
  MIN_TOKEN_RATE,
  EXCEPTION_MAX_TTL,
  ASTAIRE_BLACKLIST_DURATION,
  HTTP_BLACKLIST_DURATION,
  DIAMETER_BLACKLIST_DURATION,
  DNS_TIMEOUT,
  FORCE_HSS_PEER,
  LOCAL_SITE_NAME,
  SAS_USE_SIGNALING_IF,
  REQUEST_SHARED_IFCS,
  PIDFILE,
  DAEMON,
  REG_MAX_EXPIRES,
  CASSANDRA_THREADS
};

const static struct option long_opt[] =
{
  {"localhost",                   required_argument, NULL, 'l'},
  {"home-domain",                 required_argument, NULL, 'r'},
  {"diameter-conf",               required_argument, NULL, 'c'},
  {"dns-server",                  required_argument, NULL, DNS_SERVER},
  {"http",                        required_argument, NULL, 'H'},
  {"http-threads",                required_argument, NULL, 't'},
  {"cache-threads",               required_argument, NULL, 'u'},
  {"cassandra-threads",           required_argument, NULL, CASSANDRA_THREADS},
  {"cassandra",                   required_argument, NULL, 'S'},
  {"local-site-name",             required_argument, NULL, LOCAL_SITE_NAME},
  {"impu-stores",                 required_argument, NULL, 'M'},
  {"dest-realm",                  required_argument, NULL, 'D'},
  {"dest-host",                   required_argument, NULL, 'd'},
  {"hss-peer",                    required_argument, NULL, FORCE_HSS_PEER},
  {"max-peers",                   required_argument, NULL, 'p'},
  {"server-name",                 required_argument, NULL, 's'},
  {"impu-cache-ttl",              required_argument, NULL, 'i'},
  {"hss-reregistration-time",     required_argument, NULL, 'I'},
  {"reg-max-expires",             required_argument, NULL, REG_MAX_EXPIRES},
  {"sprout-http-name",            required_argument, NULL, 'j'},
  {"scheme-unknown",              required_argument, NULL, SCHEME_UNKNOWN},
  {"scheme-digest",               required_argument, NULL, SCHEME_DIGEST},
  {"scheme-akav1",                required_argument, NULL, SCHEME_AKAV1},
  {"scheme-akav2",                required_argument, NULL, SCHEME_AKAV2},
  {"access-log",                  required_argument, NULL, 'a'},
  {"sas",                         required_argument, NULL, SAS_CONFIG},
  {"diameter-timeout-ms",         required_argument, NULL, DIAMETER_TIMEOUT_MS},
  {"log-file",                    required_argument, NULL, 'F'},
  {"log-level",                   required_argument, NULL, 'L'},
  {"help",                        no_argument,       NULL, 'h'},
  {"target-latency-us",           required_argument, NULL, TARGET_LATENCY_US},
  {"max-tokens",                  required_argument, NULL, MAX_TOKENS},
  {"init-token-rate",             required_argument, NULL, INIT_TOKEN_RATE},
  {"min-token-rate",              required_argument, NULL, MIN_TOKEN_RATE},
  {"exception-max-ttl",           required_argument, NULL, EXCEPTION_MAX_TTL},
  {"astaire-blacklist-duration",  required_argument, NULL, ASTAIRE_BLACKLIST_DURATION},
  {"http-blacklist-duration",     required_argument, NULL, HTTP_BLACKLIST_DURATION},
  {"diameter-blacklist-duration", required_argument, NULL, DIAMETER_BLACKLIST_DURATION},
  {"dns-timeout",                 required_argument, NULL, DNS_TIMEOUT},
  {"pidfile",                     required_argument, NULL, PIDFILE},
  {"daemon",                      no_argument,       NULL, DAEMON},
  {"sas-use-signaling-interface", no_argument,       NULL, SAS_USE_SIGNALING_IF},
  {"request-shared-ifcs",         no_argument,       NULL, REQUEST_SHARED_IFCS},
  {NULL,                          0,                 NULL, 0},
};

static std::string options_description = "l:r:c:H:t:u:S:D:d:p:s:i:I:a:F:L:h";

static const std::string HTTP_MGMT_SOCKET_PATH = "/tmp/homestead-http-mgmt-socket";
static const int NUM_HTTP_MGMT_THREADS = 5;

void usage(void)
{
  puts("Options:\n"
       "\n"
       " -l, --localhost <hostname> Specify the local hostname or IP address."
       " -r, --home-domain <name>   Specify the SIP home domain."
       " -c, --diameter-conf <file> File name for Diameter configuration\n"
       " -H, --http <address>       Set HTTP bind address (default: 0.0.0.0)\n"
       " -t, --http-threads N       Number of HTTP threads (default: 1)\n"
       " -u, --cache-threads N      Number of cache threads (default: 50)\n"
       "     --cassandra-threads N  Number of cassandra threads (default: 10)\n"
       " -S, --cassandra <address>  Set the IP address or FQDN of the Cassandra database (default: 127.0.0.1 or [::1])"
       " -M  --impu-stores <site_name>=domain[:<port>][,<site_name>=<domain>:<port>,...]\n"
       "                            Enables memcached store for IMPU cache data\n"
       "                            and specifies the location of the memcached\n"
       "                            store in each site. One of the sites must\n"
       "                            be the local site. Remote sites for\n"
       "                            geo-redundant storage are optional.\n "
       "                            (If not provided, localhost is used.)\n"
       " -D, --dest-realm <name>    Set Destination-Realm on Cx messages\n"
       " -d, --dest-host <name>     Set Destination-Host on Cx messages\n"
       "     --hss-peer <name>      IP address of HSS to connect to (rather than resolving Destination-Realm/Destination-Host)\n"
       " -p, --max-peers N          Number of peers to connect to (default: 2)\n"
       " -s, --server-name <name>   Set Server-Name on Cx messages\n"
       " -i, --impu-cache-ttl <secs>\n"
       "                            IMPU cache time-to-live in seconds (default: 0)\n"
       " -I, --hss-reregistration-time <secs>\n"
       "                            How often a RE_REGISTRATION SAR should be sent to the HSS in seconds (default: 1800)\n"
       " -j, --http-sprout-name <name>\n"
       "                            Set HTTP address to send deregistration information from RTRs\n"
       "     --local-site-name <name>\n"
       "                            The name of the local site (used in a geo-redundant deployment)\n"
       "     --scheme-unknown <string>\n"
       "                            String to use to specify unknown SIP-Auth-Scheme (default: Unknown)\n"
       "     --scheme-digest <string>\n"
       "                            String to use to specify digest SIP-Auth-Scheme (default: SIP Digest)\n"
       "     --scheme-aka <string>\n"
       "                            String to use to specify AKA SIP-Auth-Scheme (default: Digest-AKAv1-MD5)\n"
       " -a, --access-log <directory>\n"
       "                            Generate access logs in specified directory\n"
       "     --sas <hostname>,<system name>\n"
       "                            Use specified host as Service Assurance Server and specified\n"
       "                            system name to identify this system to SAS.  If this option isn't\n"
       "                            specified SAS is disabled\n"
       "     --diameter-timeout-ms  Length of time (in ms) before timing out a Diameter request to the HSS\n"
       "     --target-latency-us <usecs>\n"
       "                            Target latency above which throttling applies (default: 100000)\n"
       "     --max-tokens N         Maximum number of tokens allowed in the token bucket (used by\n"
       "                            the throttling code (default: 1000))\n"
       "     --init-token-rate N    Initial token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 100.0))\n"
       "     --min-token-rate N     Minimum token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 10.0))\n"
       "     --dns-server <server>[,<server2>,<server3>]\n"
       "                            IP addresses of the DNS servers to use (defaults to 127.0.0.1)\n"
       "     --exception-max-ttl <secs>\n"
       "                            The maximum time before the process exits if it hits an exception.\n"
       "                            The actual time is randomised.\n"
       "     --sas-use-signaling-interface\n"
       "                            Whether SAS traffic is to be dispatched over the signaling network\n"
       "                            interface rather than the default management interface\n"
       "     --http-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist an HTTP peer when it is unresponsive.\n"
       "     --diameter-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist a Diameter peer when it is unresponsive.\n"
       "     --dns-timeout <milliseconds>\n"
       "                            The amount of time to wait for a DNS response (default: 200)n"
       "     --request-shared-ifcs  Indicate support for Shared IFC sets in the Supported-Features AVP.\n"
       " -F, --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " -L, --log-level N          Set log level to N (default: 4)\n"
       "     --daemon               Run as daemon\n"
       "     --pidfile=<filename>   Write pidfile\n"
       " -h, --help                 Show this help screen\n");
}

int init_logging_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'F':
      options.log_to_file = true;
      options.log_directory = std::string(optarg);
      break;

    case 'L':
      options.log_level = atoi(optarg);
      break;

    case DAEMON:
      options.daemon = true;
      break;

    default:
      // Ignore other options at this point
      break;
    }
  }

  return 0;
}

int init_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'l':
      TRC_INFO("Local host: %s", optarg);
      options.local_host = std::string(optarg);
      break;

    case 'r':
      TRC_INFO("Home domain: %s", optarg);
      options.home_domain = std::string(optarg);
      break;

    case 'c':
      TRC_INFO("Diameter configuration file: %s", optarg);
      options.diameter_conf = std::string(optarg);
      break;

    case 'H':
      TRC_INFO("HTTP address: %s", optarg);
      options.http_address = std::string(optarg);
      break;

    case 't':
      TRC_INFO("HTTP threads: %s", optarg);
      options.http_threads = atoi(optarg);
      break;

    case 'u':
      TRC_INFO("Cache threads: %s", optarg);
      options.cache_threads = atoi(optarg);
      break;

    case CASSANDRA_THREADS:
      TRC_INFO("Cassandra threads: %s", optarg);
      options.cassandra_threads = atoi(optarg);
      break;

    case 'S':
      TRC_INFO("Cassandra host: %s", optarg);
      options.cassandra = std::string(optarg);
      break;

    case 'M':
      {
        // This option has the format
        // <site_name>=<domain>,[<site_name>=<domain>,<site_name=<domain>,...].
        // For now, just split into a vector of <site_name>=<domain> strings. We
        // need to know the local site name to parse this properly, so we'll do
        // that later.
        TRC_INFO("IMPU Stores: %s", optarg);
        std::string stores_arg = std::string(optarg);
        boost::split(options.impu_stores,
                     stores_arg,
                     boost::is_any_of(","));
      }
      break;

    case 'D':
      TRC_INFO("Destination realm: %s", optarg);
      options.dest_realm = std::string(optarg);
      break;

    case 'd':
      TRC_INFO("Destination host: %s", optarg);
      options.dest_host = std::string(optarg);
      break;

    case 'p':
      TRC_INFO("Maximum peers: %s", optarg);
      options.max_peers = atoi(optarg);
      break;

    case 's':
      TRC_INFO("Server name: %s", optarg);
      options.server_name = std::string(optarg);
      break;

    case 'i':
      TRC_INFO("IMPU cache TTL: %s", optarg);
      options.impu_cache_ttl = atoi(optarg);
      break;

    case 'I':
      TRC_INFO("HSS reregistration time: %s", optarg);
      options.hss_reregistration_time = atoi(optarg);
      break;

    case REG_MAX_EXPIRES:
      TRC_INFO("Maximum registration expiry time: %s", optarg);
      options.reg_max_expires = atoi(optarg);
      break;

    case 'j':
      TRC_INFO("Sprout HTTP name: %s", optarg);
      options.sprout_http_name = std::string(optarg);
      break;

    case SCHEME_UNKNOWN:
      TRC_INFO("Scheme unknown: %s", optarg);
      options.scheme_unknown = std::string(optarg);
      break;

    case SCHEME_DIGEST:
      TRC_INFO("Scheme digest: %s", optarg);
      options.scheme_digest = std::string(optarg);
      break;

    case SCHEME_AKAV1:
      TRC_INFO("Scheme AKAv1: %s", optarg);
      options.scheme_akav1 = std::string(optarg);
      break;

    case SCHEME_AKAV2:
      TRC_INFO("Scheme AKAv2: %s", optarg);
      options.scheme_akav2 = std::string(optarg);
      break;

    case 'a':
      TRC_INFO("Access log: %s", optarg);
      options.access_log_enabled = true;
      options.access_log_directory = std::string(optarg);
      break;

    case LOCAL_SITE_NAME:
      options.local_site_name = std::string(optarg);
      TRC_INFO("Local site name = %s", optarg);
      break;

    case ASTAIRE_BLACKLIST_DURATION:
      options.astaire_blacklist_duration = atoi(optarg);
      TRC_INFO("Astaire blacklist duration set to %d",
               options.astaire_blacklist_duration);
      break;

    case SAS_CONFIG:
      {
        std::vector<std::string> sas_options;
        Utils::split_string(std::string(optarg), ',', sas_options, 0, false);
        if (sas_options.size() == 2)
        {
          options.sas_server = sas_options[0];
          options.sas_system_name = sas_options[1];
          TRC_INFO("SAS set to %s", options.sas_server.c_str());
          TRC_INFO("System name is set to %s", options.sas_system_name.c_str());
        }
      }
      break;

    case DIAMETER_TIMEOUT_MS:
      TRC_INFO("Diameter timeout: %s", optarg);
      options.diameter_timeout_ms = atoi(optarg);
      break;

    case DNS_SERVER:
      options.dns_servers.clear();
      Utils::split_string(std::string(optarg), ',', options.dns_servers, 0, false);
      TRC_INFO("%d DNS servers passed on the command line",
               options.dns_servers.size());
      break;

    case TARGET_LATENCY_US:
      options.target_latency_us = atoi(optarg);
      if (options.target_latency_us <= 0)
      {
        TRC_ERROR("Invalid --target-latency-us option %s", optarg);
        return -1;
      }
      break;

    case MAX_TOKENS:
      options.max_tokens = atoi(optarg);
      if (options.max_tokens <= 0)
      {
        TRC_ERROR("Invalid --max-tokens option %s", optarg);
        return -1;
      }
      break;

    case INIT_TOKEN_RATE:
      options.init_token_rate = atoi(optarg);
      if (options.init_token_rate <= 0)
      {
        TRC_ERROR("Invalid --init-token-rate option %s", optarg);
        return -1;
      }
      break;

    case MIN_TOKEN_RATE:
      options.min_token_rate = atoi(optarg);
      if (options.min_token_rate <= 0)
      {
        TRC_ERROR("Invalid --min-token-rate option %s", optarg);
        return -1;
      }
      break;

    case EXCEPTION_MAX_TTL:
      options.exception_max_ttl = atoi(optarg);
      TRC_INFO("Max TTL after an exception set to %d",
               options.exception_max_ttl);
      break;

    case HTTP_BLACKLIST_DURATION:
      options.http_blacklist_duration = atoi(optarg);
      TRC_INFO("HTTP blacklist duration set to %d",
               options.http_blacklist_duration);
      break;

    case DIAMETER_BLACKLIST_DURATION:
      options.diameter_blacklist_duration = atoi(optarg);
      TRC_INFO("Diameter blacklist duration set to %d",
               options.diameter_blacklist_duration);
      break;

    case DNS_TIMEOUT:
      options.dns_timeout = atoi(optarg);
      TRC_INFO("DNS timeout set to %d", options.dns_timeout);
      break;

    case FORCE_HSS_PEER:
      options.force_hss_peer = std::string(optarg);
      break;

    case PIDFILE:
      options.pidfile = std::string(optarg);
      break;

    case SAS_USE_SIGNALING_IF:
      options.sas_signaling_if = true;
      break;

    case REQUEST_SHARED_IFCS:
      options.request_shared_ifcs = true;
      break;

    case DAEMON:
    case 'F':
    case 'L':
      // Ignore F and L - these are handled by init_logging_options
      break;

    case 'h':
      usage();
      return -1;

    default:
      CL_HOMESTEAD_INVALID_OPTION_C.log(opt);
      TRC_ERROR("Unknown option. Run with --help for options.\n");
      return -1;
    }
  }

  return 0;
}

static sem_t term_sem;
ExceptionHandler* exception_handler;

// Signal handler that triggers homestead termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void signal_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, signal_handler);

  // Log the signal, along with a backtrace.
  TRC_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  TRC_COMMIT();

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  CL_HOMESTEAD_CRASH.log(strsignal(sig));

  // Dump a core.
  abort();
}

int main(int argc, char**argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.local_host = "127.0.0.1";
  options.home_domain = "dest-realm.unknown";
  options.diameter_conf = "homestead.conf";
  options.dns_servers.push_back("127.0.0.1");
  options.http_address = "0.0.0.0";
  options.http_port = 8888;
  options.http_threads = 1;
  options.cache_threads = 50;
  options.cassandra_threads = 10;
  options.cassandra = "";
  options.dest_realm = "";
  options.dest_host = "dest-host.unknown";
  options.force_hss_peer = "";
  options.max_peers = 2;
  options.server_name = "sip:server-name.unknown";
  options.access_log_enabled = false;
  options.impu_cache_ttl = 0;
  options.hss_reregistration_time = 1800;
  options.reg_max_expires = 300;
  options.sprout_http_name = "sprout-http-name.unknown";
  options.log_to_file = false;
  options.log_level = 0;
  options.sas_server = "0.0.0.0";
  options.sas_system_name = "";
  options.diameter_timeout_ms = 200;
  options.target_latency_us = 100000;
  options.max_tokens = 1000;
  options.init_token_rate = 100.0;
  options.min_token_rate = 10.0;
  options.exception_max_ttl = 600;
  options.http_blacklist_duration = HttpResolver::DEFAULT_BLACKLIST_DURATION;
  options.diameter_blacklist_duration = DiameterResolver::DEFAULT_BLACKLIST_DURATION;
  options.dns_timeout = DnsCachedResolver::DEFAULT_TIMEOUT;
  options.pidfile = "";
  options.daemon = false;
  options.sas_signaling_if = false;
  options.request_shared_ifcs = false;

  if (init_logging_options(argc, argv, options) != 0)
  {
    return 1;
  }

  Utils::daemon_log_setup(argc,
                          argv,
                          options.daemon,
                          options.log_directory,
                          options.log_level,
                          options.log_to_file);

  // We should now have a connection to syslog so we can write the started ENT
  // log.
  CL_HOMESTEAD_STARTED.log();

  std::stringstream options_ss;
  for (int ii = 0; ii < argc; ii++)
  {
    options_ss << argv[ii];
    options_ss << " ";
  }
  std::string options_str = "Command-line options were: " + options_ss.str();

  TRC_INFO(options_str.c_str());

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  // Parse the impu-stores argument.
  std::string impu_store_location;
  std::vector<std::string> remote_impu_stores_locations;

  if (!Utils::parse_multi_site_stores_arg(options.impu_stores,
                                          options.local_site_name,
                                          "impu-store",
                                          impu_store_location,
                                          remote_impu_stores_locations))
  {
    return 1;
  }

  if (options.pidfile != "")
  {
    int rc = Utils::lock_and_write_pidfile(options.pidfile);
    if (rc == -1)
    {
      // Failure to acquire pidfile lock
      TRC_ERROR("Could not write pidfile - exiting");
      return 2;
    }
  }

  AccessLogger* access_logger = NULL;
  if (options.access_log_enabled)
  {
    TRC_STATUS("Access logging enabled to %s", options.access_log_directory.c_str());
    access_logger = new AccessLogger(options.access_log_directory);
  }

  if (options.sas_server == "0.0.0.0")
  {
    TRC_WARNING("SAS server option was invalid or not configured - SAS is disabled");
    CL_HOMESTEAD_INVALID_SAS_OPTION.log();
  }

  // Create a DNS resolver and a SIP specific resolver.
  int af = AF_INET;
  struct in6_addr dummy_addr;
  if (inet_pton(AF_INET6, options.local_host.c_str(), &dummy_addr) == 1)
  {
    TRC_DEBUG("Local host is an IPv6 address");
    af = AF_INET6;
  }

  SAS::init(options.sas_system_name,
            "homestead",
            SASEvent::CURRENT_RESOURCE_BUNDLE,
            options.sas_server,
            sas_write,
            options.sas_signaling_if ? create_connection_in_signaling_namespace
                                     : create_connection_in_management_namespace);

  // Set up the statistics (Homestead specific and Diameter)
  snmp_setup("homestead");
  StatisticsManager* stats_manager = new StatisticsManager();
  SNMP::CounterTable* realm_counter = SNMP::CounterTable::create("H_diameter_invalid_dest_realm",
                                                                 ".1.2.826.0.1.1578918.9.5.8");
  SNMP::CounterTable* host_counter = SNMP::CounterTable::create("H_diameter_invalid_dest_host",
                                                                ".1.2.826.0.1.1578918.9.5.9");
  SNMP::CxCounterTable* mar_results_table = SNMP::CxCounterTable::create("cx_mar_results",
                                                                         ".1.2.826.0.1.1578918.9.5.10");
  SNMP::CxCounterTable* sar_results_table = SNMP::CxCounterTable::create("cx_sar_results",
                                                                         ".1.2.826.0.1.1578918.9.5.11");
  SNMP::CxCounterTable* uar_results_table = SNMP::CxCounterTable::create("cx_uar_results",
                                                                         ".1.2.826.0.1.1578918.9.5.12");
  SNMP::CxCounterTable* lir_results_table = SNMP::CxCounterTable::create("cx_lir_results",
                                                                         ".1.2.826.0.1.1578918.9.5.13");
  SNMP::CxCounterTable* ppr_results_table = SNMP::CxCounterTable::create("cx_ppr_results",
                                                                         ".1.2.826.0.1.1578918.9.5.14");
  SNMP::CxCounterTable* rtr_results_table = SNMP::CxCounterTable::create("cx_rtr_results",
                                                                         ".1.2.826.0.1.1578918.9.5.15");
  // Must happen after all SNMP tables have been registered.
  init_snmp_handler_threads("homestead");

  // Create Homesteads's alarm objects. Note that the alarm identifier strings must match those
  // in the alarm definition JSON file exactly.
  AlarmManager* alarm_manager = new AlarmManager();

  CommunicationMonitor* hss_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                              "homestead",
                                                                              AlarmDef::HOMESTEAD_HSS_COMM_ERROR,
                                                                              AlarmDef::CRITICAL),
                                                                    "Homestead",
                                                                    "HSS");

  CommunicationMonitor* cassandra_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                    "homestead",
                                                                                    AlarmDef::HOMESTEAD_CASSANDRA_COMM_ERROR,
                                                                                    AlarmDef::CRITICAL),
                                                                          "Homestead",
                                                                          "Cassandra");

  CommunicationMonitor* astaire_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                  "homestead",
                                                                                  AlarmDef::HOMESTEAD_ASTAIRE_COMM_ERROR,
                                                                                  AlarmDef::CRITICAL),
                                                                        "Homestead",
                                                                        "Astaire");

  CommunicationMonitor* remote_astaire_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                         "homestead",
                                                                                         AlarmDef::HOMESTEAD_REMOTE_ASTAIRE_COMM_ERROR,
                                                                                         AlarmDef::CRITICAL),
                                                                               "Homestead",
                                                                               "remote Astaire");

  // Create an exception handler. The exception handler doesn't need
  // to quiesce the process before killing it.
  HealthChecker* hc = new HealthChecker();
  hc->start_thread();
  exception_handler = new ExceptionHandler(options.exception_max_ttl,
                                           false,
                                           hc);

  LoadMonitor* load_monitor = new LoadMonitor(options.target_latency_us,
                                              options.max_tokens,
                                              options.init_token_rate,
                                              options.min_token_rate);
  DnsCachedResolver* dns_resolver = new DnsCachedResolver(options.dns_servers,
                                                          options.dns_timeout);
  HttpResolver* http_resolver = new HttpResolver(dns_resolver,
                                                 af,
                                                 options.http_blacklist_duration);

  AstaireResolver* astaire_resolver = nullptr;
  Store* local_impu_data_store = nullptr;
  ImpuStore* local_impu_store = nullptr;
  std::vector<Store*> remote_impu_data_stores;
  std::vector<ImpuStore*> remote_impu_stores;
  MemcachedCache* memcached_cache = nullptr;

  astaire_resolver = new AstaireResolver(dns_resolver,
                                         af,
                                         options.astaire_blacklist_duration);

  if (impu_store_location != "")
  {
    TRC_STATUS("Using local impu store: %s", impu_store_location.c_str());
    local_impu_data_store = (Store*)new TopologyNeutralMemcachedStore(impu_store_location,
                                                                      astaire_resolver,
                                                                      false,
                                                                      astaire_comm_monitor);
    local_impu_store = new ImpuStore(local_impu_data_store);

    for (std::vector<std::string>::iterator it = remote_impu_stores_locations.begin();
           it != remote_impu_stores_locations.end();
           ++it)
      {
        TRC_STATUS("Using remote impu store: %s", (*it).c_str());
        Store* remote_data_store = (Store*)new TopologyNeutralMemcachedStore(*it,
                                                                             astaire_resolver,
                                                                             true,
                                                                             remote_astaire_comm_monitor);
        remote_impu_data_stores.push_back(remote_data_store);
        remote_impu_stores.push_back(new ImpuStore(remote_data_store));
      }

    memcached_cache = new MemcachedCache(local_impu_store, remote_impu_stores);
  }
  else
  {
    CL_HOMESTEAD_NO_IMPU_STORE.log();
    TRC_ERROR("No IMPU store specified");
    TRC_STATUS("Homestead is shutting down");
    exit(2);
  }

  HssCacheProcessor* cache_processor = new HssCacheProcessor(memcached_cache);
  HssCacheTask::configure_cache(cache_processor);
  bool started = cache_processor->start_threads(options.cache_threads,
                                                exception_handler,
                                                0);
  if (!started)
  {
    CL_HOMESTEAD_CACHE_INIT_FAIL.log();
    TRC_ERROR("Failed to initialize the cache");
    TRC_STATUS("Homestead is shutting down");
    exit(2);
  }

  HssCacheTask::configure_health_checker(hc);

  HttpConnection* http = new HttpConnection(options.sprout_http_name,
                                            false,
                                            http_resolver,
                                            SASEvent::HttpLogLevel::PROTOCOL,
                                            NULL);
  SproutConnection* sprout_conn = new SproutConnection(http);
  HssConnection::HssConnection* hss_conn = nullptr;
  RegistrationTerminationTask::Config* rtr_config = nullptr;
  PushProfileTask::Config* ppr_config = nullptr;
  Diameter::SpawningHandler<RegistrationTerminationTask, RegistrationTerminationTask::Config>* rtr_task = nullptr;
  Diameter::SpawningHandler<PushProfileTask, PushProfileTask::Config>* ppr_task = nullptr;
  Cx::Dictionary* dict = nullptr;
  Diameter::Stack* diameter_stack = nullptr;
  HsProvStore* hs_prov_store = nullptr;
  CassandraResolver* cassandra_resolver = nullptr;

  // We need the record to last twice the HSS Re-registration
  // time, or the max expiry of the registration, whichever one
  // is longer. We pad the expiry to avoid small timing windows.
  int record_ttl = std::max(2 * options.hss_reregistration_time,
                            options.reg_max_expires + 10);

  bool hss_configured = !(options.dest_realm.empty() && (options.dest_host.empty() || options.dest_host == "0.0.0.0"));

  // Split processing depending on whether we're using an HSS or Homestead-Prov
  if (hss_configured)
  {
    TRC_STATUS("HSS configured - using diameter connection");
    diameter_stack = Diameter::Stack::get_instance();

    try
    {
      diameter_stack->initialize();
      diameter_stack->configure(options.diameter_conf,
                                exception_handler,
                                hss_comm_monitor,
                                realm_counter,
                                host_counter);
      dict = new Cx::Dictionary();

      rtr_config = new RegistrationTerminationTask::Config(cache_processor,
                                                           dict,
                                                           sprout_conn);
      ppr_config = new PushProfileTask::Config(cache_processor, dict);

      rtr_task = new Diameter::SpawningHandler<RegistrationTerminationTask, RegistrationTerminationTask::Config>(dict, rtr_config);
      ppr_task = new Diameter::SpawningHandler<PushProfileTask, PushProfileTask::Config>(dict, ppr_config);

      diameter_stack->advertize_application(Diameter::Dictionary::Application::AUTH,
                                            dict->TGPP, dict->CX);
      diameter_stack->register_handler(dict->CX, dict->REGISTRATION_TERMINATION_REQUEST, rtr_task);
      diameter_stack->register_handler(dict->CX, dict->PUSH_PROFILE_REQUEST, ppr_task);
      diameter_stack->register_fallback_handler(dict->CX);
      diameter_stack->start();
    }
    catch (Diameter::Stack::Exception& e)
    {
      CL_HOMESTEAD_DIAMETER_INIT_FAIL.log(e._func, e._rc);
      TRC_ERROR("Failed to initialize Diameter stack - function %s, rc %d", e._func, e._rc);
      TRC_STATUS("Homestead is shutting down");
      exit(2);
    }

    hss_conn = new HssConnection::DiameterHssConnection(stats_manager,
                                                        dict,
                                                        diameter_stack,
                                                        options.dest_realm.empty() ? options.home_domain : options.dest_realm,
                                                        options.dest_host == "0.0.0.0" ? "" : options.dest_host,
                                                        options.diameter_timeout_ms);

    HssConnection::configure_cx_results_tables(mar_results_table,
                                               sar_results_table,
                                               uar_results_table,
                                               lir_results_table);
    configure_handler_cx_results_tables(ppr_results_table, rtr_results_table);
  }
  else
  {
    TRC_STATUS("No HSS configured - using Homestead-prov");

    // Use a 30s black- and gray- list duration
    cassandra_resolver = new CassandraResolver(dns_resolver,
                                               af,
                                               30,
                                               30,
                                               9160);

    // Default the cassandra hostname to the loopback IP
    if (options.cassandra == "")
    {
      if (af == AF_INET6)
      {
        options.cassandra = "[::1]";
      }
      else
      {
        options.cassandra = "127.0.0.1";
      }
    }

    hs_prov_store = HsProvStore::get_instance();
    hs_prov_store->configure_connection(options.cassandra,
                                        9160,
                                        cassandra_comm_monitor,
                                        cassandra_resolver);
    hs_prov_store->configure_workers(exception_handler,
                                     options.cassandra_threads,
                                     0);

    // Test the connection to Cassandra before starting the store.
    CassandraStore::ResultCode rc = hs_prov_store->connection_test();

    if (rc == CassandraStore::OK)
    {
      // Cassandra connection is good, so start the store.
      rc = hs_prov_store->start();
    }

    if (rc != CassandraStore::OK)
    {
      CL_HOMESTEAD_CASSANDRA_INIT_FAIL.log(rc);
      TRC_ERROR("Failed to initialize the Cassandra store with error code %d.", rc);
      TRC_STATUS("Homestead is shutting down");
      exit(2);
    }

    hss_conn = new HssConnection::HsProvHssConnection(stats_manager,
                                                      hs_prov_store,
                                                      options.server_name);
  }

  // Common setup
  HssConnection::HssConnection::configure_auth_schemes(options.scheme_digest,
                                                       options.scheme_akav1,
                                                       options.scheme_akav2);

  HssCacheTask::configure_hss_connection(hss_conn, options.server_name);

  ImpiTask::Config impi_handler_config(options.scheme_unknown,
                                       options.scheme_digest,
                                       options.scheme_akav1,
                                       options.scheme_akav2);
  ImpiRegistrationStatusTask::Config registration_status_handler_config(options.dest_realm.empty() ?
                                                                          options.home_domain :
                                                                          options.dest_realm);
  ImpuLocationInfoTask::Config location_info_handler_config;
  ImpuRegDataTask::Config impu_handler_config(hss_configured,
                                              options.hss_reregistration_time,
                                              record_ttl,
                                              options.request_shared_ifcs);

  HttpStackUtils::PingHandler ping_handler;
  HttpStackUtils::SpawningHandler<ImpiDigestTask, ImpiTask::Config> impi_digest_handler(&impi_handler_config);
  HttpStackUtils::SpawningHandler<ImpiAvTask, ImpiTask::Config> impi_av_handler(&impi_handler_config);
  HttpStackUtils::SpawningHandler<ImpiRegistrationStatusTask, ImpiRegistrationStatusTask::Config> impi_reg_status_handler(&registration_status_handler_config);
  HttpStackUtils::SpawningHandler<ImpuLocationInfoTask, ImpuLocationInfoTask::Config> impu_loc_info_handler(&location_info_handler_config);
  HttpStackUtils::SpawningHandler<ImpuRegDataTask, ImpuRegDataTask::Config> impu_reg_data_handler(&impu_handler_config);

  HttpStack* http_stack_sig = new HttpStack(options.http_threads,
                                            exception_handler,
                                            access_logger,
                                            load_monitor,
                                            stats_manager);
  try
  {
    http_stack_sig->initialize();
    http_stack_sig->bind_tcp_socket(options.http_address,
                                    options.http_port);
    http_stack_sig->register_handler("^/ping$",
                                     &ping_handler);
    http_stack_sig->register_handler("^/impi/[^/]*/digest$",
                                     &impi_digest_handler);
    http_stack_sig->register_handler("^/impi/[^/]*/av",
                                     &impi_av_handler);
    http_stack_sig->register_handler("^/impi/[^/]*/registration-status$",
                                     &impi_reg_status_handler);
    http_stack_sig->register_handler("^/impu/[^/]*/location$",
                                     &impu_loc_info_handler);
    http_stack_sig->register_handler("^/impu/[^/]*/reg-data$",
                                     &impu_reg_data_handler);
    http_stack_sig->start();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_INIT_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to initialize signaling HttpStack stack - function %s, rc %d",
              e._func, e._rc);
    TRC_STATUS("Homestead is shutting down");
    exit(2);
  }

  HttpStackUtils::SpawningHandler<ImpuReadRegDataTask, ImpuRegDataTask::Config>
    impu_read_reg_data_handler(&impu_handler_config);

  HttpStack* http_stack_mgmt = new HttpStack(NUM_HTTP_MGMT_THREADS,
                                             exception_handler,
                                             access_logger,
                                             load_monitor);
  try
  {
    http_stack_mgmt->initialize();
    http_stack_mgmt->bind_unix_socket(HTTP_MGMT_SOCKET_PATH);
    http_stack_mgmt->register_handler("^/ping$",
                                      &ping_handler);
    http_stack_mgmt->register_handler("^/impu/[^/]*/reg-data$",
                                      &impu_read_reg_data_handler);
    http_stack_mgmt->start();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_INIT_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to initialize management HttpStack stack - function %s, rc %d",
              e._func, e._rc);
    TRC_STATUS("Homestead is shutting down");
    exit(3);
  }

  DiameterResolver* diameter_resolver = NULL;
  RealmManager* realm_manager = NULL;

  if (hss_configured)
  {
    diameter_resolver = new DiameterResolver(dns_resolver,
                                             af,
                                             options.diameter_blacklist_duration);
    if (options.force_hss_peer.empty())
    {
      realm_manager = new RealmManager(diameter_stack,
                                       options.dest_realm,
                                       options.dest_host,
                                       options.max_peers,
                                       diameter_resolver);
    }
    else
    {
      realm_manager = new RealmManager(diameter_stack,
                                       "",
                                       options.force_hss_peer,
                                       options.max_peers,
                                       diameter_resolver);
    }
    realm_manager->start();
  }

  TRC_STATUS("Start-up complete - wait for termination signal");
  sem_wait(&term_sem);
  snmp_terminate("homestead");
  TRC_STATUS("Termination signal received - terminating");
  CL_HOMESTEAD_ENDED.log();

  try
  {
    http_stack_sig->stop();
    http_stack_sig->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_STOP_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to stop signaling HttpStack stack - function %s, rc %d",
              e._func, e._rc);
  }

  try
  {
    http_stack_mgmt->stop();
    http_stack_mgmt->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_STOP_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to stop management HttpStack stack - function %s, rc %d",
              e._func, e._rc);
  }

  cache_processor->stop();
  cache_processor->wait_stopped();

  if (hss_configured)
  {
    realm_manager->stop();
    delete realm_manager; realm_manager = NULL;
    delete diameter_resolver; diameter_resolver = NULL;
    delete dict; dict = NULL;
    delete ppr_config; ppr_config = NULL;
    delete rtr_config; rtr_config = NULL;
    delete ppr_task; ppr_task = NULL;
    delete rtr_task; rtr_task = NULL;

    try
    {
      diameter_stack->stop();
      diameter_stack->wait_stopped();
    }
    catch (Diameter::Stack::Exception& e)
    {
      CL_HOMESTEAD_DIAMETER_STOP_FAIL.log(e._func, e._rc);
      TRC_ERROR("Failed to stop Diameter stack - function %s, rc %d", e._func, e._rc);
    }
  }
  else
  {
    hs_prov_store->stop();
    hs_prov_store->wait_stopped();
    delete cassandra_resolver; cassandra_resolver = NULL;
  }


  delete http_resolver; http_resolver = NULL;
  delete dns_resolver; dns_resolver = NULL;
  delete sprout_conn; sprout_conn = NULL;
  delete realm_counter; realm_counter = NULL;
  delete host_counter; host_counter = NULL;
  delete stats_manager; stats_manager = NULL;
  delete mar_results_table; mar_results_table = NULL;
  delete sar_results_table; sar_results_table = NULL;
  delete uar_results_table; uar_results_table = NULL;
  delete lir_results_table; lir_results_table = NULL;
  delete ppr_results_table; ppr_results_table = NULL;
  delete rtr_results_table; rtr_results_table = NULL;

  delete http_stack_sig; http_stack_sig = NULL;
  delete http_stack_mgmt; http_stack_mgmt = NULL;
  hc->stop_thread();
  delete hc; hc = NULL;
  delete exception_handler; exception_handler = NULL;

  delete cache_processor; cache_processor = NULL;
  delete memcached_cache; memcached_cache = nullptr;
  delete load_monitor; load_monitor = NULL;

  SAS::term();

  // Delete Homestead's alarm objects
  delete astaire_comm_monitor;
  delete remote_astaire_comm_monitor;
  delete hss_comm_monitor;
  delete cassandra_comm_monitor;
  delete alarm_manager;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
