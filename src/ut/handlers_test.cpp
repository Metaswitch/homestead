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
#include "test_interposer.hpp"
#include <curl/curl.h>

#include "mockdiameterstack.hpp"
#include "mockhttpstack.hpp"
#include "mockcache.hpp"
#include "handlers.h"
#include "mockstatisticsmanager.hpp"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;

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
  static const std::string VISITED_NETWORK;
  static const std::string AUTH_TYPE_DEREG;
  static const ServerCapabilities CAPABILITIES;
  static const ServerCapabilities NO_CAPABILITIES;
  static const int32_t AUTH_SESSION_STATE;
  static std::vector<std::string> ASSOCIATED_IDENTITIES;
  static std::vector<std::string> IMPUS;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;
  static MockCache* _cache;
  static MockHttpStack* _httpstack;

  // Two mock stats managers, so we can choose whether to ignore stats or not.
  static NiceMock<MockStatisticsManager>* _nice_stats;
  static StrictMock<MockStatisticsManager>* _stats;

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
    _nice_stats = new NiceMock<MockStatisticsManager>;

    HssCacheHandler::configure_diameter(_mock_stack,
                                        DEST_REALM,
                                        DEST_HOST,
                                        DEFAULT_SERVER_NAME,
                                        _cx_dict);
    HssCacheHandler::configure_cache(_cache);
    HssCacheHandler::configure_stats(_nice_stats);

    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _stats; _stats = NULL;
    delete _nice_stats; _nice_stats = NULL;
    delete _cx_dict; _cx_dict = NULL;
    delete _mock_stack; _mock_stack = NULL;
    delete _cache; _cache = NULL;
    delete _httpstack; _httpstack = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
  }

  static void store_msg_tsx(struct msg* msg, Diameter::Transaction* tsx)
  {
    _caught_fd_msg = msg;
    _caught_diam_tsx = tsx;
  }

  static void store_msg(struct msg* msg)
  {
    _caught_fd_msg = msg;
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

  static void registration_status_error_template(int32_t hss_rc, int32_t hss_experimental_rc, int32_t http_rc)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impi/" + IMPI + "/",
                               "registration-status",
                               "?impu=" + IMPU);
    ImpiRegistrationStatusHandler::Config cfg(true);
    ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
    EXPECT_CALL(*_mock_stack, send(_, _, 200))
      .Times(1)
      .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
    handler->run();

    ASSERT_FALSE(_caught_diam_tsx == NULL);
    Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                    _mock_stack,
                                    hss_rc,
                                    hss_experimental_rc,
                                    "",
                                    NO_CAPABILITIES);
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc));
    _caught_diam_tsx->on_response(uaa);

    EXPECT_EQ("", req.content());

    _caught_diam_tsx = NULL;
    _caught_fd_msg = NULL;
  }

  static void location_info_error_template(int32_t hss_rc, int32_t hss_experimental_rc, int32_t http_rc)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/",
                               "location",
                               "");
    ImpuLocationInfoHandler::Config cfg(true);
    ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
    EXPECT_CALL(*_mock_stack, send(_, _, 200))
      .Times(1)
      .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
    handler->run();

    ASSERT_FALSE(_caught_diam_tsx == NULL);
    Cx::LocationInfoAnswer lia(_cx_dict,
                               _mock_stack,
                               hss_rc,
                               hss_experimental_rc,
                               "",
                               NO_CAPABILITIES);
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc));
    _caught_diam_tsx->on_response(lia);

    EXPECT_EQ("", req.content());

    _caught_diam_tsx = NULL;
    _caught_fd_msg = NULL;
  }

  void ignore_stats(bool ignore)
  {
    if (ignore)
    {
      HssCacheHandler::configure_stats(_nice_stats);
    }
    else
    {
      HssCacheHandler::configure_stats(_stats);
    }
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
const std::string HandlersTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><ServiceProfile><PublicIdentity><Identity>" +
                                                   IMPU +
                                                   "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HandlersTest::VISITED_NETWORK = "visited-network.com";
const std::string HandlersTest::AUTH_TYPE_DEREG = "DEREG";
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities HandlersTest::CAPABILITIES(mandatory_capabilities, optional_capabilities);
const ServerCapabilities HandlersTest::NO_CAPABILITIES(no_capabilities, no_capabilities);
const int32_t HandlersTest::AUTH_SESSION_STATE = 1;
std::vector<std::string> HandlersTest::ASSOCIATED_IDENTITIES = {"impi456", "impi478"};
std::vector<std::string> HandlersTest::IMPUS = {"impu456", "impu478"};

Diameter::Stack* HandlersTest::_real_stack = NULL;
MockDiameterStack* HandlersTest::_mock_stack = NULL;
Cx::Dictionary* HandlersTest::_cx_dict = NULL;
MockCache* HandlersTest::_cache = NULL;
MockHttpStack* HandlersTest::_httpstack = NULL;
NiceMock<MockStatisticsManager>* HandlersTest::_nice_stats = NULL;
StrictMock<MockStatisticsManager>* HandlersTest::_stats = NULL;

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

TEST_F(HandlersTest, IMSSubscriptionRereg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=rereg");
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

  // Build the expected response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());
}

TEST_F(HandlersTest, IMSSubscriptionReregHSS)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=rereg");
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
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
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
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(2, test_i32);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus;
  impus.push_back(IMPU);
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, IMSSubscriptionReg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=reg");
  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();

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
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(1, test_i32);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  MockCache::MockPutIMSSubscription mock_req;
  std::vector<std::string> impus;
  impus.push_back(IMPU);
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, IMSSubscriptionDereg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=dereg-user");
  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();

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
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(5, test_i32);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected empty response and check it's correct
  EXPECT_EQ("", req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, IMSSubscriptionNoCacheNoHss)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI);
  ImpuIMSSubscriptionHandler::Config cfg(false, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  EXPECT_CALL(*_httpstack, send_reply(_, 502));
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlersTest, IMSSubscriptionUserUnknownDereg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=dereg-timeout");
  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  std::string error_text = "error";
  handler->run();

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_ERROR_USER_UNKNOWN,
                                 "");

  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(saa);

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlersTest, IMSSubscriptionOtherErrorCallReg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=call-reg");
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
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 0,
                                 "");

  EXPECT_CALL(*_httpstack, send_reply(_, 500));
  _caught_diam_tsx->on_response(saa);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

