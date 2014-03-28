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

// IMPORTANT for developers.
//
// The test cases in this file use both a real Diameter::Stack and a
// MockDiameterStack. We use the mock stack to catch diameter messages
// as the handlers send them out, and we use the real stack for
// everything else. This makes it difficult to keep track of who owns the
// underlying fd_msg structures and therefore who is responsible for freeing them.
//
// For tests where the handlers initiate the session by sending a request, we have
// to be careful that the request is freed after we catch it. This is sometimes done
// by simply calling fd_msg_free. However sometimes we want to look at the message and
// so we turn it back into a Cx message. This will trigger the caught fd_msg to be
// freed when we are finished with the Cx message.
//
// For tests where we initiate the session by sending in a request, we have to be
// careful that the request is only freed once. This can be an issue because the
// handlers build an answer from the request which references the request, and
// freeDiameter will then try to free the request when it frees the answer. We need
// to make sure that the request has not already been freed.

#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"
#include "test_interposer.hpp"
#include <curl/curl.h>

#include "mockdiameterstack.hpp"
#include "mockhttpstack.hpp"
#include "mockcache.hpp"
#include "mockhttpconnection.hpp"
#include "handlers.h"
#include "mockstatisticsmanager.hpp"
#include "fakelogger.hpp"
#include "sproutconnection.h"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Mock;

// Fixture for HandlersTest.
class HandlersTest : public testing::Test
{
public:
  FakeLogger _log;
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const std::string DEFAULT_SERVER_NAME;
  static const std::string SERVER_NAME;
  static const std::string IMPI;
  static const std::string IMPU;
  static std::vector<std::string> IMPU_IN_VECTOR;
  static std::vector<std::string> IMPI_IN_VECTOR;
  static const std::string IMS_SUBSCRIPTION;
  static const std::string REGDATA_RESULT;
  static const std::string REGDATA_RESULT_UNREG;
  static const std::string REGDATA_RESULT_DEREG;
  static const std::string REGDATA_BLANK_RESULT_DEREG;
  static const std::string VISITED_NETWORK;
  static const std::string AUTH_TYPE_DEREG;
  static const std::string AUTH_TYPE_CAPAB;
  static const ServerCapabilities CAPABILITIES;
  static const ServerCapabilities NO_CAPABILITIES;
  static const int32_t AUTH_SESSION_STATE;
  static const std::string ASSOCIATED_IDENTITY1;
  static const std::string ASSOCIATED_IDENTITY2;
  static std::vector<std::string> ASSOCIATED_IDENTITIES;
  static const std::string IMPU2;
  static const std::string IMPU3;
  static const std::string IMPU4;
  static std::vector<std::string> IMPUS;
  static std::vector<std::string> ASSOCIATED_IDENTITY1_IN_VECTOR;
  static std::vector<std::string> IMPU_REG_SET;
  static std::vector<std::string> IMPU3_REG_SET;
  static const std::string IMPU_IMS_SUBSCRIPTION;
  static const std::string IMPU3_IMS_SUBSCRIPTION;
  static const std::string SCHEME_UNKNOWN;
  static const std::string SCHEME_DIGEST;
  static const std::string SCHEME_AKA;
  static const std::string SIP_AUTHORIZATION;
  static const std::string HTTP_PATH_REG_TRUE;
  static const std::string HTTP_PATH_REG_FALSE;
  static std::vector<std::string> EMPTY_VECTOR;
  static const std::string DEREG_BODY_PAIRINGS;
  static const std::string DEREG_BODY_LIST;
  static const std::string DEREG_BODY_PAIRINGS2;
  static const std::string DEREG_BODY_LIST2;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;
  static MockCache* _cache;
  static MockHttpStack* _httpstack;
  static MockHttpConnection* _mock_http_conn;
  static SproutConnection* _sprout_conn;

  // Two mock stats managers, so we can choose whether to ignore stats or not.
  static NiceMock<MockStatisticsManager>* _nice_stats;
  static StrictMock<MockStatisticsManager>* _stats;

  // Used to catch diameter messages and transactions on the MockDiameterStack
  // so that we can inspect them.
  static struct msg* _caught_fd_msg;
  static Diameter::Transaction* _caught_diam_tsx;

  std::string test_str;
  int32_t test_i32;

  HandlersTest() {}
  virtual ~HandlersTest()
  {
    Mock::VerifyAndClear(_httpstack);
  }

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
    _mock_http_conn = new MockHttpConnection();
    _sprout_conn = new SproutConnection(_mock_http_conn);

