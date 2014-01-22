/**
 * @file httpstack_test.cpp UT for HttpStack module.
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

#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"
#include <curl/curl.h>

#include "httpstack.h"

/// Fixture for HttpStackTest.
class HttpStackTest : public testing::Test
{
public:
  HttpStackTest()
  {
    _host = "127.0.0.1";
    _port = 16384 + (getpid() % 16384);
    std::stringstream ss;
    ss << "http://" << _host << ":" << _port;
    _url_prefix = ss.str();
  }

  ~HttpStackTest()
  {
    if (_stack != NULL)
    {
      stop_stack();
    }
  }
  
  void start_stack()
  {
    _stack = HttpStack::get_instance();
    _stack->initialize();
    _stack->configure(_host.c_str(), _port, 1);
    _stack->start();
  }
  
  void stop_stack()
  {
    _stack->stop();
    _stack->wait_stopped();
    _stack = NULL;
  }

  int get(const std::string& path, int& status, std::string& response)
  {
    std::string url = _url_prefix + path;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
  
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &string_store);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  
    int rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  
    curl_easy_cleanup(curl);
    curl_global_cleanup();
  
    return rc;
  }
  
  HttpStack* _stack;

private:
  static size_t string_store(void* ptr, size_t size, size_t nmemb, void* stream)
  {
    ((std::string*)stream)->append((char*)ptr, size * nmemb);
    return (size * nmemb);
  }

  std::string _host;
  int _port;
  std::string _url_prefix;
};

// Basic Handler to test handler function.
class BasicHandler : public HttpStack::Handler
{
public:
  BasicHandler(HttpStack::Request& req) : HttpStack::Handler(req) {};
  void run()
  {
    _req.add_content("OK");
    _req.send_reply(200);
    delete this;
  }
};

TEST_F(HttpStackTest, SimpleMainline)
{
  start_stack();
  stop_stack();
}

TEST_F(HttpStackTest, NoHandler)
{
  start_stack();

  int status;
  std::string response;
  int rc = get("/NoHandler", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(404, status);

  stop_stack();
}

TEST_F(HttpStackTest, SimpleHandler)
{
  start_stack();

  HttpStack::HandlerFactory<BasicHandler> basic_handler_factory;
  _stack->register_handler("^/BasicHandler$", &basic_handler_factory);

  int status;
  std::string response;
  int rc = get("/BasicHandler", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(200, status);
  ASSERT_EQ("OK", response);

  // Check that NoHandler _doesn't_ match.
  rc = get("/NoHandler", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(404, status);

  stop_stack();
}
