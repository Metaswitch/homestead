/**
 * @file httpstack.h class definitition wrapping HTTP stack
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

#ifndef HTTP_H__
#define HTTP_H__

#include <pthread.h>
#include <string>

#include <evhtp.h>

class HttpStack
{
public:
  class Exception
  {
  public:
    inline Exception(const char* func, int rc) : _func(func), _rc(rc) {};
    const char* _func;
    const int _rc;
  };

  class Request
  {
  public:
    Request(evhtp_request_t* req) : _req(req) {}
    inline std::string path() const {return std::string(_req->uri->path->path);}
    inline std::string param(const std::string& name) const
    {
      const char* param = evhtp_kv_find(_req->uri->query, name.c_str());
      return std::string(param != NULL ? param : "");
    }
    void add_content(const std::string& content) {evbuffer_add(_req->buffer_out, content.c_str(), content.length());}
    void send_reply(int rc) {evhtp_send_reply(_req, rc);}
  private:
    evhtp_request_t* _req;
  };

  class Handler
  {
  public:
    inline Handler(const std::string& path) : _path(path) {}
    virtual ~Handler() {}
    inline const std::string path() const {return _path;}
    virtual void handle(Request& req) = 0;

  private:
    std::string _path;
  };

  static inline HttpStack* get_instance() {return INSTANCE;};
  void initialize();
  void configure(const std::string& bind_address, unsigned short port, int num_threads);
  void register_handler(Handler* handler);
  void start();
  void stop();
  void wait_stopped();

private:
  static HttpStack* INSTANCE;
  static HttpStack DEFAULT_INSTANCE;

  HttpStack();
  static void handler_callback_fn(evhtp_request_t* req, void* handler_ptr);
  static void* event_base_thread_fn(void* http_stack_ptr); 
  void event_base_thread_fn();

  // Don't implement the following, to avoid copies of this instance.
  HttpStack(HttpStack const&);
  void operator=(HttpStack const&);

  std::string _bind_address;
  unsigned short _bind_port;
  int _num_threads;
  evbase_t* _evbase;
  evhtp_t* _evhtp;
  pthread_t _event_base_thread;
};

#endif