    _stats = new StrictMock<MockStatisticsManager>;
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
    delete _sprout_conn; _sprout_conn = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
  }

  // We frequently invoke the following two methods on the send method of our
  // MockDiameterStack in order to catch the Diameter message we're trying
  // to send.
  static void store_msg_tsx(struct msg* msg, Diameter::Transaction* tsx)
  {
    _caught_fd_msg = msg;
    _caught_diam_tsx = tsx;
  }

  static void store_msg(struct msg* msg)
  {
    _caught_fd_msg = msg;
  }

  // Helper functions to build the expected json responses in our tests.
  static std::string build_digest_json(DigestAuthVector digest)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String(JSON_DIGEST_HA1.c_str());
    writer.String(digest.ha1.c_str());
    writer.EndObject();
    return sb.GetString();
  }

  static std::string build_av_json(DigestAuthVector av)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    // The qop value can be empty - in this case it should be replaced
    // with 'auth'.
    std::string qop_value = (!av.qop.empty()) ? av.qop : JSON_AUTH;

    writer.StartObject();
    {
      writer.String(JSON_DIGEST.c_str());
      writer.StartObject();
      {
        writer.String(JSON_HA1.c_str());
        writer.String(av.ha1.c_str());
        writer.String(JSON_REALM.c_str());
        writer.String(av.realm.c_str());
        writer.String(JSON_QOP.c_str());
        writer.String(qop_value.c_str());
      }
      writer.EndObject();
    }
    writer.EndObject();
    return sb.GetString();
  }

  static std::string build_aka_json(AKAAuthVector av)
  {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();
    {
      writer.String(JSON_AKA.c_str());
      writer.StartObject();
      {
        writer.String(JSON_CHALLENGE.c_str());
        writer.String(av.challenge.c_str());
        writer.String(JSON_RESPONSE.c_str());
        writer.String(av.response.c_str());
        writer.String(JSON_CRYPTKEY.c_str());
        writer.String(av.crypt_key.c_str());
        writer.String(JSON_INTEGRITYKEY.c_str());
        writer.String(av.integrity_key.c_str());
      }
      writer.EndObject();
    }
    writer.EndObject();
    return sb.GetString();
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

  void reg_data_template_with_deletion(std::string request_type,
                                       bool use_impi,
                                       RegistrationState db_regstate,
                                       int expected_type,
                                       int db_ttl = 3600,
                                       std::string expected_result = REGDATA_RESULT_DEREG,
                                       RegistrationState expected_new_state = RegistrationState::NOT_REGISTERED)
  {
    reg_data_template(request_type, use_impi, false, db_regstate, expected_type, db_ttl, expected_result, expected_new_state, true);
  }

  // Test function for the case where we have a HSS. Feeds a request
  // in to a handler, checks for a SAR, checks for a resulting
  // database delete or insert, and then verifies the response.
  void reg_data_template(std::string request_type,
                         bool use_impi,
                         bool new_binding,
                         RegistrationState db_regstate,
                         int expected_type,
                         int db_ttl = 3600,
                         std::string expected_result = REGDATA_RESULT,
                         RegistrationState expected_new_state = RegistrationState::REGISTERED,
                         bool expect_deletion = false)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/reg-data",
                               "",
                               use_impi ? "?private_id=" + IMPI : "",
                               "{\"reqtype\": \"" + request_type +"\"}",
                               htp_method_PUT);

    // Configure the handler to use a HSS, and send a RE_REGISTRATION
    // SAR to the HSS every hour.
    ImpuRegDataHandler::Config cfg(true, 3600);
    ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

    // Once the request is processed by the handler, we expect it to
    // look up the subscriber's data in Cassandra.
    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
    handler->run();

    // Have the cache return the values passed in for this test.
    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_registration_state(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(db_regstate), SetArgReferee<1>(db_ttl)));

    // If we have the new_binding flag set, return a list of associated IMPIs
    // not containing the IMPI specified on the request. Add IMPI to the list
    // if this isn't a new binding.
    std::vector<std::string> associated_identities = ASSOCIATED_IDENTITIES;
    if (!new_binding)
    {
      associated_identities.push_back(IMPI);
    }
    EXPECT_CALL(mock_req, get_associated_impis(_))
      .WillRepeatedly(SetArgReferee<0>(associated_identities));

    // If this is a new binding, we expect to put the new associated
    // IMPI in the cache.
    MockCache::MockPutAssociatedPrivateID mock_req2;
    if (new_binding)
    {
      EXPECT_CALL(*_cache, create_PutAssociatedPrivateID(IMPU_REG_SET, IMPI, _, 7200))
        .WillOnce(Return(&mock_req2));
      EXPECT_CALL(*_cache, send(_, &mock_req2))
        .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));
    }

    // A Server-Assignment-Request should be generated for this
    // test. Check for it, and check that its properties are as expected.
    EXPECT_CALL(*_mock_stack, send(_, _, 200))
      .Times(1)
      .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

    t->on_success(&mock_req);

    if ((new_binding) && (use_impi))
    {
      t = mock_req2.get_trx();
      ASSERT_FALSE(t == NULL);
    }
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

    // Check that the SAR has the type expected by the caller.
    EXPECT_EQ(expected_type, test_i32);

    Cx::ServerAssignmentAnswer saa(_cx_dict,
                                   _mock_stack,
                                   DIAMETER_SUCCESS,
                                   IMPU_IMS_SUBSCRIPTION);

    if (!expect_deletion)
    {
      // This is a request where we expect the database to be
      // updated - check that it is updated with the expected state
      // for this test, and that the TTL is twice the configured HSS
      // RE_REGISTRATION time of 3600 seconds.

      // Once we simulate the Diameter response, check that the
      // database is updated and a 200 OK is sent.
      MockCache::MockPutIMSSubscription mock_req3;
      EXPECT_CALL(*_cache, create_PutIMSSubscription(IMPU_REG_SET, IMPU_IMS_SUBSCRIPTION, expected_new_state, IMPI_IN_VECTOR, _, 7200))
        .WillOnce(Return(&mock_req3));
      EXPECT_CALL(*_cache, send(_, &mock_req3))
        .WillOnce(WithArgs<0>(Invoke(&mock_req3, &Cache::Request::set_trx)));

      EXPECT_CALL(*_httpstack, send_reply(_, 200));
      _caught_diam_tsx->on_response(saa);

      t = mock_req3.get_trx();
      ASSERT_FALSE(t == NULL);
    }
    else
    {
      // This is a request where we expect fields to be deleted from
      // the database after the SAA. Check that they are, and that a
      // 200 OK HTTP response is sent.

      MockCache::MockDeletePublicIDs mock_req3;
      EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU_REG_SET, IMPI_IN_VECTOR, _))
        .WillOnce(Return(&mock_req3));
      EXPECT_CALL(*_cache, send(_, &mock_req3))
        .WillOnce(WithArgs<0>(Invoke(&mock_req3, &Cache::Request::set_trx)));

      EXPECT_CALL(*_httpstack, send_reply(_, 200));
      _caught_diam_tsx->on_response(saa);

      t = mock_req3.get_trx();
      ASSERT_FALSE(t == NULL);
    }

    // Build the expected response and check it's correct
    EXPECT_EQ(expected_result, req.content());

    _caught_fd_msg = NULL;
    delete _caught_diam_tsx; _caught_diam_tsx = NULL;
  }

  // Test function for the case where we have a HSS, but we're making a
  // request that doesn't require a SAR or database hit. Feeds a request
  // in to a handler and then verifies the response.
  void reg_data_template_no_sar(std::string request_type,
                                bool use_impi,
                                RegistrationState db_regstate,
                                int db_ttl = 3600,
                                std::string expected_result = REGDATA_RESULT)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/reg-data",
                               "",
                               use_impi ? "?private_id=" + IMPI : "",
                               "{\"reqtype\": \"" + request_type +"\"}",
                               htp_method_PUT);

    // Configure the handler to use a HSS, and send a RE_REGISTRATION
    // SAR to the HSS every hour.
    ImpuRegDataHandler::Config cfg(true, 3600);
    ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

    // Once the request is processed by the handler, we expect it to
    // look up the subscriber's data in Cassandra.
    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
    handler->run();

    // Have the cache return the values passed in for this test.
    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_registration_state(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(db_regstate), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_associated_impis(_))
            .WillRepeatedly(SetArgReferee<0>(IMPI_IN_VECTOR));

    // No SAR was generated, so there shouldn't be any effect on the
    // database - just check that we send back a response.
    EXPECT_CALL(*_httpstack, send_reply(_, 200));
    t->on_success(&mock_req);

    // Build the expected response and check it's correct
    EXPECT_EQ(expected_result, req.content());
  }

  // Test function for the case where we have a HSS, and we're making a
  // request that should read from the database and generate a
  // Server-Assignment-Request, but not update the database.
  void reg_data_template_no_write(std::string body,
                                  bool use_impi,
                                  bool hss_configured,
                                  RegistrationState db_regstate,
                                  int expected_type,
                                  int db_ttl = 3600,
                                  std::string expected_result = REGDATA_RESULT)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/reg-data",
                               "",
                               use_impi ? "?private_id=" + IMPI : "",
                               "{\"reqtype\": \"" + body +"\"}",
                               htp_method_PUT);
    ImpuRegDataHandler::Config cfg(hss_configured, 3600);
    ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
    handler->run();

    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_registration_state(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(db_regstate), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_associated_impis(_))
          .WillRepeatedly(SetArgReferee<0>(IMPI_IN_VECTOR));

    if (hss_configured)
    {
      EXPECT_CALL(*_mock_stack, send(_, _, 200))
        .Times(1)
        .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
    }
    else
    {
      EXPECT_CALL(*_httpstack, send_reply(_, 200));
    }

    t->on_success(&mock_req);

    if (hss_configured)
    {
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
      EXPECT_EQ(expected_type, test_i32);

      Cx::ServerAssignmentAnswer saa(_cx_dict,
                                     _mock_stack,
                                     DIAMETER_SUCCESS,
                                     IMPU_IMS_SUBSCRIPTION);

      EXPECT_CALL(*_httpstack, send_reply(_, 200));
      _caught_diam_tsx->on_response(saa);
    }

    // Build the expected response and check it's correct
    EXPECT_EQ(expected_result, req.content());
    _caught_fd_msg = NULL;
    delete _caught_diam_tsx; _caught_diam_tsx = NULL;
  }

  void reg_data_template_no_hss(std::string body,
                                bool use_impi,
                                RegistrationState db_regstate,
                                int db_ttl = 3600,
                                std::string expected_result = REGDATA_RESULT,
                                bool expect_update = false,
                                RegistrationState expected_new_state = RegistrationState::REGISTERED)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/reg-data",
                               "",
                               use_impi ? "?private_id=" + IMPI : "",
                               "{\"reqtype\": \"" + body +"\"}",
                               htp_method_PUT);

    // Configure the handler to use a HSS, and send a RE_REGISTRATION
    // SAR to the HSS every hour.

    ImpuRegDataHandler::Config cfg(false, 3600);
    ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

    // Once the request is processed by the handler, we expect it to
    // look up the subscriber's data in Cassandra.
    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
    handler->run();

    // Have the cache return the values passed in for this test.
    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_registration_state(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(db_regstate), SetArgReferee<1>(db_ttl)));
    EXPECT_CALL(mock_req, get_associated_impis(_));

    if (expect_update)
    {
      // If we expect the subscriber's registration state to be updated,
      // check that an appropriate database write is made.
      MockCache::MockPutIMSSubscription mock_req2;
      std::vector<std::string> no_associated_impis;

      // We should never set a TTL in the non-HSS case
      EXPECT_CALL(*_cache, create_PutIMSSubscription(IMPU_REG_SET,
                                                     IMPU_IMS_SUBSCRIPTION,
                                                     expected_new_state,
                                                     no_associated_impis,
                                                     _,
                                                     0))
        .WillOnce(Return(&mock_req2));
      EXPECT_CALL(*_cache, send(_, &mock_req2))
        .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

      EXPECT_CALL(*_httpstack, send_reply(_, 200));
      t->on_success(&mock_req);

      t = mock_req2.get_trx();
      ASSERT_FALSE(t == NULL);
    }
    else
    {
      // If we're not expecting a database update, just check that we
      // return 200 OK.
      EXPECT_CALL(*_httpstack, send_reply(_, 200));
      t->on_success(&mock_req);
    }
    // Build the expected response and check it's correct
    EXPECT_EQ(expected_result, req.content());
  }

  void reg_data_template_invalid_dereg(bool hss_configured)
  {
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/reg-data",
                               "",
                               "?private_id=" + IMPI,
                               "{\"reqtype\": \"dereg-user\"}",
                               htp_method_PUT);
    ImpuRegDataHandler::Config cfg(hss_configured, 3600);
    ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
    handler->run();

    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
    EXPECT_CALL(mock_req, get_associated_impis(_))
      .WillRepeatedly(SetArgReferee<0>(IMPI_IN_VECTOR));

    // Ensure that the database returns NOT_REGISTERED.
    EXPECT_CALL(mock_req, get_registration_state(_, _))
      .WillRepeatedly(SetArgReferee<0>(RegistrationState::NOT_REGISTERED));

    // A 400 error should be sent as a result.
    EXPECT_CALL(*_httpstack, send_reply(_, 400));
    t->on_success(&mock_req);

    EXPECT_EQ("", req.content());

    _caught_diam_tsx = NULL;
    _caught_fd_msg = NULL;
  }

  // Template functions to test our processing when various error codes are returned by the HSS
  // from UARs and LIRs.
  static void registration_status_error_template(int32_t hss_rc, int32_t hss_experimental_rc, int32_t http_rc)
  {
    // Build the HTTP request which will invoke a UAR to be sent to the HSS.
    MockHttpStack::Request req(_httpstack,
                               "/impi/" + IMPI + "/",
                               "registration-status",
                               "?impu=" + IMPU);

    ImpiRegistrationStatusHandler::Config cfg(true);
    ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

    // Once the handler's run function is called, expect a diameter message to be
    // sent. We don't bother checking the diameter message is as expected here. This
    // is done by other tests.
    EXPECT_CALL(*_mock_stack, send(_, _, 200))
      .Times(1)
      .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
    handler->run();
    ASSERT_FALSE(_caught_diam_tsx == NULL);

    // Create a response with the correct return code set and expect an HTTP response
    // with the correct return code.
    Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                    _mock_stack,
                                    hss_rc,
                                    hss_experimental_rc,
                                    "",
                                    NO_CAPABILITIES);
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc));
    _caught_diam_tsx->on_response(uaa);
    fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
    delete _caught_diam_tsx; _caught_diam_tsx = NULL;

    // Ensure that the HTTP body on the response is empty.
    EXPECT_EQ("", req.content());
  }

  static void location_info_error_template(int32_t hss_rc, int32_t hss_experimental_rc, int32_t http_rc)
  {
    // Build the HTTP request which will invoke an LIR to be sent to the HSS.
    MockHttpStack::Request req(_httpstack,
                               "/impu/" + IMPU + "/",
                               "location",
                               "");

    ImpuLocationInfoHandler::Config cfg(true);
    ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);

    // Once the handler's run function is called, expect a diameter message to be
    // sent. We don't bother checking the diameter message is as expected here. This
    // is done by other tests.
    EXPECT_CALL(*_mock_stack, send(_, _, 200))
      .Times(1)
      .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
    handler->run();
    ASSERT_FALSE(_caught_diam_tsx == NULL);

    // Create a response with the correct return code set and expect an HTTP response
    // with the correct return code.
    Cx::LocationInfoAnswer lia(_cx_dict,
                               _mock_stack,
                               hss_rc,
                               hss_experimental_rc,
                               "",
                               NO_CAPABILITIES);
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc));
    _caught_diam_tsx->on_response(lia);
    fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
    delete _caught_diam_tsx; _caught_diam_tsx = NULL;

    // Ensure that the HTTP body on the response is empty.
    EXPECT_EQ("", req.content());
  }

  void rtr_template(int32_t dereg_reason,
                    std::string http_path,
                    std::string body,
                    HTTPCode http_ret_code)
  {
    // This is a template function for an RTR test.
    Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                           _mock_stack,
                                           dereg_reason,
                                           IMPI,
                                           ASSOCIATED_IDENTITIES,
                                           IMPUS,
                                           AUTH_SESSION_STATE);

    // The free_on_delete flag controls whether we want to free the underlying
    // fd_msg structure when we delete this RTR. We don't, since this will be
    // freed when the answer is freed later in the test. If we leave this flag set
    // then the request will be freed twice.
    rtr._free_on_delete = false;

    RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
    RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

    // We have to make sure the message is pointing at the mock stack.
    handler->_msg._stack = _mock_stack;

    // Once the handler's run function is called, we expect a cache request for
    // the IMS subscription of the final public identity in IMPUS.
    MockCache::MockGetIMSSubscription mock_req;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU2))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

    handler->run();

    // The cache successfully returns the correct IMS subscription.
    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU3_IMS_SUBSCRIPTION), SetArgReferee<1>(0)));

    // Expect another cache request for the IMS subscription of the next
    // public identity in IMPUS.
    MockCache::MockGetIMSSubscription mock_req2;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req2));
    EXPECT_CALL(*_cache, send(_, &mock_req2))
      .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

    t->on_success(&mock_req);

    // The cache successfully returns the correct IMS subscription.
    t = mock_req2.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req2, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(0)));

    // Expect a delete to be sent to Sprout.
    EXPECT_CALL(*_mock_http_conn, send_delete(http_path, body, 0))
      .Times(1)
      .WillOnce(Return(http_ret_code));

    // Expect to receive a diameter message.
    EXPECT_CALL(*_mock_stack, send(_))
      .Times(1)
      .WillOnce(WithArgs<0>(Invoke(store_msg)));

    // We also expect to receive cache requests for each registration set. Catch these.
    std::vector<std::string> impis{IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
    MockCache::MockDissociateImplicitRegistrationSetFromImpi mock_req3;
    EXPECT_CALL(*_cache, create_DissociateImplicitRegistrationSetFromImpi(IMPU_REG_SET, impis, _))
      .WillOnce(Return(&mock_req3));
    EXPECT_CALL(*_cache, send(_, &mock_req3))
      .WillOnce(WithArgs<0>(Invoke(&mock_req3, &Cache::Request::set_trx)));

    MockCache::MockDissociateImplicitRegistrationSetFromImpi mock_req4;
    EXPECT_CALL(*_cache, create_DissociateImplicitRegistrationSetFromImpi(IMPU3_REG_SET, impis, _))
      .WillOnce(Return(&mock_req4));
    EXPECT_CALL(*_cache, send(_, &mock_req4))
      .WillOnce(WithArgs<0>(Invoke(&mock_req4, &Cache::Request::set_trx)));

    t->on_success(&mock_req2);

    // Turn the caught Diameter msg structure into a RTA and confirm it's contents.
    Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
    Cx::RegistrationTerminationAnswer rta(msg);
    EXPECT_TRUE(rta.result_code(test_i32));
    if (http_ret_code == HTTP_OK)
    {
      EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
    }
    else
    {
      EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
    }
    EXPECT_EQ(impis, rta.associated_identities());
    EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());

    // Check the cache requests have transactions.
    t = mock_req3.get_trx();
    ASSERT_FALSE(t == NULL);
    t = mock_req4.get_trx();
    ASSERT_FALSE(t == NULL);
  }

  void rtr_template_no_impus(int32_t dereg_reason,
                             std::string http_path,
                             std::string body)
  {
    // This is a template function for an RTR test where no public identities
    // are specified on the request.
    Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                           _mock_stack,
                                           dereg_reason,
                                           IMPI,
                                           ASSOCIATED_IDENTITIES,
                                           EMPTY_VECTOR,
                                           AUTH_SESSION_STATE);

    // The free_on_delete flag controls whether we want to free the underlying
    // fd_msg structure when we delete this RTR. We don't, since this will be
    // freed when the answer is freed later in the test. If we leave this flag set
    // then the request will be freed twice.
    rtr._free_on_delete = false;

    RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
    RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

    // We have to make sure the message is pointing at the mock stack.
    handler->_msg._stack = _mock_stack;

    // No public identities, so once the handler's run function is called, we
    // expect a cache request for associated default public identities.
    std::vector<std::string> impis{IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
    MockCache::MockGetAssociatedPrimaryPublicIDs mock_req;
    EXPECT_CALL(*_cache, create_GetAssociatedPrimaryPublicIDs(impis))
      .WillOnce(Return(&mock_req));
    EXPECT_CALL(*_cache, send(_, &mock_req))
      .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

    handler->run();

    // The cache successfully returns a list of public identities.
    Cache::Transaction* t = mock_req.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req, get_result(_))
      .WillRepeatedly(SetArgReferee<0>(IMPUS));

    // Next expect a cache request for the IMS subscription of the final
    // public identity in IMPUS.
    MockCache::MockGetIMSSubscription mock_req2;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
      .WillOnce(Return(&mock_req2));
    EXPECT_CALL(*_cache, send(_, &mock_req2))
      .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

    t->on_success(&mock_req);

    // The cache successfully returns the correct IMS subscription.
    t = mock_req2.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req2, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION), SetArgReferee<1>(0)));

    // Sometimes we're interested in the associated identities returned by
    // this cache request.
    if ((dereg_reason == SERVER_CHANGE) ||
        (dereg_reason == NEW_SERVER_ASSIGNED))
    {
      EXPECT_CALL(mock_req2, get_associated_impis(_))
        .WillRepeatedly(SetArgReferee<0>(ASSOCIATED_IDENTITIES));
    }

    // Expect another cache request for the IMS subscription of the next
    // public identity in IMPUS.
    MockCache::MockGetIMSSubscription mock_req3;
    EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU2))
      .WillOnce(Return(&mock_req3));
    EXPECT_CALL(*_cache, send(_, &mock_req3))
      .WillOnce(WithArgs<0>(Invoke(&mock_req3, &Cache::Request::set_trx)));

    t->on_success(&mock_req2);

    // The cache successfully returns the correct IMS subscription.
    t = mock_req3.get_trx();
    ASSERT_FALSE(t == NULL);
    EXPECT_CALL(mock_req3, get_xml(_, _))
      .WillRepeatedly(DoAll(SetArgReferee<0>(IMPU3_IMS_SUBSCRIPTION), SetArgReferee<1>(0)));

    // Sometimes we're interested in the associated identities returned by
    // this cache request.
    if ((dereg_reason == SERVER_CHANGE) ||
        (dereg_reason == NEW_SERVER_ASSIGNED))
    {
      EXPECT_CALL(mock_req3, get_associated_impis(_))
        .WillRepeatedly(SetArgReferee<0>(ASSOCIATED_IDENTITIES));
    }

    // Expect a delete to be sent to Sprout.
    EXPECT_CALL(*_mock_http_conn, send_delete(http_path, body, 0))
      .Times(1)
      .WillOnce(Return(HTTP_OK));

    // Expect to receive a diameter message.
    EXPECT_CALL(*_mock_stack, send(_))
      .Times(1)
      .WillOnce(WithArgs<0>(Invoke(store_msg)));

    // We also expect to receive cache requests for each registration set. Catch these.
    MockCache::MockDissociateImplicitRegistrationSetFromImpi mock_req4;
    EXPECT_CALL(*_cache, create_DissociateImplicitRegistrationSetFromImpi(IMPU_REG_SET, impis, _))
      .WillOnce(Return(&mock_req4));
    EXPECT_CALL(*_cache, send(_, &mock_req4))
      .WillOnce(WithArgs<0>(Invoke(&mock_req4, &Cache::Request::set_trx)));

    MockCache::MockDissociateImplicitRegistrationSetFromImpi mock_req5;
    EXPECT_CALL(*_cache, create_DissociateImplicitRegistrationSetFromImpi(IMPU3_REG_SET, impis, _))
      .WillOnce(Return(&mock_req5));
    EXPECT_CALL(*_cache, send(_, &mock_req5))
      .WillOnce(WithArgs<0>(Invoke(&mock_req5, &Cache::Request::set_trx)));

    // Sometimes we want to delete entire IMPI rows.
    MockCache::MockDeleteIMPIMapping mock_req6;
    if ((dereg_reason == SERVER_CHANGE) ||
        (dereg_reason == NEW_SERVER_ASSIGNED))
    {
      EXPECT_CALL(*_cache, create_DeleteIMPIMapping(impis, _))
        .WillOnce(Return(&mock_req6));
      EXPECT_CALL(*_cache, send(_, &mock_req6))
        .WillOnce(WithArgs<0>(Invoke(&mock_req6, &Cache::Request::set_trx)));
    }

    t->on_success(&mock_req3);

    // Turn the caught Diameter msg structure into a RTA and confirm it's contents.
    Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
    Cx::RegistrationTerminationAnswer rta(msg);
    EXPECT_TRUE(rta.result_code(test_i32));
    EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
    EXPECT_EQ(impis, rta.associated_identities());
    EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());

    // Check the cache requests have transactions.
    t = mock_req4.get_trx();
    ASSERT_FALSE(t == NULL);
    t = mock_req5.get_trx();
    ASSERT_FALSE(t == NULL);
    if ((dereg_reason == SERVER_CHANGE) ||
        (dereg_reason == NEW_SERVER_ASSIGNED))
    {
      t = mock_req6.get_trx();
      ASSERT_FALSE(t == NULL);
    }
  }

  static void ignore_stats(bool ignore)
  {
    if (ignore)
    {
      Mock::VerifyAndClear(_stats);
      HssCacheHandler::configure_stats(_nice_stats);
    }
    else
    {
      HssCacheHandler::configure_stats(_stats);
    }
  }
};

