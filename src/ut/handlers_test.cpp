/**
 * @file handlers_test.cpp UT for Handlers module.
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

#include "mockdiameterstack.hpp"
#include "mockhttpstack.hpp"
#include "mockcache.hpp"
#include "handlers.h"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;

/// Fixture for HandlersTest.
class HandlersTest : public testing::Test
{
public:
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const std::string DEFAULT_SERVER_NAME;
  static const std::string SERVER_NAME;
  static const std::string IMPI;
  static const std::string IMPU;
  static const std::string IMS_SUBSCRIPTION;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;
  static MockCache* _cache;
  static MockHttpStack* _httpstack;

  static struct msg* _caught_fd_msg;
  static Diameter::Transaction* _caught_diam_tsx;
  
  std::string test_str;
  int32_t test_i32;

  static void SetUpTestCase()
  {
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf");
    _real_stack->start();
    _mock_stack = new MockDiameterStack();
    _cx_dict = new Cx::Dictionary();
    _cache = new MockCache();
    _httpstack = new MockHttpStack();
    HssCacheHandler::configure_diameter(_mock_stack,
                                        DEST_REALM,
                                        DEST_HOST,
                                        DEFAULT_SERVER_NAME,
                                        _cx_dict);
    HssCacheHandler::configure_cache(_cache);
  }

  static void TearDownTestCase()
  {
    delete _cx_dict; _cx_dict = NULL;
    delete _mock_stack; _mock_stack = NULL;
    delete _cache; _cache = NULL;
    delete _httpstack; _httpstack = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
  }

  static void store(struct msg* msg, Diameter::Transaction* tsx)
  {
    _caught_fd_msg = msg;
    _caught_diam_tsx = tsx;
  }

  static std::string build_icscf_json(int32_t rc, std::string scscf, ServerCapabilities capabs)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_RC.c_str());
    writer.Int(rc);
    if (!scscf.empty())
    {
      writer.String(JSON_SCSCF.c_str());
      writer.String(scscf.c_str());
    }
    else
    {
      writer.String(JSON_MAN_CAP.c_str());
      writer.StartArray();
      if (!capabs.mandatory_capabilities.empty())
      {
        for (std::vector<int>::const_iterator it = capabs.mandatory_capabilities.begin();
             it != capabs.mandatory_capabilities.end();
             ++it)
        {
          writer.Int(*it);
        }
      }
      writer.EndArray();
      writer.String(JSON_OPT_CAP.c_str());
      writer.StartArray();
      if (!capabs.optional_capabilities.empty())
      {
        for (std::vector<int>::const_iterator it = capabs.optional_capabilities.begin();
            it != capabs.optional_capabilities.end();
            ++it)
        {
          writer.Int(*it);
        }
      }
      writer.EndArray();
    }
    writer.EndObject();
    return sb.GetString();
  }

  HandlersTest() {}
  ~HandlersTest() {}
};

const std::string HandlersTest::DEST_REALM = "dest-realm";
const std::string HandlersTest::DEST_HOST = "dest-host";
const std::string HandlersTest::DEFAULT_SERVER_NAME = "sprout";
const std::string HandlersTest::SERVER_NAME = "scscf";
const std::string HandlersTest::IMPI = "impi@example.com";
const std::string HandlersTest::IMPU = "sip:impu@example.com";
const std::string HandlersTest::IMS_SUBSCRIPTION = "<some interesting XML>";

Diameter::Stack* HandlersTest::_real_stack = NULL;
MockDiameterStack* HandlersTest::_mock_stack = NULL;
Cx::Dictionary* HandlersTest::_cx_dict = NULL;
MockCache* HandlersTest::_cache = NULL;
MockHttpStack* HandlersTest::_httpstack = NULL;

struct msg* HandlersTest::_caught_fd_msg = NULL;
Diameter::Transaction* HandlersTest::_caught_diam_tsx = NULL;

TEST_F(HandlersTest, SimpleMainline)
{
  MockHttpStack::Request req(_httpstack, "/", "ping");
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  PingHandler* handler = new PingHandler(req);
  handler->run();
  EXPECT_EQ("OK", req.content());
}

//
// IMS Subscription tests
//

TEST_F(HandlersTest, IMSSubscriptionCache)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI);
  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  t->on_success(&mock_req);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());
}

TEST_F(HandlersTest, IMSSubscriptionHSS)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI);
  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store)));
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(DEFAULT_SERVER_NAME, test_str);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

//
// Registration Status tests
//

TEST_F(HandlersTest, RegistrationStatus)
{
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);
  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store)));
  handler->run();

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::UserAuthorizationRequest uar(msg);
  EXPECT_TRUE(uar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(uar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, uar.impi());
  EXPECT_EQ(IMPU, uar.impu());
  EXPECT_TRUE(uar.visited_network(test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(0, test_i32);

  ServerCapabilities capabs;
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  capabs);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, capabs), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, RegistrationStatusNoHSS)
{
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=sip:impu@example.com");
  ImpiRegistrationStatusHandler::Config cfg(false);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct
  ServerCapabilities capabs;
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, capabs), req.content());
}

//
// Location Info tests
//

TEST_F(HandlersTest, LocationInfo)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");
  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store)));
  handler->run();

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));

  ServerCapabilities capabs;
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             SERVER_NAME,
                             capabs);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(lia);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, capabs), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, LocationInfoNoHSS)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");
  ImpuLocationInfoHandler::Config cfg(false);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct
  ServerCapabilities capabs;
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, capabs), req.content());
}

#if 0

// Transaction that would be implemented in the handlers.
class ExampleTransaction : public Cache::Transaction
{
  void on_success(Cache::Request* req)
  {
    std::string xml;
    dynamic_cast<Cache::GetIMSSubscription*>(req)->get_result(xml);
    std::cout << "GOT RESULT: " << xml << std::endl;
  }

  void on_failure(Cache::Request* req, Cache::ResultCode rc, std::string& text)
  {
    std::cout << "FAILED:" << std::endl << text << std::endl;
  }
};

// Start of the test code.
TEST(HandlersTest, ExampleTransaction)
{
  MockCache cache;
  MockCache::MockGetIMSSubscription mock_req;

  EXPECT_CALL(cache, create_GetIMSSubscription("kermit"))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>("<some boring xml>"));

  // This would be in the code-under-test.
  ExampleTransaction* tsx = new ExampleTransaction;
  Cache::Request* req = cache.create_GetIMSSubscription("kermit");
  cache.send(tsx, req);

  // Back to the test code.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t->on_success(&mock_req);
}
#endif