//
// Registration Status tests
//

TEST_F(HandlersTest, RegistrationStatus)
{
  // This test tests a mainline Registration Status handler case.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);
  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
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

  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, RegistrationStatusOptParamsSubseqRegCapabs)
{
  // This test tests a Registration Status handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, optional parameters
  // on the HTTP request are added, and the success code
  // DIAMETER_SUBSEQUENT_REGISTRATION is returned by the HSS with a set of server
  // capabilities. 
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU + "&visited-network=" + VISITED_NETWORK + "&auth-type=" + AUTH_TYPE_DEREG);
  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
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
  EXPECT_EQ(VISITED_NETWORK, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(1, test_i32);

  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  DIAMETER_SUBSEQUENT_REGISTRATION,
                                  "",
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUBSEQUENT_REGISTRATION, "", CAPABILITIES), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, RegistrationStatusFirstRegNoCapabs)
{
  // This test tests a Registration Status handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, the success code
  // DIAMETER_FIRST_REGISTRATION is returned by the HSS, but no server or server
  // capabilities are specified.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);
  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
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

  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  DIAMETER_FIRST_REGISTRATION,
                                  "",
                                  NO_CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_FIRST_REGISTRATION, "", NO_CAPABILITIES), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, RegistrationStatusUserUnknown)
{
  registration_status_error_template(0, DIAMETER_ERROR_USER_UNKNOWN, 404);
}

TEST_F(HandlersTest, RegistrationStatusIdentitiesDontMatch)
{
  registration_status_error_template(0, DIAMETER_ERROR_IDENTITIES_DONT_MATCH, 404);
}

TEST_F(HandlersTest, RegistrationStatusRoamingNowAllowed)
{
  registration_status_error_template(0, DIAMETER_ERROR_ROAMING_NOT_ALLOWED, 403);
}

TEST_F(HandlersTest, RegistrationStatusAuthRejected)
{
  registration_status_error_template(DIAMETER_AUTHORIZATION_REJECTED, 0, 403);
}

TEST_F(HandlersTest, RegistrationStatusDiameterBusy)
{
  registration_status_error_template(DIAMETER_TOO_BUSY, 0, 503);
}

TEST_F(HandlersTest, RegistrationStatusOtherError)
{
  registration_status_error_template(0, 0, 500);
}