const std::string HandlersTest::DEST_REALM = "dest-realm";
const std::string HandlersTest::DEST_HOST = "dest-host";
const std::string HandlersTest::DEFAULT_SERVER_NAME = "sprout";
const std::string HandlersTest::SERVER_NAME = "scscf";
const std::string HandlersTest::IMPI = "_impi@example.com";
const std::string HandlersTest::IMPU = "sip:impu@example.com";
const std::string HandlersTest::IMPU2 = "sip:impu2@example.com";
const std::string HandlersTest::IMPU3 = "sip:impu3@example.com";
const std::string HandlersTest::IMPU4 = "sip:impu4@example.com";
const std::string HandlersTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HandlersTest::REGDATA_RESULT = "<ClearwaterRegData>\n\t<RegistrationState>REGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HandlersTest::REGDATA_RESULT_DEREG = "<ClearwaterRegData>\n\t<RegistrationState>NOT_REGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HandlersTest::REGDATA_BLANK_RESULT_DEREG = "<ClearwaterRegData>\n\t<RegistrationState>NOT_REGISTERED</RegistrationState>\n</ClearwaterRegData>\n\n";
const std::string HandlersTest::REGDATA_RESULT_UNREG = "<ClearwaterRegData>\n\t<RegistrationState>UNREGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HandlersTest::VISITED_NETWORK = "visited-network.com";
const std::string HandlersTest::AUTH_TYPE_DEREG = "DEREG";
const std::string HandlersTest::AUTH_TYPE_CAPAB = "CAPAB";
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities HandlersTest::CAPABILITIES(mandatory_capabilities, optional_capabilities);
const ServerCapabilities HandlersTest::NO_CAPABILITIES(no_capabilities, no_capabilities);
const int32_t HandlersTest::AUTH_SESSION_STATE = 1;
const std::string HandlersTest::ASSOCIATED_IDENTITY1 = "associated_identity1@example.com";
const std::string HandlersTest::ASSOCIATED_IDENTITY2 = "associated_identity2@example.com";
std::vector<std::string> HandlersTest::ASSOCIATED_IDENTITIES = {ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
std::vector<std::string> HandlersTest::IMPUS = {IMPU, IMPU2};
std::vector<std::string> HandlersTest::IMPU_IN_VECTOR = {IMPU};
std::vector<std::string> HandlersTest::IMPI_IN_VECTOR = {IMPI};
std::vector<std::string> HandlersTest::ASSOCIATED_IDENTITY1_IN_VECTOR = {ASSOCIATED_IDENTITY1};
std::vector<std::string> HandlersTest::IMPU_REG_SET = {IMPU, IMPU4};
std::vector<std::string> HandlersTest::IMPU3_REG_SET = {IMPU3, IMPU2};
const std::string HandlersTest::IMPU_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU4 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HandlersTest::IMPU3_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU3 + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
std::vector<std::string> HandlersTest::EMPTY_VECTOR = {};
const std::string HandlersTest::DEREG_BODY_PAIRINGS = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HandlersTest::DEREG_BODY_LIST = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\"},{\"primary-impu\":\"" + IMPU + "\"}]}";
// These are effectively the same as above, but depending on the exact code path the ordering of IMPUS can be different.
const std::string HandlersTest::DEREG_BODY_PAIRINGS2 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HandlersTest::DEREG_BODY_LIST2 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\"},{\"primary-impu\":\"" + IMPU3 + "\"}]}";
const std::string HandlersTest::SCHEME_UNKNOWN = "Unknwon";
const std::string HandlersTest::SCHEME_DIGEST = "SIP Digest";
const std::string HandlersTest::SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string HandlersTest::SIP_AUTHORIZATION = "Authorization";

const std::string HandlersTest::HTTP_PATH_REG_TRUE = "/registrations?send-notifications=true";
const std::string HandlersTest::HTTP_PATH_REG_FALSE = "/registrations?send-notifications=false";

Diameter::Stack* HandlersTest::_real_stack = NULL;
MockDiameterStack* HandlersTest::_mock_stack = NULL;
Cx::Dictionary* HandlersTest::_cx_dict = NULL;
MockCache* HandlersTest::_cache = NULL;
MockHttpStack* HandlersTest::_httpstack = NULL;
MockHttpConnection* HandlersTest::_mock_http_conn = NULL;
SproutConnection* HandlersTest::_sprout_conn = NULL;
NiceMock<MockStatisticsManager>* HandlersTest::_nice_stats = NULL;
StrictMock<MockStatisticsManager>* HandlersTest::_stats = NULL;
struct msg* HandlersTest::_caught_fd_msg = NULL;
Diameter::Transaction* HandlersTest::_caught_diam_tsx = NULL;

//
// Ping test
//

TEST_F(HandlersTest, SimpleMainline)
{
  MockHttpStack::Request req(_httpstack, "/", "ping");
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  PingHandler* handler = new PingHandler(req);
  handler->run();
  EXPECT_EQ("OK", req.content());
}

//
// Digest and AV tests
//

TEST_F(HandlersTest, DigestCache)
{
  // This test tests an Impi Digest handler case where no HSS is configured.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup an auth
  // vector for the specified public and private IDs.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  // Confirm the cache transaction is not NULL, and specify an auth vector
  // to be returned on the expected call for the cache request's results.
  // We also expect a successful HTTP response.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // When the cache result returns the handler gets the digest result, and sends
  // an HTTP reply.
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(digest));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct.
  EXPECT_EQ(build_digest_json(digest), req.content());
}

