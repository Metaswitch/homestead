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

struct options
{
  std::string diameter_conf;
  std::string http_address;
  unsigned short http_port;
  int http_threads;
  std::string cassandra;
  std::string dest_realm;
  std::string dest_host;
  std::string server_name;
  int impu_cache_ttl;
  int ims_sub_cache_ttl;
  std::string scheme_unknown;
  std::string scheme_digest;
  std::string scheme_aka;
  bool access_log_enabled;
  std::string access_log_directory;
  bool log_to_file;
  std::string log_directory;
  int log_level;
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " -c, --diameter-conf <file> File name for Diameter configuration\n"
       " -H, --http <address>[:<port>]\n"
       "                            Set HTTP bind address and port (default: 0.0.0.0:8888)\n"
       " -t, --http-threads N       Number of HTTP threads (default: 1)\n"
       " -S, --cassandra <address>  Set the IP address or FQDN of the Cassandra database (default: localhost)"
       " -D, --dest-realm <name>    Set Destination-Realm on Cx messages\n"
       " -d, --dest-host <name>     Set Destination-Host on Cx messages\n"
       " -s, --server-name <name>   Set Server-Name on Cx messages\n"
       " -i, --impu-cache-ttl <secs>\n"
       "                            IMPU cache time-to-live in seconds (default: 0)\n"
       " -I, --ims-sub-cache-ttl <secs>\n"
       "                            IMS subscription cache time-to-live in seconds (default: 0)\n"
       "     --scheme-unknown <string>\n"
       "                            String to use to specify unknown SIP-Auth-Scheme (default: Unknown)\n"
       "     --scheme-digest <string>\n"
       "                            String to use to specify digest SIP-Auth-Scheme (default: SIP Digest)\n"
       "     --scheme-aka <string>\n"
       "                            String to use to specify AKA SIP-Auth-Scheme (default: Digest-AKAv1-MD5)\n"
       " -a, --access-log <directory>\n"
       "                            Generate access logs in specified directory\n"
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
  SCHEME_AKA
};

