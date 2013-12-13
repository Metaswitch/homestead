/**
 * @file httpstack.cpp class implementation wrapping libevhtp HTTP stack
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

#include <httpstack.h>

HttpStack* HttpStack::INSTANCE = &DEFAULT_INSTANCE;
HttpStack HttpStack::DEFAULT_INSTANCE;

HttpStack::HttpStack() {}

void HttpStack::initialize()
{
  // Initialize if we haven't already done so.  We don't do this in the
  // constructor because we can't throw exceptions on failure.
  if (!_evbase)
  {
    _evbase = event_base_new();
  }
  if (!_evhtp)
  {
    _evhtp = evhtp_new(_evbase, NULL);
  }
}

void HttpStack::configure(const std::string& bind_address, unsigned short bind_port, int num_threads)
{
  _bind_address = bind_address;
  _bind_port = bind_port;
  _num_threads = num_threads;
}

void HttpStack::register_handler(Handler* handler)
{
  evhtp_callback_t* cb = evhtp_set_regex_cb(_evhtp, handler->path().c_str(), handler_callback_fn, handler);
  if (cb == NULL)
  {
    throw Exception("evhtp_set_cb", 0);
  }
}

void HttpStack::start()
{
  initialize();

  int rc = evhtp_use_threads(_evhtp, NULL, _num_threads, this);
  if (rc != 0)
  {
    throw Exception("evhtp_use_threads", rc);
  }

  rc = evhtp_bind_socket(_evhtp, _bind_address.c_str(), _bind_port, 1024);
  if (rc != 0)
  {
    throw Exception("evhtp_bind_socket", rc);
  }

  rc = pthread_create(&_event_base_thread, NULL, event_base_thread_fn, this);
  if (rc != 0)
  {
    throw Exception("pthread_create", rc);
  }
}

void HttpStack::stop()
{
  event_base_loopbreak(_evbase);
  evhtp_unbind_socket(_evhtp);
}

void HttpStack::wait_stopped()
{
  pthread_join(_event_base_thread, NULL);
}

void HttpStack::handler_callback_fn(evhtp_request_t* req, void* handler_ptr)
{
  Request request(req);
  ((Handler*)handler_ptr)->handle(request);
}

void* HttpStack::event_base_thread_fn(void* http_stack_ptr)
{
  ((HttpStack*)http_stack_ptr)->event_base_thread_fn();
  return NULL;
}

void HttpStack::event_base_thread_fn()
{
  event_base_loop(_evbase, 0);
}