TEST_F(HandlersTest, DigestCacheFailure)
{
  // This test tests an Impi Digest handler case where no HSS is configured, and
  // the cache request fails. Start by building the HTTP request which will
  // invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup an auth
  // vector for the specified public and private IDs.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm that the cache transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Expect a 502 HTTP response once the cache returns an error to the handler.
  EXPECT_CALL(*_httpstack, send_reply(_, 502));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
}

TEST_F(HandlersTest, DigestHSS)
{
  // This test tests an Impi Digest handler case with an HSS configured.
  // Start by building the HTTP request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_DIGEST, mar.sip_auth_scheme());
  EXPECT_EQ("", mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(DEFAULT_SERVER_NAME, test_str);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // Once it receives the MAA, check that the handler tries to add the public ID
  // to the database and that a successful HTTP response is sent.
  MockCache::MockPutAssociatedPublicID mock_req;
  EXPECT_CALL(*_cache, create_PutAssociatedPublicID(IMPI, IMPU,  _, 300))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(maa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Confirm the cache transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected response and check it's correct.
  EXPECT_EQ(build_digest_json(digest), req.content());
}

TEST_F(HandlersTest, DigestHSSTimeout)
{
  // This test tests an Impi Digest handler case with an HSS configured.
  // Start by building the HTTP request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_DIGEST, mar.sip_auth_scheme());
  EXPECT_EQ("", mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(DEFAULT_SERVER_NAME, test_str);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  EXPECT_CALL(*_httpstack, send_reply(_, 503));
  _caught_diam_tsx->on_timeout();
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, DigestHSSNoIMPU)
{
  // This test tests an Impi Digest handler case with an HSS configured, but
  // no public ID is specified on the HTTP request. Start by building the
  // HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "");

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to look for associated
  // public IDs in the cache.
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(IMPI))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL, and specify a list of IMPUS to be returned on
  // the expected call for the cache request's results.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  std::vector<std::string> impus = {IMPU, "another_impu"};
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(impus));

  // Once the cache transaction's on_success callback is called, expect a
  // diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  t->on_success(&mock_req);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_DIGEST, mar.sip_auth_scheme());
  EXPECT_EQ("", mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(DEFAULT_SERVER_NAME, test_str);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // Once it receives the MAA, check that the handler tries to add the public
  // ID to the database, and that a successful HTTP response is sent.
  MockCache::MockPutAssociatedPublicID mock_req2;
  EXPECT_CALL(*_cache, create_PutAssociatedPublicID(IMPI, IMPU,  _, 300))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(maa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Confirm the cache transaction is not NULL.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected response and check it's correct.
  EXPECT_EQ(build_digest_json(digest), req.content());
}

TEST_F(HandlersTest, DigestHSSUserUnknown)
{
  // This test tests an Impi Digest handler case with an HSS configured, but
  // the HSS returns a user unknown error. Start by building the HTTP
  // request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  // We don't bother checking the contents of this message since this is done in
  // previous tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_ERROR_USER_UNKNOWN,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // Once the handler recieves the MAA, expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(maa);
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, DigestHSSOtherError)
{
  // This test tests an Impi Digest handler case with an HSS configured, but
  // the HSS returns an error. Start by building the HTTP
  // request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  // We don't bother checking the contents of this message since this is done in
  // previous tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               0,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // Once the handler recieves the MAA, expect a 500 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 500));
  _caught_diam_tsx->on_response(maa);
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, DigestHSSUnkownScheme)
{
  // This test tests an Impi Digest handler case with an HSS configured, but
  // the HSS returns an unknown scheme. Start by building the HTTP
  // request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  // We don't bother checking the contents of this message since this is done in
  // previous tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA with scheme unknown.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_UNKNOWN,
                               digest,
                               aka);

  // Once the handler recieves the MAA, expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(maa);
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, DigestHSSAKAReturned)
{
  // This test tests an Impi Digest handler case with an HSS configured, but
  // the HSS returns an AKA scheme rather than a digest. Start by building the HTTP
  // request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  // We don't bother checking the contents of this message since this is done in
  // previous tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  // Build an MAA with an AKA scheme.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_AKA,
                               digest,
                               aka);

  // Once the handler recieves the MAA, expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(maa);
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, DigestNoCachedIMPUs)
{
  // This test tests an Impi Digest handler case where no public ID is specified
  // on the HTTP request, and the cache returns an empty list. Start by building the HTTP
  // request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "");
  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to look for associated
  // public IDs in the cache.
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(IMPI))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL, and specify an empty list of IMPUs to be
  // returned on the expected call for the cache request's results.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(EMPTY_VECTOR));

  // Expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  t->on_success(&mock_req);
}