TEST_F(HandlersTest, RegistrationStatusNoHSS)
{
  // Test Registration Status handler when no HSS is configured.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=sip:impu@example.com");
  ImpiRegistrationStatusHandler::Config cfg(false);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, NO_CAPABILITIES), req.content());
}

//
// Location Info tests
//

TEST_F(HandlersTest, LocationInfo)
{
  // This test tests a mainline Registration Status handler case.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");
  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
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

  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(lia);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, LocationInfoOptParamsUnregisteredService)
{
  // This test tests a Location Info handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, optional parameters
  // on the HTTP request are added, and the success code
  // DIAMETER_UNREGISTERED_SERVICE is returned by the HSS with a set of server
  // capabilities.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "?originating=true&auth-type=CAPAB");
  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_TRUE(lir.originating(test_i32));
  EXPECT_EQ(0, test_i32);
  EXPECT_TRUE(lir.auth_type(test_i32));
  EXPECT_EQ(2, test_i32);

  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             DIAMETER_UNREGISTERED_SERVICE,
                             "",
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(lia);

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_UNREGISTERED_SERVICE, "", CAPABILITIES), req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, LocationInfoUserUnknown)
{
  location_info_error_template(0, DIAMETER_ERROR_USER_UNKNOWN, 404);
}

TEST_F(HandlersTest, LocationInfoIdentityNotRegistered)
{
  location_info_error_template(0, DIAMETER_ERROR_IDENTITY_NOT_REGISTERED, 404);
}

TEST_F(HandlersTest, LocationInfoDiameterBusy)
{
  location_info_error_template(DIAMETER_TOO_BUSY, 0, 503);
}

TEST_F(HandlersTest, LocationInfoOtherError)
{
  location_info_error_template(0, 0, 500);
}

TEST_F(HandlersTest, LocationInfoNoHSS)
{
  // Test Location Info handler when no HSS is configured.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");
  ImpuLocationInfoHandler::Config cfg(false);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, NO_CAPABILITIES), req.content());
}

//
// Registration Termination tests
//

TEST_F(HandlersTest, RegistrationTerminationNoImpus)
{
  std::vector<std::string> no_impus;
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         no_impus,
                                         AUTH_SESSION_STATE);
  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(rtr, &cfg);

  std::vector<std::string> associated_identities;
  associated_identities.push_back(IMPI);
  associated_identities.insert(associated_identities.end(), ASSOCIATED_IDENTITIES.begin(), ASSOCIATED_IDENTITIES.end());
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(associated_identities))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(IMPUS));

  MockCache::MockDeletePublicIDs mock_req2;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPUS, _))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));
  MockCache::MockDeletePrivateIDs mock_req3;
  EXPECT_CALL(*_cache, create_DeletePrivateIDs(associated_identities, _))
    .WillOnce(Return(&mock_req3));
  EXPECT_CALL(*_cache, send(_, &mock_req3))
    .WillOnce(WithArgs<0>(Invoke(&mock_req3, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req);

  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req3.get_trx();
  ASSERT_FALSE(t == NULL);

  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice. 
  msg._free_on_delete = false;
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(associated_identities, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());

  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, RegistrationTermination)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);
  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(rtr, &cfg);

  std::vector<std::string> associated_identities;
  associated_identities.push_back(IMPI);
  associated_identities.insert(associated_identities.end(), ASSOCIATED_IDENTITIES.begin(), ASSOCIATED_IDENTITIES.end());
  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPUS, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  MockCache::MockDeletePrivateIDs mock_req2;
  EXPECT_CALL(*_cache, create_DeletePrivateIDs(associated_identities, _))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice. 
  msg._free_on_delete = false;
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(associated_identities, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());

  _caught_fd_msg = NULL;
}

//
// Push Profile tests
//

TEST_F(HandlersTest, PushProfile)
{
  DigestAuthVector digest_av;
  digest_av.ha1 = "ha1";
  digest_av.realm = "realm";
  digest_av.qop = "qop";
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             IMS_SUBSCRIPTION,
                             AUTH_SESSION_STATE);
  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(ppr, &cfg);

  MockCache::MockPutAuthVector mock_req;
  EXPECT_CALL(*_cache, create_PutAuthVector(IMPI, _, _, 0))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus;
  impus.push_back(IMPU);
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice. 
  msg._free_on_delete = false;
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());

  _caught_fd_msg = NULL;
}
