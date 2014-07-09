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
#include "test_interposer.hpp"
#include <curl/curl.h>

#include "httpstack.h"

#include "mockloadmonitor.hpp"
#include "mockstatisticsmanager.hpp"
#include "mock_sas.h"

using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::testing::Gt;

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

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

  int get(const std::string& path,
          int& status,
          std::string& response,
          std::list<std::string>* headers=NULL)
  {
    std::string url = _url_prefix + path;
    struct curl_slist *extra_headers = NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &string_store);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    if (headers && headers->size() > 0)
    {
      for(std::list<std::string>::iterator hdr = headers->begin();
          hdr != headers->end();
          ++hdr)
      {
        extra_headers = curl_slist_append(extra_headers, hdr->c_str());
      }
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, extra_headers);
    }

    int rc = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(extra_headers);
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

class HttpStackStatsTest : public HttpStackTest
{
public:
  HttpStackStatsTest()
  {
    _stack = HttpStack::get_instance();
    _stack->initialize();
    _stack->configure(_host.c_str(), _port, 1, NULL, &_load_monitor, &_stats_manager);
    _stack->start();

    cwtest_completely_control_time();
  }
  virtual ~HttpStackStatsTest()
  {
    cwtest_reset_time();
    stop_stack();
  }

  void start_stack()
  {
  }

private:
  // Strict mocks - we only allow method calls that the test explicitly expects.
  StrictMock<MockLoadMonitor> _load_monitor;
  StrictMock<MockStatisticsManager> _stats_manager;
};

// Basic controller.
class BasicController : public HttpStack::ControllerInterface
{
public:
  void process_request(HttpStack::Request &req, SAS::TrailId trail)
  {
    req.add_content("OK");
    req.send_reply(200, trail);
  }
};

// A controller that takes a long time to process requests (to test latency
// stats).
const int DELAY_MS = 2000;
const unsigned long DELAY_US = DELAY_MS * 1000;

class SlowController : public HttpStack::ControllerInterface
{
public:
  void process_request(HttpStack::Request &req, SAS::TrailId trail)
  {
    cwtest_advance_time_ms(DELAY_MS);
    req.send_reply(200, trail);
  }
};

// A controller that applies a load penalty.
class PenaltyController : public HttpStack::ControllerInterface
{
public:
  void process_request(HttpStack::Request &req, SAS::TrailId trail)
  {
    req.record_penalty();
    req.send_reply(200, trail);
  }
};

TEST_F(HttpStackTest, SimpleMainline)
{
  start_stack();
  stop_stack();
}

TEST_F(HttpStackTest, NoController)
{
  start_stack();

  int status;
  std::string response;
  int rc = get("/NoController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(404, status);

  stop_stack();
}

TEST_F(HttpStackTest, SimpleController)
{
  start_stack();

  BasicController controller;
  _stack->register_controller("^/BasicController$", &controller);

  int status;
  std::string response;
  int rc = get("/BasicController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(200, status);
  ASSERT_EQ("OK", response);

  // Check that NoController _doesn't_ match.
  rc = get("/NoController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(404, status);

  stop_stack();
}

// Check that the stack copes with receiving a SAS correltion header.
TEST_F(HttpStackTest, SASCorrelationHeader)
{
  mock_sas_collect_messages(true);
  start_stack();

  BasicController controller;
  _stack->register_controller("^/BasicController$", &controller);

  int status;
  std::string response;
  std::list<std::string> hdrs;
  hdrs.push_back("X-SAS-HTTP-Branch-ID: 12345678-1234-1234-1234-123456789ABC");

  int rc = get("/BasicController", status, response, &hdrs);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(200, status);
  ASSERT_EQ("OK", response);

  MockSASMessage* marker = mock_sas_find_marker(MARKER_ID_VIA_BRANCH_PARAM);
  EXPECT_TRUE(marker != NULL);
  EXPECT_EQ(marker->var_params.size(), 1u);
  EXPECT_EQ(marker->var_params[0], "12345678-1234-1234-1234-123456789ABC");

  stop_stack();
  mock_sas_collect_messages(false);
}


TEST_F(HttpStackStatsTest, SuccessfulRequest)
{
  SlowController controller;
  _stack->register_controller("^/BasicController$", &controller);

  EXPECT_CALL(_load_monitor, admit_request()).WillOnce(Return(true));
  EXPECT_CALL(_stats_manager, incr_http_incoming_requests()).Times(1);
  EXPECT_CALL(_stats_manager, update_http_latency_us(DELAY_US)).Times(1);
  EXPECT_CALL(_load_monitor, request_complete(DELAY_US)).Times(1);

  int status;
  std::string response;
  int rc = get("/BasicController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(200, status);
}

TEST_F(HttpStackStatsTest, RejectOverload)
{
  BasicController controller;
  _stack->register_controller("^/BasicController$", &controller);

  EXPECT_CALL(_load_monitor, admit_request()).WillOnce(Return(false));
  EXPECT_CALL(_stats_manager, incr_http_incoming_requests()).Times(1);
  EXPECT_CALL(_stats_manager, incr_http_rejected_overload()).Times(1);

  int status;
  std::string response;
  int rc = get("/BasicController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(503, status);  // Request is rejected with a 503.
}

TEST_F(HttpStackStatsTest, LatencyPenalties)
{
  PenaltyController controller;
  _stack->register_controller("^/BasicController$", &controller);

  EXPECT_CALL(_load_monitor, admit_request()).WillOnce(Return(true));
  EXPECT_CALL(_load_monitor, incr_penalties()).Times(1);
  EXPECT_CALL(_stats_manager, incr_http_incoming_requests()).Times(1);
  EXPECT_CALL(_stats_manager, update_http_latency_us(_)).Times(1);
  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);

  int status;
  std::string response;
  int rc = get("/BasicController", status, response);
  ASSERT_EQ(CURLE_OK, rc);
  ASSERT_EQ(200, status);
}