TEST_F(HandlersTest, DigestIMPUNotFound)
{
  // This test tests an Impi Digest handler case where no public ID is specified
  // on the HTTP request, and none can be found in the cache. Start by building the HTTP
  // request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "");

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to look for associated
  // public IDs in the cache.
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(IMPI))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Once the cache transaction's failure callback is called, expect a 404 HTTP
  // response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
}

TEST_F(HandlersTest, DigestNoIMPUCacheFailure)
{
  // This test tests an Impi Digest handler case where no public ID is specified
  // on the HTTP request, the cache request fails. Start by building the HTTP
  // request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "");

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect to look for associated
  // public IDs in the cache.
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(IMPI))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Once the cache transaction's failure callback is called, expect a 502 HTTP
  // response.
  EXPECT_CALL(*_httpstack, send_reply(_, 502));
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::UNKNOWN_ERROR, error_text);
}

TEST_F(HandlersTest, AvCache)
{
  // This test tests an Impi Av handler case where no HSS is configured.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "av",
                             "?impu=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiAvHandler* handler = new ImpiAvHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup an auth
  // vector for the specified public and private IDs.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  // Confirm the cache transaction is not NULL, and specify an auth vector
  // to be returned on the expected call for the cache request's results.
  // We also expect a successful HTTP response.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(digest));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));

  t->on_success(&mock_req);

  // Build the expected response and check it's correct.
  EXPECT_EQ(build_av_json(digest), req.content());
}

TEST_F(HandlersTest, AvEmptyQoP)
{
  // This test tests an Impi Av handler case where no HSS is configured.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "av",
                             "?impu=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiAvHandler* handler = new ImpiAvHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup an auth
  // vector for the specified public and private IDs.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Set up the returned auth vector to have qop set to the empty string.
  // Homestead should convert this to auth.
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "";

  // Confirm the cache transaction is not NULL, and specify an auth vector
  // to be returned on the expected call for the cache request's results.
  // We also expect a successful HTTP response.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(digest));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));

  t->on_success(&mock_req);

  // Build the expected response and check it's correct.
  EXPECT_EQ(build_av_json(digest), req.content());
}

TEST_F(HandlersTest, AvNoPublicIDHSSAKA)
{
  // This test tests an Impi Av handler case with an HSS configured and no
  // public IDs specified on the HTTP request. Start by building the HTTP
  // request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "av",
                             "?autn=" + SIP_AUTHORIZATION);
  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiAvHandler* handler = new ImpiAvHandler(req, &cfg);

  // Once the handler's run function is called, expect to look for associated
  // public IDs in the cache.
  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(IMPI))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL, and specify a list of IMPUS to be returned on
  // the expected call for the cache request's results.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  std::vector<std::string> impus = {IMPU, "another_impu"};
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(impus));

  // Once the cache transaction's on_success callback is called, expect a
  // diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  t->on_success(&mock_req);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into an MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_UNKNOWN, mar.sip_auth_scheme());
  EXPECT_EQ(SIP_AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(DEFAULT_SERVER_NAME, test_str);

  DigestAuthVector digest;
  AKAAuthVector aka;
  aka.challenge = "challenge";
  aka.response = "response";
  aka.crypt_key = "crypt_key";
  aka.integrity_key = "integrity_key";

  // Build an MAA with an AKA scheme specified.
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_AKA,
                               digest,
                               aka);

  // Once it receives the MAA, check that a successful HTTP response is sent.
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(maa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected response and check it's correct. We need to first
  // encode the values we sent earlier into base64 or hex. This is hardcoded.
  AKAAuthVector encoded_aka;
  encoded_aka.challenge = "Y2hhbGxlbmdl";
  encoded_aka.response = "726573706f6e7365";
  encoded_aka.crypt_key = "63727970745f6b6579";
  encoded_aka.integrity_key = "696e746567726974795f6b6579";
  EXPECT_EQ(build_aka_json(encoded_aka), req.content());
}

TEST_F(HandlersTest, AuthInvalidScheme)
{
  // This test tests an Impi Av handler case with an invalid scheme on the HTTP
  // request. Start by building the HTTP request.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "invalid",
                             "");

  ImpiHandler::Config cfg(true);
  ImpiAvHandler* handler = new ImpiAvHandler(req, &cfg);

  // Once the handler's run function is called, expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  handler->run();
}

TEST_F(HandlersTest, AkaNoIMPU)
{
  // This test tests an Impi Av handler case with AKA specified on the HTTP
  // request, but no public ID. Start by building the HTTP request.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "aka",
                             "");

  ImpiHandler::Config cfg(true);
  ImpiAvHandler* handler = new ImpiAvHandler(req, &cfg);

  // Once the handler's run function is called, expect a 404 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  std::string error_text = "error";
  handler->run();
}

