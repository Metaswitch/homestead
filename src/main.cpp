/**
 * @file main.cpp main function for homestead
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
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
#include "httpstack.h"
#include "handlers.h"
#include "logger.h"
#include "cache.h"
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
  std::string dest_realm;
  std::string dest_host;
  std::string force_hss_peer;
  int max_peers;
  std::string server_name;
  int impu_cache_ttl;
  int hss_reregistration_time;
  std::string sprout_http_name;
  std::string scheme_unknown;
  std::string scheme_digest;
  std::string scheme_aka;
  bool access_log_enabled;
  std::string access_log_directory;
  bool log_to_file;
  std::string log_directory;
  int log_level;
  int cache_threads;
  std::string sas_server;
  std::string sas_system_name;
  int diameter_timeout_ms;
  int target_latency_us;
  int max_tokens;
  float init_token_rate;
  float min_token_rate;
  int exception_max_ttl;
  int http_blacklist_duration;
  int diameter_blacklist_duration;
  std::string pidfile;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  SCHEME_UNKNOWN = 128, // start after the ASCII set ends to avoid conflicts
  SCHEME_DIGEST,
  SCHEME_AKA,
  SAS_CONFIG,
  DIAMETER_TIMEOUT_MS,
  ALARMS_ENABLED,
  DNS_SERVER,
  TARGET_LATENCY_US,
  MAX_TOKENS,
  INIT_TOKEN_RATE,
  MIN_TOKEN_RATE,
  EXCEPTION_MAX_TTL,
  HTTP_BLACKLIST_DURATION,
  DIAMETER_BLACKLIST_DURATION,
  FORCE_HSS_PEER,
  PIDFILE
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
  {"cassandra",                   required_argument, NULL, 'S'},
  {"dest-realm",                  required_argument, NULL, 'D'},
  {"dest-host",                   required_argument, NULL, 'd'},
  {"hss-peer",                    required_argument, NULL, FORCE_HSS_PEER},
  {"max-peers",                   required_argument, NULL, 'p'},
  {"server-name",                 required_argument, NULL, 's'},
  {"impu-cache-ttl",              required_argument, NULL, 'i'},
  {"hss-reregistration-time",     required_argument, NULL, 'I'},
  {"sprout-http-name",            required_argument, NULL, 'j'},
  {"scheme-unknown",              required_argument, NULL, SCHEME_UNKNOWN},
  {"scheme-digest",               required_argument, NULL, SCHEME_DIGEST},
  {"scheme-aka",                  required_argument, NULL, SCHEME_AKA},
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
  {"http-blacklist-duration",     required_argument, NULL, HTTP_BLACKLIST_DURATION},
  {"diameter-blacklist-duration", required_argument, NULL, DIAMETER_BLACKLIST_DURATION},
  {"pidfile",                     required_argument, NULL, PIDFILE},
  {NULL,                          0,                 NULL, 0},
};

static std::string options_description = "l:r:c:H:t:u:S:D:d:p:s:i:I:a:F:L:h";

void usage(void)
{
  puts("Options:\n"
       "\n"
       " -l, --localhost <hostname> Specify the local hostname or IP address."
       " -r, --home-domain <name>   Specify the SIP home domain."
       " -c, --diameter-conf <file> File name for Diameter configuration\n"
       " -H, --http <address>       Set HTTP bind address (default: 0.0.0.0)\n"
       " -t, --http-threads N       Number of HTTP threads (default: 1)\n"
       " -u, --cache-threads N      Number of cache threads (default: 10)\n"
       " -S, --cassandra <address>  Set the IP address or FQDN of the Cassandra database (default: localhost)"
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
       "                            the throttling code (default: 100))\n"
       "     --init-token-rate N    Initial token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 100.0))\n"
       "     --min-token-rate N     Minimum token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 10.0))\n"
       "     --dns-server <server>[,<server2>,<server3>]\n"
       "                            IP addresses of the DNS servers to use (defaults to 127.0.0.1)\n"
       "     --exception-max-ttl <secs>\n"
       "                            The maximum time before the process exits if it hits an exception.\n"
       "                            The actual time is randomised.\n"
       " --http-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist an HTTP peer when it is unresponsive.\n"
       " --diameter-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist a Diameter peer when it is unresponsive.\n"
       " -F, --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " -L, --log-level N          Set log level to N (default: 4)\n"
       " -d, --daemon               Run as daemon\n"
       " --pidfile=<filename>       Write pidfile\n"
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

    case 'S':
      TRC_INFO("Cassandra host: %s", optarg);
      options.cassandra = std::string(optarg);
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

    case SCHEME_AKA:
      TRC_INFO("Scheme AKA: %s", optarg);
      options.scheme_aka = std::string(optarg);
      break;

    case 'a':
      TRC_INFO("Access log: %s", optarg);
      options.access_log_enabled = true;
      options.access_log_directory = std::string(optarg);
      break;

    case SAS_CONFIG:
      {
        std::vector<std::string> sas_options;
        Utils::split_string(std::string(optarg), ',', sas_options, 0, false);
        if (sas_options.size() == 2)
        {
          options.sas_server = sas_options[0];
          options.sas_system_name = sas_options[1];
          TRC_INFO("SAS set to %s\n", options.sas_server.c_str());
          TRC_INFO("System name is set to %s\n", options.sas_system_name.c_str());
        }
        else
        {
          CL_HOMESTEAD_INVALID_SAS_OPTION.log();
          TRC_WARNING("Invalid --sas option, SAS disabled\n");
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

    case FORCE_HSS_PEER:
      options.force_hss_peer = std::string(optarg);
      break;

    case PIDFILE:
      options.pidfile = std::string(optarg);
      break;

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
  options.cache_threads = 10;
  options.cassandra = "localhost";
  options.dest_realm = "";
  options.dest_host = "dest-host.unknown";
  options.force_hss_peer = "";
  options.max_peers = 2;
  options.server_name = "sip:server-name.unknown";
  options.scheme_unknown = "Unknown";
  options.scheme_digest = "SIP Digest";
  options.scheme_aka = "Digest-AKAv1-MD5";
  options.access_log_enabled = false;
  options.impu_cache_ttl = 0;
  options.hss_reregistration_time = 1800;
  options.sprout_http_name = "sprout-http-name.unknown";
  options.log_to_file = false;
  options.log_level = 0;
  options.sas_server = "0.0.0.0";
  options.sas_system_name = "";
  options.diameter_timeout_ms = 200;
  options.target_latency_us = 100000;
  options.max_tokens = 100;
  options.init_token_rate = 100.0;
  options.min_token_rate = 10.0;
  options.exception_max_ttl = 600;
  options.http_blacklist_duration = HttpResolver::DEFAULT_BLACKLIST_DURATION;
  options.diameter_blacklist_duration = DiameterResolver::DEFAULT_BLACKLIST_DURATION;
  options.pidfile = "";

  // Initialise ENT logging before making "Started" log
  PDLogStatic::init(argv[0]);

  CL_HOMESTEAD_STARTED.log();

  if (init_logging_options(argc, argv, options) != 0)
  {
    return 1;
  }

  Log::setLoggingLevel(options.log_level);

  if ((options.log_to_file) && (options.log_directory != ""))
  {
    // Work out the program name from argv[0], stripping anything before the final slash.
    char* prog_name = argv[0];
    char* slash_ptr = rindex(argv[0], '/');
    if (slash_ptr != NULL)
    {
      prog_name = slash_ptr + 1;
    }
    Log::setLogger(new Logger(options.log_directory, prog_name));
  }

  TRC_STATUS("Log level set to %d", options.log_level);

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
            create_connection_in_management_namespace);

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

  configure_cx_results_tables(mar_results_table,
                             sar_results_table,
                             uar_results_table,
                             lir_results_table,
                             ppr_results_table,
                             rtr_results_table);

  // Create Homesteads's alarm objects. Note that the alarm identifier strings must match those
  // in the alarm definition JSON file exactly.

  CommunicationMonitor* hss_comm_monitor = new CommunicationMonitor(new Alarm("homestead",
                                                                              AlarmDef::HOMESTEAD_HSS_COMM_ERROR,
                                                                              AlarmDef::CRITICAL),
                                                                    "Homestead",
                                                                    "HSS");

  CommunicationMonitor* cassandra_comm_monitor = new CommunicationMonitor(new Alarm("homestead",
                                                                                    AlarmDef::HOMESTEAD_CASSANDRA_COMM_ERROR,
                                                                                    AlarmDef::CRITICAL),
                                                                          "Homestead",
                                                                          "Cassandra");

  // Start the alarm request agent
  AlarmReqAgent::get_instance().start();

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
  DnsCachedResolver* dns_resolver = new DnsCachedResolver(options.dns_servers);
  HttpResolver* http_resolver = new HttpResolver(dns_resolver,
                                                 af,
                                                 options.http_blacklist_duration);

  Cache* cache = Cache::get_instance();
  cache->configure_connection(options.cassandra,
                              9160,
                              cassandra_comm_monitor);
  cache->configure_workers(exception_handler,
                           options.cache_threads,
                           0);

  // Test the connection to Cassandra before starting the store.
  CassandraStore::ResultCode rc = cache->connection_test();

  if (rc == CassandraStore::OK)
  {
    // Cassandra connection is good, so start the store.
    rc = cache->start();
  }

  if (rc != CassandraStore::OK)
  {
    CL_HOMESTEAD_CASSANDRA_CACHE_INIT_FAIL.log(rc);
    TRC_ERROR("Failed to initialize the Cassandra cache with error code %d.", rc);
    TRC_STATUS("Homestead is shutting down");
    exit(2);
  }

  HttpConnection* http = new HttpConnection(options.sprout_http_name,
                                            false,
                                            http_resolver,
                                            SASEvent::HttpLogLevel::PROTOCOL,
                                            NULL);
  SproutConnection* sprout_conn = new SproutConnection(http);

  RegistrationTerminationTask::Config* rtr_config = NULL;
  PushProfileTask::Config* ppr_config = NULL;
  Diameter::SpawningHandler<RegistrationTerminationTask, RegistrationTerminationTask::Config>* rtr_task = NULL;
  Diameter::SpawningHandler<PushProfileTask, PushProfileTask::Config>* ppr_task = NULL;
  Cx::Dictionary* dict = NULL;

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();

  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf,
                              exception_handler,
                              hss_comm_monitor,
                              realm_counter,
                              host_counter);
    dict = new Cx::Dictionary();

    rtr_config = new RegistrationTerminationTask::Config(cache, dict, sprout_conn, options.hss_reregistration_time);
    ppr_config = new PushProfileTask::Config(cache, dict, options.impu_cache_ttl, options.hss_reregistration_time);
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

  HttpStack* http_stack = HttpStack::get_instance();
  HssCacheTask::configure_diameter(diameter_stack,
                                   options.dest_realm.empty() ? options.home_domain : options.dest_realm,
                                   options.dest_host == "0.0.0.0" ? "" : options.dest_host,
                                   options.server_name,
                                   dict);
  HssCacheTask::configure_cache(cache);
  HssCacheTask::configure_health_checker(hc);
  HssCacheTask::configure_stats(stats_manager);

  // We should only query the cache for AV information if there is no HSS.  If there is an HSS, we
  // should always hit it.  If there is not, the AV information must have been provisioned in the
  // "cache" (which becomes persistent).
  bool hss_configured = !(options.dest_realm.empty() && (options.dest_host.empty() || options.dest_host == "0.0.0.0"));

  ImpiTask::Config impi_handler_config(hss_configured,
                                       options.impu_cache_ttl,
                                       options.scheme_unknown,
                                       options.scheme_digest,
                                       options.scheme_aka,
                                       options.diameter_timeout_ms);
  ImpiRegistrationStatusTask::Config registration_status_handler_config(hss_configured, options.diameter_timeout_ms);
  ImpuLocationInfoTask::Config location_info_handler_config(hss_configured, options.diameter_timeout_ms);
  ImpuRegDataTask::Config impu_handler_config(hss_configured, options.hss_reregistration_time, options.diameter_timeout_ms);
  ImpuIMSSubscriptionTask::Config impu_handler_config_old(hss_configured, options.hss_reregistration_time, options.diameter_timeout_ms);

  HttpStackUtils::PingHandler ping_handler;
  HttpStackUtils::SpawningHandler<ImpiDigestTask, ImpiTask::Config> impi_digest_handler(&impi_handler_config);
  HttpStackUtils::SpawningHandler<ImpiAvTask, ImpiTask::Config> impi_av_handler(&impi_handler_config);
  HttpStackUtils::SpawningHandler<ImpiRegistrationStatusTask, ImpiRegistrationStatusTask::Config> impi_reg_status_handler(&registration_status_handler_config);
  HttpStackUtils::SpawningHandler<ImpuLocationInfoTask, ImpuLocationInfoTask::Config> impu_loc_info_handler(&location_info_handler_config);
  HttpStackUtils::SpawningHandler<ImpuRegDataTask, ImpuRegDataTask::Config> impu_reg_data_handler(&impu_handler_config);
  HttpStackUtils::SpawningHandler<ImpuIMSSubscriptionTask, ImpuIMSSubscriptionTask::Config> impu_ims_sub_handler(&impu_handler_config_old);

  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address,
                          options.http_port,
                          options.http_threads,
                          exception_handler,
                          access_logger,
                          load_monitor,
                          stats_manager);
    http_stack->register_handler("^/ping$",
                                    &ping_handler);
    http_stack->register_handler("^/impi/[^/]*/digest$",
                                    &impi_digest_handler);
    http_stack->register_handler("^/impi/[^/]*/av",
                                    &impi_av_handler);
    http_stack->register_handler("^/impi/[^/]*/registration-status$",
                                    &impi_reg_status_handler);
    http_stack->register_handler("^/impu/[^/]*/location$",
                                    &impu_loc_info_handler);
    http_stack->register_handler("^/impu/[^/]*/reg-data$",
                                    &impu_reg_data_handler);
    http_stack->register_handler("^/impu/",
                                    &impu_ims_sub_handler);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_INIT_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to initialize HttpStack stack - function %s, rc %d", e._func, e._rc);
    TRC_STATUS("Homestead is shutting down");
    exit(2);
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
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    CL_HOMESTEAD_HTTP_STOP_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to stop HttpStack stack - function %s, rc %d", e._func, e._rc);
  }

  cache->stop();
  cache->wait_stopped();

  if (hss_configured)
  {
    realm_manager->stop();
    delete realm_manager; realm_manager = NULL;
    delete diameter_resolver; diameter_resolver = NULL;
    delete dns_resolver; dns_resolver = NULL;
  }

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
  delete dict; dict = NULL;
  delete ppr_config; ppr_config = NULL;
  delete rtr_config; rtr_config = NULL;
  delete ppr_task; ppr_task = NULL;
  delete rtr_task; rtr_task = NULL;

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

  hc->stop_thread();
  delete hc; hc = NULL;
  delete exception_handler; exception_handler = NULL;

  delete load_monitor; load_monitor = NULL;

  SAS::term();

  // Stop the alarm request agent
  AlarmReqAgent::get_instance().stop();

  // Delete Homestead's alarm objects
  delete hss_comm_monitor;
  delete cassandra_comm_monitor;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
