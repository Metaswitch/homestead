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

struct options
{
  std::string local_host;
  std::string home_domain;
  std::string diameter_conf;
  std::string http_address;
  unsigned short http_port;
  int http_threads;
  std::string cassandra;
  std::string dest_realm;
  std::string dest_host;
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
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " -l, --localhost <hostname> Specify the local hostname or IP address."
       " -r, --home-domain <name>   Specify the SIP home domain."
       " -c, --diameter-conf <file> File name for Diameter configuration\n"
       " -Y, --target-latency-us <usecs>\n"
       "                            Target latency above which throttling applies (default: 100000)\n"
       " -H, --http <address>       Set HTTP bind address (default: 0.0.0.0)\n"
       " -t, --http-threads N       Number of HTTP threads (default: 1)\n"
       " -u, --cache-threads N      Number of cache threads (default: 10)\n"
       " -S, --cassandra <address>  Set the IP address or FQDN of the Cassandra database (default: localhost)"
       " -D, --dest-realm <name>    Set Destination-Realm on Cx messages\n"
       " -d, --dest-host <name>     Set Destination-Host on Cx messages\n"
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
       " -F, --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " -L, --log-level N          Set log level to N (default: 4)\n"
       " -d, --daemon               Run as daemon\n"
       " -h, --help                 Show this help screen\n");
}

// Enum for option types not assigned short-forms
enum OptionTypes
{
  SCHEME_UNKNOWN = 128, // start after the ASCII set ends to avoid conflicts
  SCHEME_DIGEST,
  SCHEME_AKA,
  SAS_CONFIG,
  DIAMETER_TIMEOUT_MS
};

int init_options(int argc, char**argv, struct options& options)
{
  struct option long_opt[] =
  {
    {"localhost",               required_argument, NULL, 'l'},
    {"home-domain",             required_argument, NULL, 'r'},
    {"diameter-conf",           required_argument, NULL, 'c'},
    {"target-latency-us",       required_argument, NULL, 'Y'},
    {"http",                    required_argument, NULL, 'H'},
    {"http-threads",            required_argument, NULL, 't'},
    {"cache-threads",           required_argument, NULL, 'u'},
    {"cassandra",               required_argument, NULL, 'S'},
    {"dest-realm",              required_argument, NULL, 'D'},
    {"dest-host",               required_argument, NULL, 'd'},
    {"max-peers",               required_argument, NULL, 'p'},
    {"server-name",             required_argument, NULL, 's'},
    {"impu-cache-ttl",          required_argument, NULL, 'i'},
    {"hss-reregistration-time", required_argument, NULL, 'I'},
    {"sprout-http-name",        required_argument, NULL, 'j'},
    {"scheme-unknown",          required_argument, NULL, SCHEME_UNKNOWN},
    {"scheme-digest",           required_argument, NULL, SCHEME_DIGEST},
    {"scheme-aka",              required_argument, NULL, SCHEME_AKA},
    {"access-log",              required_argument, NULL, 'a'},
    {"sas",                     required_argument, NULL, SAS_CONFIG},
    {"diameter-timeout-ms",     required_argument, NULL, DIAMETER_TIMEOUT_MS},
    {"log-file",                required_argument, NULL, 'F'},
    {"log-level",               required_argument, NULL, 'L'},
    {"help",                    no_argument,       NULL, 'h'},
    {NULL,                      0,                 NULL, 0},
  };

  int opt;
  int long_opt_ind;
  while ((opt = getopt_long(argc, argv, "l:r:c:H:t:u:S:D:d:p:s:i:I:a:F:L:h:Y", long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'l':
      options.local_host = std::string(optarg);
      break;

    case 'r':
      options.home_domain = std::string(optarg);
      break;

    case 'c':
      options.diameter_conf = std::string(optarg);
      break;

    case 'Y':
      options.target_latency_us = atoi(optarg);
      if (options.target_latency_us <= 0)
      {
        fprintf(stdout, "Invalid --target-latency-us option %s\n", optarg);
        return -1;
      }
      break;

    case 'H':
      options.http_address = std::string(optarg);
      break;

    case 't':
      options.http_threads = atoi(optarg);
      break;

    case 'u':
      options.cache_threads = atoi(optarg);
      break;

    case 'S':
      options.cassandra = std::string(optarg);
      break;

    case 'D':
      options.dest_realm = std::string(optarg);
      break;

    case 'd':
      options.dest_host = std::string(optarg);
      break;

    case 'p':
      options.max_peers = atoi(optarg);
      break;

    case 's':
      options.server_name = std::string(optarg);
      break;

    case 'i':
      options.impu_cache_ttl = atoi(optarg);
      break;

    case 'I':
      options.hss_reregistration_time = atoi(optarg);
      break;

    case 'j':
      options.sprout_http_name = std::string(optarg);
      break;

    case SCHEME_UNKNOWN:
      options.scheme_unknown = std::string(optarg);
      break;

    case SCHEME_DIGEST:
      options.scheme_digest = std::string(optarg);
      break;

    case SCHEME_AKA:
      options.scheme_aka = std::string(optarg);
      break;

    case 'a':
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
          fprintf(stdout, "SAS set to %s\n", options.sas_server.c_str());
          fprintf(stdout, "System name is set to %s\n", options.sas_system_name.c_str());
        }
        else
        {
          fprintf(stdout, "Invalid --sas option, SAS disabled\n");
        }
      }
      break;

    case DIAMETER_TIMEOUT_MS:
      options.diameter_timeout_ms = atoi(optarg);
      break;

    case 'F':
      options.log_to_file = true;
      options.log_directory = std::string(optarg);
      break;

    case 'L':
      options.log_level = atoi(optarg);
      break;

    case 'h':
      usage();
      return -1;

    default:
      fprintf(stdout, "Unknown option.  Run with --help for options.\n");
      return -1;
    }
  }

  return 0;
}

