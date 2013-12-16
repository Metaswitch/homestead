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

#include "diameterstack.h"
#include "httpstack.h"
#include "handlers.h"

#include "cache.h"

struct options
{
  std::string diameter_conf;
  std::string http_address;
  unsigned short http_port;
  std::string dest_realm;
  std::string dest_host;
  std::string server_name;
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
       " -a, --analytics <directory>\n"
       "                            Generate analytics logs in specified directory\n"
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
    {"analytics",     required_argument, NULL, 'a'},
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
    case 'F':
    case 'L':
      // TODO: Implement.
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

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();
  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf);
    Cx::Dictionary dict;
    diameter_stack->advertize_application(dict.CX);
    diameter_stack->start();
  }
  catch (Diameter::Stack::Exception& e)
  {
    fprintf(stderr, "Caught Diameter::Stack::Exception - %s - %d\n", e._func, e._rc);
  }

  HttpStack* http_stack = HttpStack::get_instance();
  PingHandler ping_handler;
  ImpiDigestHandler impi_digest_handler(diameter_stack, options.dest_realm, options.dest_host, options.server_name);
  ImpiAvHandler impi_av_handler(diameter_stack, options.dest_realm, options.dest_host, options.server_name);
  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address, options.http_port, 10);
    http_stack->register_handler(&ping_handler);
    http_stack->register_handler(&impi_digest_handler);
    http_stack->register_handler(&impi_av_handler);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }

  Cache* cache = Cache::get_instance();
  cache->initialize();
  cache->configure("localhost", 9160, 10);
  Cache::ResultCode rc = cache->start();

  if (rc != Cache::ResultCode::OK)
  {
    fprintf(stderr, "Error starting cache: %d\n", rc);
  }

  sem_wait(&term_sem);

  cache->stop();
  cache->wait_stopped();

  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }

  try
  {
    diameter_stack->stop();
    diameter_stack->wait_stopped();
  }
  catch (Diameter::Stack::Exception& e)
  {
    fprintf(stderr, "Caught Diameter::Stack::Exception - %s - %d\n", e._func, e._rc);
  }

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