int init_options(int argc, char**argv, struct options& options)
{
  struct option long_opt[] =
  {
    {"diameter-conf",     required_argument, NULL, 'c'},
    {"http",              required_argument, NULL, 'H'},
    {"http-threads",      required_argument, NULL, 't'},
    {"cassandra",         required_argument, NULL, 'S'},
    {"dest-realm",        required_argument, NULL, 'D'},
    {"dest-host",         required_argument, NULL, 'd'},
    {"server-name",       required_argument, NULL, 's'},
    {"impu-cache-ttl",    required_argument, NULL, 'i'},
    {"ims-sub-cache-ttl", required_argument, NULL, 'I'},
    {"scheme-unknown",    required_argument, NULL, SCHEME_UNKNOWN},
    {"scheme-digest",     required_argument, NULL, SCHEME_DIGEST},
    {"scheme-aka",        required_argument, NULL, SCHEME_AKA},
    {"access-log",        required_argument, NULL, 'a'},
    {"log-file",          required_argument, NULL, 'F'},
    {"log-level",         required_argument, NULL, 'L'},
    {"help",              no_argument,       NULL, 'h'},
    {NULL,                0,                 NULL, 0},
  };

  int opt;
  int long_opt_ind;
  while ((opt = getopt_long(argc, argv, "c:H:t:S:D:d:s:i:I:a:F:L:h", long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'c':
      options.diameter_conf = std::string(optarg);
      break;

    case 'H':
      options.http_address = std::string(optarg);
      // TODO: Parse optional HTTP port.
      break;

    case 't':
      options.http_threads = atoi(optarg);
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

    case 's':
      options.server_name = std::string(optarg);
      break;

    case 'i':
      options.impu_cache_ttl = atoi(optarg);
      break;

    case 'I':
      options.ims_sub_cache_ttl = atoi(optarg);
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
  options.diameter_conf = "homestead.conf";
  options.http_address = "0.0.0.0";
  options.http_port = 8888;
  options.http_threads = 1;
  options.cassandra = "localhost";
  options.dest_realm = "dest-realm.unknown";
  options.dest_host = "dest-host.unknown";
  options.server_name = "sip:server-name.unknown";
  options.scheme_unknown = "Unknown";
  options.scheme_digest = "SIP Digest";
  options.scheme_aka = "Digest-AKAv1-MD5";
  options.access_log_enabled = false;
  options.impu_cache_ttl = 0;
  options.ims_sub_cache_ttl = 0;
  options.log_to_file = false;
  options.log_level = 0;

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

  StatisticsManager* stats_manager = new StatisticsManager();
  LoadMonitor* load_monitor = new LoadMonitor(100000, 20, 10.0, 10.0);

  Cache* cache = Cache::get_instance();
  cache->initialize();
  // TODO: Make number of threads configurable.
  cache->configure(options.cassandra, 9160, 10);
  Cache::ResultCode rc = cache->start();

  if (rc != Cache::OK)
  {
    LOG_ERROR("Failed to initialize cache - rc %d", rc);
    exit(2);
  }

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();
  RegistrationTerminationHandler::Config rt_handler_config(NULL, NULL, 0);
  PushProfileHandler::Config pp_handler_config(NULL, NULL, 0, 0);
  Diameter::Stack::ConfiguredHandlerFactory<RegistrationTerminationHandler, RegistrationTerminationHandler::Config> rtr_handler_factory(NULL, NULL);
  Diameter::Stack::ConfiguredHandlerFactory<PushProfileHandler, PushProfileHandler::Config> ppr_handler_factory(NULL, NULL);
  Cx::Dictionary* dict = NULL;
  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf);
    dict = new Cx::Dictionary();
    diameter_stack->advertize_application(dict->TGPP, dict->CX);
    rt_handler_config = RegistrationTerminationHandler::Config(cache, dict, options.ims_sub_cache_ttl);
    pp_handler_config = PushProfileHandler::Config(cache, dict, options.impu_cache_ttl, options.ims_sub_cache_ttl);
    rtr_handler_factory = Diameter::Stack::ConfiguredHandlerFactory<RegistrationTerminationHandler, RegistrationTerminationHandler::Config>(dict, &rt_handler_config);
    ppr_handler_factory = Diameter::Stack::ConfiguredHandlerFactory<PushProfileHandler, PushProfileHandler::Config>(dict, &pp_handler_config);
    diameter_stack->register_handler(dict->CX, dict->REGISTRATION_TERMINATION_REQUEST, &rtr_handler_factory);
    diameter_stack->register_handler(dict->CX, dict->PUSH_PROFILE_REQUEST, &ppr_handler_factory);
    diameter_stack->register_fallback_handler(dict->CX);
    diameter_stack->start();
  }
  catch (Diameter::Stack::Exception& e)
  {
    LOG_ERROR("Failed to initialize Diameter stack - function %s, rc %d", e._func, e._rc);
    exit(2);
  }

  HttpStack* http_stack = HttpStack::get_instance();
  HssCacheHandler::configure_diameter(diameter_stack,
                                      options.dest_realm,
                                      options.dest_host,
                                      options.server_name,
                                      dict);
  HssCacheHandler::configure_cache(cache);
  HssCacheHandler::configure_stats(stats_manager);

  // We should only query the cache for AV information if there is no HSS.  If there is an HSS, we
  // should always hit it.  If there is not, the AV information must have been provisioned in the
  // "cache" (which becomes persistent).
  bool hss_configured = !(options.dest_host.empty() || (options.dest_host == "0.0.0.0"));

  ImpiHandler::Config impi_handler_config(hss_configured,
                                          options.impu_cache_ttl,
                                          options.scheme_unknown,
                                          options.scheme_digest,
                                          options.scheme_aka);
  ImpiRegistrationStatusHandler::Config registration_status_handler_config(hss_configured);
  ImpuLocationInfoHandler::Config location_info_handler_config(hss_configured);
  ImpuIMSSubscriptionHandler::Config impu_handler_config(hss_configured, options.ims_sub_cache_ttl);

  HttpStack::HandlerFactory<PingHandler> ping_handler_factory;
  HttpStack::ConfiguredHandlerFactory<ImpiDigestHandler, ImpiHandler::Config> impi_digest_handler_factory(&impi_handler_config);
  HttpStack::ConfiguredHandlerFactory<ImpiAvHandler, ImpiHandler::Config> impi_av_handler_factory(&impi_handler_config);
  HttpStack::ConfiguredHandlerFactory<ImpiRegistrationStatusHandler, ImpiRegistrationStatusHandler::Config> impi_reg_status_handler_factory(&registration_status_handler_config);
  HttpStack::ConfiguredHandlerFactory<ImpuLocationInfoHandler, ImpuLocationInfoHandler::Config> impu_loc_info_handler_factory(&location_info_handler_config);
  HttpStack::ConfiguredHandlerFactory<ImpuIMSSubscriptionHandler, ImpuIMSSubscriptionHandler::Config> impu_ims_sub_handler_factory(&impu_handler_config);

  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address,
                          options.http_port,
                          options.http_threads,
                          access_logger,
                          stats_manager,
                          load_monitor);
    http_stack->register_handler("^/ping$",
                                 &ping_handler_factory);
    http_stack->register_handler("^/impi/[^/]*/digest$",
                                 &impi_digest_handler_factory);
    http_stack->register_handler("^/impi/[^/]*/av",
                                 &impi_av_handler_factory);
    http_stack->register_handler("^/impi/[^/]*/registration-status$",
                                 &impi_reg_status_handler_factory);
    http_stack->register_handler("^/impu/[^/]*/location$",
                                 &impu_loc_info_handler_factory);
    http_stack->register_handler("^/impu/",
                                 &impu_ims_sub_handler_factory);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Failed to initialize HttpStack stack - function %s, rc %d", e._func, e._rc);
    exit(2);
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
  delete dict;

  delete stats_manager; stats_manager = NULL;
  delete load_monitor; load_monitor = NULL;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