static sem_t term_sem;

// Signal handler that triggers homestead termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void exception_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);

  // Log the signal, along with a backtrace.
  LOG_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  LOG_COMMIT();

  // Dump a core.
  abort();
}

int main(int argc, char**argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, exception_handler);
  signal(SIGSEGV, exception_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.local_host = "127.0.0.1";
  options.home_domain = "dest-realm.unknown";
  options.diameter_conf = "homestead.conf";
  options.http_address = "0.0.0.0";
  options.http_port = 8888;
  options.http_threads = 1;
  options.cache_threads = 10;
  options.cassandra = "localhost";
  options.dest_realm = "";
  options.dest_host = "dest-host.unknown";
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

  if (init_options(argc, argv, options) != 0)
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

  AccessLogger* access_logger = NULL;
  if (options.access_log_enabled)
  {
    LOG_STATUS("Access logging enabled to %s", options.access_log_directory.c_str());
    access_logger = new AccessLogger(options.access_log_directory);
  }

  LOG_STATUS("Log level set to %d", options.log_level);

  // Create a DNS resolver and a SIP specific resolver.
  int af = AF_INET;
  struct in6_addr dummy_addr;
  if (inet_pton(AF_INET6, options.local_host.c_str(), &dummy_addr) == 1)
  {
    LOG_DEBUG("Local host is an IPv6 address");
    af = AF_INET6;
  }

  SAS::init(options.sas_system_name,
            "homestead",
            SASEvent::CURRENT_RESOURCE_BUNDLE,
            options.sas_server,
            sas_write);

  StatisticsManager* stats_manager = new StatisticsManager();

  LoadMonitor* load_monitor = new LoadMonitor(options.target_latency_us, // Initial target latency (us).
                                              20,                        // Maximum token bucket size.
                                              10.0,                      // Initial token fill rate (per sec).
                                              10.0);                     // Minimum token fill rate (per sec).

  DnsCachedResolver* dns_resolver = new DnsCachedResolver("127.0.0.1");
  HttpResolver* http_resolver = new HttpResolver(dns_resolver, af);

  Cache* cache = Cache::get_instance();
  cache->initialize();
  cache->configure(options.cassandra, 9160, options.cache_threads);
  Cache::ResultCode rc = cache->start();

  if (rc != Cache::OK)
  {
    LOG_ERROR("Failed to initialize cache - rc %d", rc);
    exit(2);
  }

  HttpConnection* http = new HttpConnection(options.sprout_http_name,
                                            http_resolver,
                                            false,
                                            SASEvent::HttpLogLevel::PROTOCOL);
  SproutConnection* sprout_conn = new SproutConnection(http);

  RegistrationTerminationHandler::Config* rtr_config = NULL;
  PushProfileHandler::Config* ppr_config = NULL;
  Diameter::SpawningController<RegistrationTerminationHandler, RegistrationTerminationHandler::Config>* rtr_controller = NULL;
  Diameter::SpawningController<PushProfileHandler, PushProfileHandler::Config>* ppr_controller = NULL;
  Cx::Dictionary* dict = NULL;

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();

  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf);
    dict = new Cx::Dictionary();

    rtr_config = new RegistrationTerminationHandler::Config(cache, dict, sprout_conn, options.hss_reregistration_time);
    ppr_config = new PushProfileHandler::Config(cache, dict, options.impu_cache_ttl, options.hss_reregistration_time);
    rtr_controller = new Diameter::SpawningController<RegistrationTerminationHandler, RegistrationTerminationHandler::Config>(dict, rtr_config);
    ppr_controller = new Diameter::SpawningController<PushProfileHandler, PushProfileHandler::Config>(dict, ppr_config);

    diameter_stack->advertize_application(Diameter::Dictionary::Application::AUTH,
                                          dict->TGPP, dict->CX);
    diameter_stack->register_controller(dict->CX, dict->REGISTRATION_TERMINATION_REQUEST, rtr_controller);
    diameter_stack->register_controller(dict->CX, dict->PUSH_PROFILE_REQUEST, ppr_controller);
    diameter_stack->register_fallback_controller(dict->CX);
    diameter_stack->start();
  }
  catch (Diameter::Stack::Exception& e)
  {
    LOG_ERROR("Failed to initialize Diameter stack - function %s, rc %d", e._func, e._rc);
    exit(2);
  }

  HttpStack* http_stack = HttpStack::get_instance();
  HssCacheHandler::configure_diameter(diameter_stack,
                                      options.dest_realm.empty() ? options.home_domain : options.dest_realm,
                                      options.dest_host == "0.0.0.0" ? "" : options.dest_host,
                                      options.server_name,
                                      dict);
  HssCacheHandler::configure_cache(cache);
  HssCacheHandler::configure_stats(stats_manager);

  // We should only query the cache for AV information if there is no HSS.  If there is an HSS, we
  // should always hit it.  If there is not, the AV information must have been provisioned in the
  // "cache" (which becomes persistent).
  bool hss_configured = !(options.dest_realm.empty() && (options.dest_host.empty() || options.dest_host == "0.0.0.0"));

  ImpiHandler::Config impi_handler_config(hss_configured,
                                          options.impu_cache_ttl,
                                          options.scheme_unknown,
                                          options.scheme_digest,
                                          options.scheme_aka,
                                          options.diameter_timeout_ms);
  ImpiRegistrationStatusHandler::Config registration_status_handler_config(hss_configured, options.diameter_timeout_ms);
  ImpuLocationInfoHandler::Config location_info_handler_config(hss_configured, options.diameter_timeout_ms);
  ImpuRegDataHandler::Config impu_handler_config(hss_configured, options.hss_reregistration_time, options.diameter_timeout_ms);
  ImpuIMSSubscriptionHandler::Config impu_handler_config_old(hss_configured, options.hss_reregistration_time, options.diameter_timeout_ms);

  HttpStackUtils::PingController ping_controller;
  HttpStackUtils::SpawningController<ImpiDigestHandler, ImpiHandler::Config> impi_digest_controller(&impi_handler_config);
  HttpStackUtils::SpawningController<ImpiAvHandler, ImpiHandler::Config> impi_av_controller(&impi_handler_config);
  HttpStackUtils::SpawningController<ImpiRegistrationStatusHandler, ImpiRegistrationStatusHandler::Config> impi_reg_status_controller(&registration_status_handler_config);
  HttpStackUtils::SpawningController<ImpuLocationInfoHandler, ImpuLocationInfoHandler::Config> impu_loc_info_controller(&location_info_handler_config);
  HttpStackUtils::SpawningController<ImpuRegDataHandler, ImpuRegDataHandler::Config> impu_reg_data_controller(&impu_handler_config);
  HttpStackUtils::SpawningController<ImpuIMSSubscriptionHandler, ImpuIMSSubscriptionHandler::Config> impu_ims_sub_controller(&impu_handler_config_old);

  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address,
                          options.http_port,
                          options.http_threads,
                          access_logger,
                          load_monitor,
                          stats_manager);
    http_stack->register_controller("^/ping$",
                                    &ping_controller);
    http_stack->register_controller("^/impi/[^/]*/digest$",
                                    &impi_digest_controller);
    http_stack->register_controller("^/impi/[^/]*/av",
                                    &impi_av_controller);
    http_stack->register_controller("^/impi/[^/]*/registration-status$",
                                    &impi_reg_status_controller);
    http_stack->register_controller("^/impu/[^/]*/location$",
                                    &impu_loc_info_controller);
    http_stack->register_controller("^/impu/[^/]*/reg-data$",
                                    &impu_reg_data_controller);
    http_stack->register_controller("^/impu/",
                                    &impu_ims_sub_controller);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Failed to initialize HttpStack stack - function %s, rc %d", e._func, e._rc);
    exit(2);
  }

  DiameterResolver* diameter_resolver = NULL;
  RealmManager* realm_manager = NULL;
  Diameter::Peer* peer = NULL;

  if (!options.dest_realm.empty())
  {
    diameter_resolver = new DiameterResolver(dns_resolver, af);
    realm_manager = new RealmManager(diameter_stack,
                                     options.dest_realm,
                                     options.max_peers,
                                     diameter_resolver);
    realm_manager->start();
  }
  else if (!(options.dest_host.empty() || options.dest_host == "0.0.0.0"))
  {
    peer = new Diameter::Peer(options.dest_host);
    diameter_stack->add(peer);
  }

  LOG_STATUS("Start-up complete - wait for termination signal");
  sem_wait(&term_sem);
  LOG_STATUS("Termination signal received - terminating");

  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Failed to stop HttpStack stack - function %s, rc %d", e._func, e._rc);
  }

  cache->stop();
  cache->wait_stopped();

  try
  {
    diameter_stack->stop();
    diameter_stack->wait_stopped();
  }
  catch (Diameter::Stack::Exception& e)
  {
    LOG_ERROR("Failed to stop Diameter stack - function %s, rc %d", e._func, e._rc);
  }
  delete dict; dict = NULL;
  delete ppr_config; ppr_config = NULL;
  delete rtr_config; rtr_config = NULL;
  delete ppr_controller; ppr_controller = NULL;
  delete rtr_controller; rtr_controller = NULL;

  delete sprout_conn; sprout_conn = NULL;

  if (!options.dest_realm.empty())
  {
    realm_manager->stop();
    delete realm_manager; realm_manager = NULL;
    delete diameter_resolver; diameter_resolver = NULL;
    delete dns_resolver; dns_resolver = NULL;
  }
  else if (!(options.dest_host.empty() || options.dest_host == "0.0.0.0"))
  {
    diameter_stack->remove(peer);
    delete peer; peer = NULL;
  }

  delete stats_manager; stats_manager = NULL;
  delete load_monitor; load_monitor = NULL;

  SAS::term();

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