//
// IMS Subscription tests
//

//
// This set of tests covers the various types of request that can be
// made to the IMS subscription handler. It simulates a HSS and
// verifies that both the Cx flows and the Cassandra flows are correct.
//

// Initial registration

TEST_F(HandlersTest, IMSSubscriptionHSS_InitialRegister)
{
  reg_data_template("reg", true, false, RegistrationState::NOT_REGISTERED, 1);
}

// Initial registration from UNREGISTERED state

TEST_F(HandlersTest, IMSSubscriptionHSS_InitialRegisterFromUnreg)
{
  reg_data_template("reg", true, false, RegistrationState::UNREGISTERED, 1);
}

// Re-registration when the database record is old enough (500s - less
// than the configured 3600s) to trigger a new SAR.

TEST_F(HandlersTest, IMSSubscriptionHSS_ReregWithSAR)
{
  reg_data_template("reg", true, false, RegistrationState::REGISTERED, 2, 500);
}

// Re-registration with a new binding.

TEST_F(HandlersTest, IMSSubscriptionHSS_ReregNewBinding)
{
  reg_data_template("reg", true, true, RegistrationState::REGISTERED, 1);
}

// Re-registration when the database record is not old enough to
// trigger a new SAR.

TEST_F(HandlersTest, IMSSubscriptionHSS_ReregWithoutSAR)
{
  reg_data_template_no_sar("reg", true, RegistrationState::REGISTERED);
}

// Call to a registered subscriber

TEST_F(HandlersTest, IMSSubscriptionCallHSS)
{
  reg_data_template_no_sar("call", true, RegistrationState::REGISTERED);
}

// Call to an unregistered subscriber (one whose data is already
// stored in the cache)

TEST_F(HandlersTest, IMSSubscriptionCallHSSUnregisteredService)
{
  reg_data_template_no_sar("call", true, RegistrationState::UNREGISTERED, 3600, REGDATA_RESULT_UNREG);
}

// Call to a not-registered subscriber (one whose data is not already
// stored in the cache)

TEST_F(HandlersTest, IMSSubscriptionCallHSSNewUnregisteredService)
{
  reg_data_template("call", true, false, RegistrationState::NOT_REGISTERED, 3, 0, REGDATA_RESULT_UNREG, RegistrationState::UNREGISTERED);
}

// Test the three types of deregistration flows

TEST_F(HandlersTest, IMSSubscriptionDeregHSS)
{
  reg_data_template_with_deletion("dereg-user", true, RegistrationState::REGISTERED, 5);
}

TEST_F(HandlersTest, IMSSubscriptionDeregTimeout)
{
  reg_data_template_with_deletion("dereg-timeout", true, RegistrationState::REGISTERED, 4);
}

TEST_F(HandlersTest, IMSSubscriptionDeregAdmin)
{
  reg_data_template_with_deletion("dereg-admin", true, RegistrationState::REGISTERED, 8);
}

// Test that if an IMPI is not explicitly provided on a deregistration
// flow, we use the one from the cached User-Data.

TEST_F(HandlersTest, IMSSubscriptionDeregUseCacheIMPI)
{
  reg_data_template_with_deletion("dereg-admin", false, RegistrationState::REGISTERED, 8);
}

// Test the two authentication failure flows (which should only affect
// the HSS and not the database). In particular, doing one of these
// for a registered subscriber should keep them in registered state
// (as it just means they've failed to log in with a new binding).

TEST_F(HandlersTest, IMSSubscriptionAuthFailRegistered)
{
  reg_data_template_no_write("dereg-auth-failed", false, true, RegistrationState::REGISTERED, 9, 3600, REGDATA_RESULT);
}

TEST_F(HandlersTest, IMSSubscriptionAuthFailRegisteredNoHSS)
{
  reg_data_template_no_write("dereg-auth-failed", false, false, RegistrationState::REGISTERED, 9, 3600, "");
}

TEST_F(HandlersTest, IMSSubscriptionAuthFail)
{
  reg_data_template_no_write("dereg-auth-failed", false, true, RegistrationState::NOT_REGISTERED, 9, 3600, REGDATA_RESULT_DEREG);
}

TEST_F(HandlersTest, IMSSubscriptionAuthTimeout)
{
  reg_data_template_no_write("dereg-auth-timeout", false, true, RegistrationState::NOT_REGISTERED, 10, 3600, REGDATA_RESULT_DEREG);
}

// Test that an attempt to deregister a subscriber who is not in
// REGISTERED state results in a 400 Bad Request

TEST_F(HandlersTest, IMSSubscriptionInvalidDereg)
{
  reg_data_template_invalid_dereg(false);
}

// Test that an attempt to deregister a subscriber who is not in
// REGISTERED state results in a 400 Bad Request with an HSS
// configured

TEST_F(HandlersTest, IMSSubscriptionHSSInvalidDereg)
{
  reg_data_template_invalid_dereg(true);
}

// Test that proper database flows happen when we don't have a HSS.

// Registration should leave the database in registered state.

TEST_F(HandlersTest, IMSSubscriptionReg)
{
  reg_data_template_no_hss("reg", true, RegistrationState::UNREGISTERED, 3600, REGDATA_RESULT, true, RegistrationState::REGISTERED);
}

TEST_F(HandlersTest, IMSSubscriptionRereg)
{
  reg_data_template_no_hss("reg", true, RegistrationState::REGISTERED);
}

// Deregistration should leave the database in unregistered state.

TEST_F(HandlersTest, IMSSubscriptionDereg)
{
  reg_data_template_no_hss("dereg-user", true, RegistrationState::REGISTERED, 3600, REGDATA_RESULT_UNREG, true, RegistrationState::UNREGISTERED);
}

// Making a call shouldn't change the registration state.

TEST_F(HandlersTest, IMSSubscriptionCall)
{
  reg_data_template_no_hss("call", true, RegistrationState::REGISTERED);
}

TEST_F(HandlersTest, IMSSubscriptionUnregisteredService)
{
  reg_data_template_no_hss("call", true, RegistrationState::UNREGISTERED, 0, REGDATA_RESULT_UNREG);
}

// If we have no record of the user, we should just return 404.

TEST_F(HandlersTest, IMSSubscriptionNoHSSUnknown)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "?private_id=" + IMPI,
                             "{\"reqtype\": \"reg\"}",
                             htp_method_PUT);
  ImpuRegDataHandler::Config cfg(false, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(""));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::NOT_REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ("", req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, IMSSubscriptionNoHSSUnknownCall)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "{\"reqtype\": \"call\"}",
                             htp_method_PUT);
  ImpuRegDataHandler::Config cfg(false, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(""));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::NOT_REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ("", req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

// Verify that the old interface (without /reg-data in the URL) still works

TEST_F(HandlersTest, LegacyIMSSubscriptionNoHSS)
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
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, LegacyIMSSubscriptionNoHSS_NotFound)
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
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(""));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::NOT_REGISTERED));
  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  EXPECT_CALL(mock_req, get_associated_impis(_));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ("", req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlersTest, LegacyIMSSubscriptionNoHSS_Unregistered)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "");
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
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::UNREGISTERED));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

// Verify that when doing a GET rather than a PUT, we just read from
// the cache, rather than doing a write or sending anything to the HSS.

TEST_F(HandlersTest, IMSSubscriptionGet)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "",
                             htp_method_GET);
  ImpuRegDataHandler::Config cfg(true, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_));

  // HTTP response is sent straight back - no state is changed.
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  t->on_success(&mock_req);

  // Build the expected response and check it's correct
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

// Test error handling

// If we don't recognise the body, we should reject the request

TEST_F(HandlersTest, IMSSubscriptionInvalidType)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "invalid",
                             htp_method_PUT);
  ImpuRegDataHandler::Config cfg(false, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  EXPECT_CALL(*_httpstack, send_reply(_, 400));
  handler->run();
}

// If we don't get a PUT or GET, we should reject the request

TEST_F(HandlersTest, IMSSubscriptionWrongMethod)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "{\"reqtype\": \"reg\"}",
                             htp_method_DELETE);
  ImpuRegDataHandler::Config cfg(false, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  EXPECT_CALL(*_httpstack, send_reply(_, 405));
  handler->run();
}

// HSS USER_UNKNOWN errors should translate into a 404

TEST_F(HandlersTest, IMSSubscriptionUserUnknownDereg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "?private_id=" + ASSOCIATED_IDENTITY1,
                             "{\"reqtype\": \"dereg-timeout\"}",
                             htp_method_PUT);
  ImpuRegDataHandler::Config cfg(true, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);
  MockCache::MockGetIMSSubscription mock_req;

  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_))
    .WillRepeatedly(SetArgReferee<0>(ASSOCIATED_IDENTITY1_IN_VECTOR));

  MockCache::MockDeletePublicIDs mock_req2;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU_REG_SET, _, _))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  std::string error_text = "error";
  t->on_success(&mock_req);

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_ERROR_USER_UNKNOWN,
                                 "");

  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(saa);

  Cache::Transaction* t2 = mock_req2.get_trx();
  ASSERT_FALSE(t2 == NULL);

  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

