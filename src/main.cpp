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

#include <signal.h>
#include <semaphore.h>

#include "diameterstack.h"
#include "httpstack.h"
#include "handlers.h"

static sem_t term_sem;

// Signal handler that triggers sprout termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

int main(int argc, char**argv)
{
  std::string bind_address = "0.0.0.0";
  unsigned short port = 8888;

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();
  try
  {
    diameter_stack->initialize();
    diameter_stack->configure("/var/lib/homestead/homestead.conf");
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
  ImpiDigestHandler impi_digest_handler(diameter_stack, "realm", "host", "server-name");
  try
  {
    http_stack->initialize();
    http_stack->configure(bind_address, port, 10);
    http_stack->register_handler(&ping_handler);
    http_stack->register_handler(&impi_digest_handler);
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
