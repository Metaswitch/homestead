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
#include "cache.h"
#include "diameterstack.h"
#include "httpstack.h"
#include "handlers.h"
#include "log.h"
#include "logger.h"

struct options
{
  std::string diameter_conf;
  std::string http_address;
  unsigned short http_port;
  std::string dest_realm;
  std::string dest_host;
  std::string server_name;
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
       " -h, --http <address>[:<port>]\n"
       "                            Set HTTP bind address and port (default: 0.0.0.0:8888)\n"
       " -D, --dest-realm <name>    Set Destination-Realm on Cx messages\n"
       " -d, --dest-host <name>     Set Destination-Host on Cx messages\n"
       " -s, --server-name <name>   Set Server-Name on Cx messages\n"
       " -a, --access-log <directory>\n"
       "                            Generate access logs in specified directory\n"
       " -F, --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " -L, --log-level N          Set log level to N (default: 4)\n"
       " -d, --daemon               Run as daemon\n"
       " -h, --help                 Show this help screen\n");
}

int init_options(int argc, char**argv, struct options& options)
{
  struct option long_opt[] =
  {
    {"diameter-conf", required_argument, NULL, 'c'},
    {"http",          required_argument, NULL, 'H'},
    {"dest-realm",    required_argument, NULL, 'D'},
    {"dest-host",     required_argument, NULL, 'd'},
    {"server-name",   required_argument, NULL, 's'},
    {"access-log",    required_argument, NULL, 'a'},
    {"log-file",      required_argument, NULL, 'F'},
    {"log-level",     required_argument, NULL, 'L'},
    {"help",          no_argument,       NULL, 'h'},
    {NULL,            0,                 NULL, 0},
  };

  int opt;
  int long_opt_ind;
  while ((opt = getopt_long(argc, argv, "c:H:D:d:s:a:F:L:h", long_opt, &long_opt_ind)) != -1)
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

    case 'D':
      options.dest_realm = std::string(optarg);
      break;

    case 'd':
      options.dest_host = std::string(optarg);
      break;

    case 's':
      options.server_name = std::string(optarg);
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

// Signal handler that triggers sprout termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

int main(int argc, char**argv)
{
  struct options options;

  options.diameter_conf = "homestead.conf";
  options.http_address = "0.0.0.0";
  options.http_port = 8888;
  options.dest_realm = "dest-realm.unknown";
  options.dest_host = "dest-host.unknown";
  options.server_name = "sip:server-name.unknown";
  options.access_log_enabled = false;
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
    if (slash_ptr != NULL) {
      prog_name = slash_ptr + 1;
    }
    Log::setLogger(new Logger(options.log_directory, prog_name));
  }

  AccessLogger* access_logger = NULL;
  if (options.access_log_enabled)
  {
    access_logger = new AccessLogger(options.access_log_directory);
  }

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();
  Cx::Dictionary* dict = NULL;
  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf);
    dict = new Cx::Dictionary();
    diameter_stack->advertize_application(dict->CX);
    diameter_stack->start();
  }
  catch (Diameter::Stack::Exception& e)
  {
    fprintf(stderr, "Caught Diameter::Stack::Exception - %s - %d\n", e._func, e._rc);
  }

  Cache* cache = Cache::get_instance();
  cache->initialize();
  cache->configure("localhost", 9160, 10);
  Cache::ResultCode rc = cache->start();

  if (rc != Cache::ResultCode::OK)
  {
    fprintf(stderr, "Error starting cache: %d\n", rc);
  }

  HttpStack* http_stack = HttpStack::get_instance();
  HssCacheHandler::configure_diameter(diameter_stack,
                                      options.dest_realm,
                                      options.dest_host,
                                      options.server_name,
                                      dict);
  HssCacheHandler::configure_cache(cache);
  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address, options.http_port, 10, access_logger);
    http_stack->register_handler("^/ping$",
                                 HttpStack::handler_factory<PingHandler>);
    http_stack->register_handler("^/impi/[^/]*/digest$",
                                 HttpStack::handler_factory<ImpiDigestHandler>);
    http_stack->register_handler("^/impi/[^/]*/av",
                                 HttpStack::handler_factory<ImpiDigestHandler>);
    http_stack->register_handler("^/impu/",
                                 HttpStack::handler_factory<ImpuIMSSubscriptionHandler>);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }

  sem_wait(&term_sem);

  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
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
    fprintf(stderr, "Caught Diameter::Stack::Exception - %s - %d\n", e._func, e._rc);
  }
  delete dict;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