// Other HSS errors should translate into a 500 error

TEST_F(HandlersTest, IMSSubscriptionOtherErrorCallReg)
{
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "?private_id=" + IMPI,
                             "{\"reqtype\": \"call\"}",
                             htp_method_PUT);
  req.method();
  ImpuRegDataHandler::Config cfg(true, 3600);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(SetArgReferee<0>(IMPU_IMS_SUBSCRIPTION));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(SetArgReferee<0>(RegistrationState::NOT_REGISTERED));
  EXPECT_CALL(mock_req, get_associated_impis(_))
    .WillRepeatedly(SetArgReferee<0>(IMPI_IN_VECTOR));

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  std::string error_text = "error";
  t->on_success(&mock_req);

  ASSERT_FALSE(_caught_diam_tsx == NULL);
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);

  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 0,
                                 "");

  EXPECT_CALL(*_httpstack, send_reply(_, 500));
  _caught_diam_tsx->on_response(saa);

  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


// Cache failures should translate into a 502 Bad Gateway error

TEST_F(HandlersTest, IMSSubscriptionCacheFailureNoHSSInvalidType)
{
  // This test tests an IMS Subscription handler case for an invalid
  // registration type where no HSS is configured, and where the cache doesn't
  // return a result. Start by building the HTTP request which will
  // invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?type=invalid");

  ImpuIMSSubscriptionHandler::Config cfg(false, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm that the cache transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Expect a 502 HTTP response once the cache returns an error to the handler.
  EXPECT_CALL(*_httpstack, send_reply(_, 502));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
}

TEST_F(HandlersTest, RegistrationStatusHSSTimeout)
{
  // This test tests the common diameter timeout function. Build the HTTP request
  // which will invoke a UAR to be sent to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent. We don't bother checking the diameter message is as expected here. This
  // is done by other tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Expect a 503 response once we notify the handler about the timeout error.
  EXPECT_CALL(*_httpstack, send_reply(_, 503));
  _caught_diam_tsx->on_timeout();
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(HandlersTest, RegistrationStatus)
{
  // This test tests a mainline Registration Status handler case. Build the HTTP request
  // which will invoke a UAR to be sent to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a UAR and check its contents.
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

  // Build a UAA and expect a successful HTTP response.
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES), req.content());
}

TEST_F(HandlersTest, RegistrationStatusOptParamsSubseqRegCapabs)
{
  // This test tests a Registration Status handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, optional parameters
  // on the HTTP request are added, and the success code
  // DIAMETER_SUBSEQUENT_REGISTRATION is returned by the HSS with a set of server
  // capabilities. Build the HTTP request which will invoke a UAR to be sent to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU + "&visited-network=" + VISITED_NETWORK + "&auth-type=" + AUTH_TYPE_DEREG);

  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a UAR and check its contents.
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

  // Build a UAA and expect a successful HTTP response.
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  DIAMETER_SUBSEQUENT_REGISTRATION,
                                  "",
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_SUBSEQUENT_REGISTRATION, "", CAPABILITIES), req.content());
}

TEST_F(HandlersTest, RegistrationStatusFirstRegNoCapabs)
{
  // This test tests a Registration Status handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, the success code
  // DIAMETER_FIRST_REGISTRATION is returned by the HSS, but no server or server
  // capabilities are specified. Build the HTTP request which will invoke a UAR to be sent
  // to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a UAR and check its contents.
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

  // Build a UAA and expect a successful HTTP response.
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  DIAMETER_FIRST_REGISTRATION,
                                  "",
                                  NO_CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(uaa);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_FIRST_REGISTRATION, "", NO_CAPABILITIES), req.content());
}

// The following tests all test HSS error response cases, and use a template
// function defined at the top of this file.
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

  // Once the handler's run function is called, expect a successful HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, NO_CAPABILITIES), req.content());
}

//
// Location Info tests
//

TEST_F(HandlersTest, LocationInfo)
{
  // This test tests a mainline Location Info handler case. Build the HTTP request
  // which will invoke an LIR to be sent to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");

  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a LIR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));

  // Build an LIA and expect a successful HTTP response.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));

  _caught_diam_tsx->on_response(lia);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES), req.content());
}

TEST_F(HandlersTest, LocationInfoOptParamsUnregisteredService)
{
  // This test tests a Location Info handler case. The scenario is unrealistic
  // but lots of code branches are tested. Specifically, optional parameters
  // on the HTTP request are added, and the success code
  // DIAMETER_UNREGISTERED_SERVICE is returned by the HSS with a set of server
  // capabilities. Start by building the HTTP request which will invoke an LIR
  // to be sent to the HSS.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "?originating=true&auth-type=" + AUTH_TYPE_CAPAB);

  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a LIR and check its contents.
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

  // Build an LIA and expect a successful HTTP response.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             DIAMETER_UNREGISTERED_SERVICE,
                             "",
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(lia);
  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_UNREGISTERED_SERVICE, "", CAPABILITIES), req.content());
}

// The following tests all test HSS error response cases, and use a template
// function defined at the top of this file.
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

  // Once the handler's run function is called, expect a successful HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  handler->run();

  // Build the expected JSON response and check it's correct.
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, DEFAULT_SERVER_NAME, NO_CAPABILITIES), req.content());
}

//
// Registration Termination tests
//

TEST_F(HandlersTest, RegistrationTerminationPermanentTermination)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_OK);
}

TEST_F(HandlersTest, RegistrationTerminationRemoveSCSCF)
{
  rtr_template(REMOVE_SCSCF, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST, HTTP_OK);
}

TEST_F(HandlersTest, RegistrationTerminationPermanentTerminationNoIMPUs)
{
  rtr_template_no_impus(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS2);
}

TEST_F(HandlersTest, RegistrationTerminationRemoveSCSCFNoIMPUS)
{
  rtr_template_no_impus(REMOVE_SCSCF, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST2);
}

TEST_F(HandlersTest, RegistrationTerminationServerChange)
{
  rtr_template_no_impus(SERVER_CHANGE, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST2);
}

TEST_F(HandlersTest, RegistrationTerminationNewServerAssigned)
{
  rtr_template_no_impus(NEW_SERVER_ASSIGNED, HTTP_PATH_REG_FALSE, DEREG_BODY_LIST2);
}

TEST_F(HandlersTest, RegistrationTerminationHTTPBadMethod)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_BADMETHOD);
}

TEST_F(HandlersTest, RegistrationTerminationHTTPBadResult)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_BAD_RESULT);
}

TEST_F(HandlersTest, RegistrationTerminationHTTPServerError)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_SERVER_ERROR);
}

TEST_F(HandlersTest, RegistrationTerminationHTTPUnknownError)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, 999);
}

TEST_F(HandlersTest, RegistrationTerminationNoRegSets)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect a cache request for
  // the IMS subscription of the final public identity in IMPUS.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU2))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // The cache indicates success, but couldn't find any IMS subscription
  // information.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(DoAll(SetArgReferee<0>(""), SetArgReferee<1>(0)));

  // Expect another cache request for the IMS subscription of the next
  // public identity in IMPUS.
  MockCache::MockGetIMSSubscription mock_req2;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  t->on_success(&mock_req);

  // The cache indicates success, but couldn't find any IMS subscription
  // information.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req2, get_xml(_, _))
    .WillRepeatedly(DoAll(SetArgReferee<0>(""), SetArgReferee<1>(0)));

  // Expect to receive a diameter message.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req2);

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
}

TEST_F(HandlersTest, RegistrationTerminationRegSetsCacheFailure)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect a cache request for
  // the IMS subscription of the final public identity in IMPUS.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU2))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // The cache request fails.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Expect to receive a diameter message.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::INVALID_REQUEST, error_text);

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
}

TEST_F(HandlersTest, RegistrationTerminationNoAssocIMPUs)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         EMPTY_VECTOR,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // No public identities, so once the handler's run function is called, we
  // expect a cache request for associated default public identities.
  std::vector<std::string> impis{IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
  MockCache::MockGetAssociatedPrimaryPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPrimaryPublicIDs(impis))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // The cache indicates success but returns an empty list of IMPUs.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(EMPTY_VECTOR));

  // Expect to receive a diameter message.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req);

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
}

TEST_F(HandlersTest, RegistrationTerminationAssocIMPUsCacheFailure)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         EMPTY_VECTOR,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // No public identities, so once the handler's run function is called, we
  // expect a cache request for associated default public identities.
  std::vector<std::string> impis{IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
  MockCache::MockGetAssociatedPrimaryPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPrimaryPublicIDs(impis))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // The cache request fails.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Expect to receive a diameter message.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::INVALID_REQUEST, error_text);

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
}

TEST_F(HandlersTest, RegistrationTerminationInvalidDeregReason)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         5,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, _sprout_conn, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(_cx_dict, &rtr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // We expect to receive a diameter message.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  // Turn the caught Diameter msg structure into a RTA and confirm it's contents.
  std::vector<std::string> impis{IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
  EXPECT_EQ(impis, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
}

//
// Push Profile tests
//

TEST_F(HandlersTest, PushProfileAVIMSSub)
{
  DigestAuthVector digest_av;
  digest_av.ha1 = "ha1";
  digest_av.realm = "realm";
  digest_av.qop = "qop";

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             IMS_SUBSCRIPTION,
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect to try and update
  // the AV in the cache.
  MockCache::MockPutAuthVector mock_req;
  EXPECT_CALL(*_cache, create_PutAuthVector(IMPI, _, _, 0))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Next we expect to try and cache the IMS subscription.
  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, _, 7200))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  t->on_success(&mock_req);

  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // Finally we expect a PPA.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req2);

  // Turn the caught Diameter msg structure into a PPA and confirm it's contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());
}

TEST_F(HandlersTest, PushProfileAV)
{
  DigestAuthVector digest_av;
  digest_av.ha1 = "ha1";
  digest_av.realm = "realm";
  digest_av.qop = "qop";

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             "",
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect to try and update
  // the AV in the cache.
  MockCache::MockPutAuthVector mock_req;
  EXPECT_CALL(*_cache, create_PutAuthVector(IMPI, _, _, 0))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Finally we expect a PPA.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req);

  // Turn the caught Diameter msg structure into a PPA and confirm it's contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());
}

TEST_F(HandlersTest, PushProfileIMSSub)
{
  DigestAuthVector digest_av;

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             IMS_SUBSCRIPTION,
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect to try and update
  // the IMS Subscription in the cache.
  MockCache::MockPutIMSSubscription mock_req;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, _, 7200))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  // Finally we expect a PPA.
  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  t->on_success(&mock_req);

  // Turn the caught Diameter msg structure into a PPA and confirm it's contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());
}

TEST_F(HandlersTest, PushProfileAVCacheFailure)
{
  DigestAuthVector digest_av;
  digest_av.ha1 = "ha1";
  digest_av.realm = "realm";
  digest_av.qop = "qop";

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             "",
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect to try and update
  // the AV in the cache.
  MockCache::MockPutAuthVector mock_req;
  EXPECT_CALL(*_cache, create_PutAuthVector(IMPI, _, _, 0))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  // The cache request fails.
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::INVALID_REQUEST, error_text);

  // Turn the caught Diameter msg structure into a PPA and confirm the result code.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
}

TEST_F(HandlersTest, PushProfileIMSSubCacheFailure)
{
  DigestAuthVector digest_av;

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             IMS_SUBSCRIPTION,
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we expect to try and update
  // the IMS Subscription in the cache.
  MockCache::MockPutIMSSubscription mock_req;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, _, 7200))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  // The cache request fails.
  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::INVALID_REQUEST, error_text);

  // Turn the caught Diameter msg structure into a PPA and confirm the result code.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
}

TEST_F(HandlersTest, PushProfileNoInformation)
{
  DigestAuthVector digest_av;

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             "",
                             AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this PPR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  ppr._free_on_delete = false;

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(_cx_dict, &ppr._fd_msg, &cfg);

  // We have to make sure the message is pointing at the mock stack.
  handler->_msg._stack = _mock_stack;

  // Once the handler's run function is called, we just expect a PPA to be sent
  // since there was nothing to do on this PPR.
  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  // Turn the caught Diameter msg structure into a PPA and confirm the result code.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
}

//
// Stats tests
//
// These onlt test stats function - they only check diameter/cache/HTTP flows to
// the extent that is required to drive the necessary flows.
//

class HandlerStatsTest : public HandlersTest
{
public:
  HandlerStatsTest() : HandlersTest() {}
  virtual ~HandlerStatsTest() {}

  static void SetUpTestCase()
  {
    HandlersTest::SetUpTestCase();
    ignore_stats(false);
  }

  static void TearDownTestCase()
  {
    ignore_stats(true);
    HandlersTest::TearDownTestCase();
  }
};


TEST_F(HandlerStatsTest, DigestCache)
{
  // Test that successful cache requests result in the latency stats being
  // updated. Drive this with an HTTP request for digest.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Handler does a cache digest lookup.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  // The cache request takes some time.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  t->start_timer();
  cwtest_advance_time_ms(12);
  t->stop_timer();

  // The cache stats get updated when the transaction complete.
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  EXPECT_CALL(*_stats, update_H_cache_latency_us(12000));
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(digest));
  EXPECT_CALL(*_httpstack, send_reply(_, _));
  t->on_success(&mock_req);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, DigestCacheFailure)
{
  // Test that UNsuccessful cache requests result in the latency stats being
  // updated. Drive this with an HTTP request for digest.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(false);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Handler does a cache digest lookup.
  MockCache::MockGetAuthVector mock_req;
  EXPECT_CALL(*_cache, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  // The cache request takes some time.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  t->start_timer();
  cwtest_advance_time_ms(12);
  t->stop_timer();

  // Cache latency stats are updated when the transaction fails.
  EXPECT_CALL(*_httpstack, send_reply(_, _));
  EXPECT_CALL(*_stats, update_H_cache_latency_us(12000));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, DigestHSS)
{
  // Check that a diameter MultimediaAuthRequest updates the right stats.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();

  // The transaction takes some time.
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(13);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  // Build an MAA.
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;

  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // The HSS and digest stats are updated.
  EXPECT_CALL(*_stats, update_H_hss_latency_us(13000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(13000));

  MockCache::MockPutAssociatedPublicID mock_req;
  EXPECT_CALL(*_cache, create_PutAssociatedPublicID(IMPI, IMPU,  _, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, _));
  _caught_diam_tsx->on_response(maa);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, DigestHSSTimeout)
{
  // Check that a timed-out MultimediaAuthRequest updates the HSS and digest
  // stats.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiHandler::Config cfg(true, 300, SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA);
  ImpiDigestHandler* handler = new ImpiDigestHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // The transaction takes some time.
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(13);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  EXPECT_CALL(*_stats, update_H_hss_latency_us(13000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(13000));

  EXPECT_CALL(*_httpstack, send_reply(_, _));
  _caught_diam_tsx->on_timeout();
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, IMSSubscriptionReregHSS)
{
  // Check a ServerAssignmentRequest updates the HSS and subscription stats.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "?private_id=" + IMPI,
                             "{\"reqtype\": \"reg\"}",
                             htp_method_PUT);

  ImpuRegDataHandler::Config cfg(true, 1800);
  ImpuRegDataHandler* handler = new ImpuRegDataHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  // Check the cache get latency is recorded.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_xml(_, _))
    .WillRepeatedly(DoAll(SetArgReferee<0>(""), SetArgReferee<1>(0)));
  EXPECT_CALL(mock_req, get_registration_state(_, _))
    .WillRepeatedly(DoAll(SetArgReferee<0>(RegistrationState::NOT_REGISTERED), SetArgReferee<1>(0)));
  EXPECT_CALL(mock_req, get_associated_impis(_));

  t->start_timer();
  cwtest_advance_time_ms(10);
  t->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  EXPECT_CALL(*_stats, update_H_cache_latency_us(10000));

  t->on_success(&mock_req);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // The diameter SAR takes some time to process.
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(20);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, _, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  // Expect the stats to get updated.
  EXPECT_CALL(*_stats, update_H_hss_latency_us(20000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(20000));

  EXPECT_CALL(*_httpstack, send_reply(_, _));
  _caught_diam_tsx->on_response(saa);

  // Check the cache put latency is recorded.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  t->start_timer();
  cwtest_advance_time_ms(11);
  t->stop_timer();

  EXPECT_CALL(*_stats, update_H_cache_latency_us(11000));
  t->on_success(&mock_req2);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, RegistrationStatus)
{
  // Check a UAR request updated the HSS and subscription stats.
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI + "/",
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusHandler::Config cfg(true);
  ImpiRegistrationStatusHandler* handler = new ImpiRegistrationStatusHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();

  // The diameter message takes some time.
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(13);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  // Expect the stats to be updated when the answer is handled.
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, _));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(13000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(13000));
  _caught_diam_tsx->on_response(uaa);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, LocationInfo)
{
  // Check an LIR request updates the HSS and subsbcription latency stats.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");

  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(16);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  // Expect the stats to be updated when the answer is handled.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, _));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(16000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(16000));

  _caught_diam_tsx->on_response(lia);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}


TEST_F(HandlerStatsTest, LocationInfoOverload)
{
  // Check that an HSS overload repsonse causes the handlers to record a latency
  // penalty in the HTTP stack.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");

  ImpuLocationInfoHandler::Config cfg(true);
  ImpuLocationInfoHandler* handler = new ImpuLocationInfoHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be
  // sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(17);
  _caught_diam_tsx->stop_timer();

  // Free the underlying FD message.
  fd_msg_free(_caught_fd_msg); _caught_fd_msg = NULL;

  // Expect a latency penalty to be recorded when the "too busy" answer is
  // handled.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_TOO_BUSY,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, record_penalty());
  EXPECT_CALL(*_httpstack, send_reply(_, _));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(17000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(17000));

  _caught_diam_tsx->on_response(lia);
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}
