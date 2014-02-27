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
using ::testing::Mock;

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
  static const std::string AUTH_TYPE_CAPAB;
  static const ServerCapabilities CAPABILITIES;
  static const ServerCapabilities NO_CAPABILITIES;
  static const int32_t AUTH_SESSION_STATE;
  static std::vector<std::string> ASSOCIATED_IDENTITIES;
  static std::vector<std::string> IMPUS;
  static const std::string SCHEME_UNKNOWN;
  static const std::string SCHEME_DIGEST;
  static const std::string SCHEME_AKA;
  static const std::string SIP_AUTHORIZATION;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;
  static MockCache* _cache;
  static MockHttpStack* _httpstack;

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
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
    delete _caught_fd_msg; _caught_fd_msg = NULL;
    delete _caught_diam_tsx; _caught_diam_tsx = NULL;
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
        writer.String(av.qop.c_str());
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

    // Ensure that the HTTP body on the response is empty.
    EXPECT_EQ("", req.content());
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
const std::string HandlersTest::IMPI = "impi@example.com";
const std::string HandlersTest::IMPU = "sip:impu@example.com";
const std::string HandlersTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><ServiceProfile><PublicIdentity><Identity>" +
                                                   IMPU +
                                                   "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HandlersTest::VISITED_NETWORK = "visited-network.com";
const std::string HandlersTest::AUTH_TYPE_DEREG = "DEREG";
const std::string HandlersTest::AUTH_TYPE_CAPAB = "CAPAB";
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities HandlersTest::CAPABILITIES(mandatory_capabilities, optional_capabilities);
const ServerCapabilities HandlersTest::NO_CAPABILITIES(no_capabilities, no_capabilities);
const int32_t HandlersTest::AUTH_SESSION_STATE = 1;
std::vector<std::string> HandlersTest::ASSOCIATED_IDENTITIES = {"impi456", "impi478"};
std::vector<std::string> HandlersTest::IMPUS = {"impu456", "impu478"};
const std::string HandlersTest::SCHEME_UNKNOWN = "Unknwon";
const std::string HandlersTest::SCHEME_DIGEST = "SIP Digest";
const std::string HandlersTest::SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string HandlersTest::SIP_AUTHORIZATION = "Authorization";

Diameter::Stack* HandlersTest::_real_stack = NULL;
MockDiameterStack* HandlersTest::_mock_stack = NULL;
Cx::Dictionary* HandlersTest::_cx_dict = NULL;
MockCache* HandlersTest::_cache = NULL;
MockHttpStack* HandlersTest::_httpstack = NULL;
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

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
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
  std::vector<std::string> empty_impus = {};
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(empty_impus));

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

TEST_F(HandlersTest, IMSSubscriptionRereg)
{
  // This test tests an IMS Subscription handler case for a reregistration.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=rereg");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the cache transaction is not NULL, and specify some IMS
  // subscription information to be returned on the expected call for the
  // cache request's results. We also expect a successful HTTP response.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));

  t->on_success(&mock_req);

  // Build the expected response and check it's correct.
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());
}

TEST_F(HandlersTest, IMSSubscriptionReregHSS)
{
  // This test tests an IMS Subscription handler case for a reregistration where
  // the IMS subscription information is not found in the cache.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=rereg");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  // Confirm the cache transaction is not NULL. When we tell the handler the cache
  // request failed, we expect to receive a Diameter message.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a SAR and check its contents.
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

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  // Once it receives the SAA, check that the handler tries to put the IMS
  // subscription in the database, that a successful HTTP response is sent, and
  // that the handler updates the latency stats.
  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  // Confirm the cache transaction is not NULL.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);
  t->on_success(&mock_req2);

  // Build the expected response and check it's correct.
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}

TEST_F(HandlersTest, IMSSubscriptionReg)
{
  // This test tests an IMS Subscription handler case for a registration.
  // Start by building the HTTP request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=reg");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a SAR and check its contents.
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

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  // Once it receives the SAA, check that the handler tries to put the IMS
  // subscription in the database, and that a successful HTTP response is sent.
  MockCache::MockPutIMSSubscription mock_req;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  // Confirm the cache transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected response and check it's correct.
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());
}

TEST_F(HandlersTest, IMSSubscriptionDereg)
{
  // This test tests an IMS Subscription handler case for a deregistration.
  // Start by building the HTTP request which will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=dereg-user");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Turn the caught Diameter msg structure into a SAR and check its contents.
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

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  // Once it receives the SAA, check that the handler tries to delete the public
  // ID from the database, and that a successful HTTP response is sent.
  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  // Confirm the cache transaction is not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // Build the expected empty response and check it's correct.
  EXPECT_EQ("", req.content());
}

TEST_F(HandlersTest, IMSSubscriptionNoHSS)
{
  // This test tests an IMS Subscription handler case for a registration where
  // no HSS is configured. Start by building the HTTP request which will invoke
  // a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=reg");

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

  // Confirm the cache transaction is not NULL, and specify some IMS
  // subscription information to be returned on the expected call for the
  // cache request's results. We also expect a successful HTTP response.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(IMS_SUBSCRIPTION));
  EXPECT_CALL(*_httpstack, send_reply(_, 200));

  t->on_success(&mock_req);

  // Build the expected response and check it's correct.
  EXPECT_EQ(IMS_SUBSCRIPTION, req.content());
}

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

TEST_F(HandlersTest, IMSSubscriptionUserUnknownDereg)
{
  // This test tests an IMS Subscription handler case for a deregistration where
  // the HSS returns a user unknown error. Start by building the HTTP request which
  // will invoke an HSS lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=dereg-timeout");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect a diameter message to be sent.
  // Don't bother checking it's contents since we've done this in previous tests.
  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  handler->run();
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_ERROR_USER_UNKNOWN,
                                 "");

  // Once it receives the SAA, check that the handler tries to delete the public
  // ID from the database, but that a 404 HTTP response is sent.
  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPU, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  EXPECT_CALL(*_httpstack, send_reply(_, 404));
  _caught_diam_tsx->on_response(saa);

  // Confirm the cache transaction isn't NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
}


TEST_F(HandlersTest, IMSSubscriptionOtherErrorCallReg)
{
  // This test tests an IMS Subscription handler case for a call registration where
  // the cache request fails, and the HSS returns an error. Start by building
  // the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=call-reg");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the cache transaction is not NULL. When we tell the handler the cache
  // request failed, we expect to receive a Diameter message. We don't bother
  // checking the contents of this message, since this is done in previous tests.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 0,
                                 "");

  // Expect a 500 HTTP response.
  EXPECT_CALL(*_httpstack, send_reply(_, 500));
  _caught_diam_tsx->on_response(saa);
}

//
// Registration Status tests
//

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

TEST_F(HandlersTest, RegistrationTerminationNoImpus)
{
  // Build an RTR and create a Registration Termination Handler with this message. This RTR
  // contains no IMPUS, an IMPI and some associated private identities.
  std::vector<std::string> no_impus;
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         no_impus,
                                         AUTH_SESSION_STATE);

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(rtr, &cfg);

  // Once the handler's run function is called, we expect a cache request to find out the
  // public identities associated with the private identities specified on the RTR. First
  // build a vector of these private identities from the IMPI and associated identities.
  std::vector<std::string> associated_identities{IMPI};
  associated_identities.insert(associated_identities.end(), ASSOCIATED_IDENTITIES.begin(), ASSOCIATED_IDENTITIES.end());

  MockCache::MockGetAssociatedPublicIDs mock_req;
  EXPECT_CALL(*_cache, create_GetAssociatedPublicIDs(associated_identities))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  handler->run();

  // Confirm the transaction is not NULL, and specify a list of IMPUS to be returned on
  // the expected call for the cache request's results.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>(IMPUS));

  // Once the captured cache transaction's on_success callback is called, we
  // expect several things to happen. We expect calls to the cache to delete
  // both the public IDs and private IDs associated with the RTR. We also expect
  // an RTA to be sent back to the diameter stack.
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

  // Confirm both cache transactions are not NULL.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req3.get_trx();
  ASSERT_FALSE(t == NULL);

  // Turn the caught Diameter msg structure into a RTA and confirm it's contents.
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  msg._free_on_delete = false;
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(associated_identities, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
}

TEST_F(HandlersTest, RegistrationTermination)
{
  // Build an RTR and create a Registration Termination Handler with this message. This RTR
  // contains a list of IMPUS, an IMPI and some associated private identities.
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);

  RegistrationTerminationHandler::Config cfg(_cache, _cx_dict, 0);
  RegistrationTerminationHandler* handler = new RegistrationTerminationHandler(rtr, &cfg);

  // Once the handler's run function is called, we expect several things to happen.
  // We expect calls to the cache to delete both the public IDs and private IDs associated
  // with the RTR. We also expect an RTA to be sent back to the diameter stack.
  MockCache::MockDeletePublicIDs mock_req;
  EXPECT_CALL(*_cache, create_DeletePublicIDs(IMPUS, _))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  // Create a vector of private IDs from the IMPI and associated identities.
  std::vector<std::string> associated_identities{IMPI};
  associated_identities.insert(associated_identities.end(), ASSOCIATED_IDENTITIES.begin(), ASSOCIATED_IDENTITIES.end());
  MockCache::MockDeletePrivateIDs mock_req2;
  EXPECT_CALL(*_cache, create_DeletePrivateIDs(associated_identities, _))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  // Confirm both cache transactions are not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // Turn the caught Diameter msg structure into a RTA and confirm it's contents.
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  msg._free_on_delete = false;
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(associated_identities, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
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

  // Build a PPR and create a Push Profile Handler with this message. This PPR
  // contains both IMS subscription information, and a digest AV.
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             digest_av,
                             IMS_SUBSCRIPTION,
                             AUTH_SESSION_STATE);

  PushProfileHandler::Config cfg(_cache, _cx_dict, 0, 3600);
  PushProfileHandler* handler = new PushProfileHandler(ppr, &cfg);

  // Once the handler's run function is called, we expect several things to happen.
  // We expect calls to the cache for both the AV and the IMS subscription. We also
  // expect a PPA to be sent back to the diameter stack.
  MockCache::MockPutAuthVector mock_req;
  EXPECT_CALL(*_cache, create_PutAuthVector(IMPI, _, _, 0))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));

  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));

  EXPECT_CALL(*_mock_stack, send(_))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  handler->run();

  // Confirm both cache transactions are not NULL.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // Turn the caught Diameter msg structure into a PPA and confirm it's contents.
  // Change the _free_on_delete flag to false, or we will try and
  // free this message twice.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  msg._free_on_delete = false;
  Cx::PushProfileAnswer ppa(msg);
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());
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
}


TEST_F(HandlerStatsTest, DigestHSSTimeout)
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

  // The transaction takes some time.
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(13);
  _caught_diam_tsx->stop_timer();

  EXPECT_CALL(*_stats, update_H_hss_latency_us(13000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(13000));

  EXPECT_CALL(*_httpstack, send_reply(_, 503));
  _caught_diam_tsx->on_timeout();

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlerStatsTest, IMSSubscriptionReregHSS)
{
  // This test tests an IMS Subscription handler case for a reregistration where
  // the IMS subscription information is not found in the cache.
  // Start by building the HTTP request which will invoke a cache lookup.
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "",
                             "?private_id=" + IMPI + "&type=rereg");

  ImpuIMSSubscriptionHandler::Config cfg(true, 3600);
  ImpuIMSSubscriptionHandler* handler = new ImpuIMSSubscriptionHandler(req, &cfg);

  // Once the handler's run function is called, expect to lookup IMS
  // subscription information for the specified public ID.
  MockCache::MockGetIMSSubscription mock_req;
  EXPECT_CALL(*_cache, create_GetIMSSubscription(IMPU))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(*_cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  handler->run();

  // Confirm the cache transaction is not NULL. When we tell the handler the cache
  // request failed, we expect to receive a Diameter message.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);

  // The cache request takes some time.
  t->start_timer();
  cwtest_advance_time_ms(10);
  t->stop_timer();

  EXPECT_CALL(*_mock_stack, send(_, _, 200))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));
  EXPECT_CALL(*_stats, update_H_cache_latency_us(10000));

  std::string error_text = "error";
  t->on_failure(&mock_req, Cache::NOT_FOUND, error_text);
  ASSERT_FALSE(_caught_diam_tsx == NULL);

  // The diameter requests takes some time to process.
  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(20);
  _caught_diam_tsx->stop_timer();

  // Build an SAA.
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 IMS_SUBSCRIPTION);

  // Once it receives the SAA, check that the handler tries to put the IMS
  // subscription in the database, that a successful HTTP response is sent, and
  // that the handler updates the latency stats.
  MockCache::MockPutIMSSubscription mock_req2;
  std::vector<std::string> impus{IMPU};
  EXPECT_CALL(*_cache, create_PutIMSSubscription(impus, IMS_SUBSCRIPTION, _, 3600))
    .WillOnce(Return(&mock_req2));
  EXPECT_CALL(*_cache, send(_, &mock_req2))
    .WillOnce(WithArgs<0>(Invoke(&mock_req2, &Cache::Request::set_trx)));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(20000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(20000));

  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  _caught_diam_tsx->on_response(saa);

  // Confirm the cache transaction is not NULL.
  t = mock_req2.get_trx();
  ASSERT_FALSE(t == NULL);

  // The cache request takes some time.
  t->start_timer();
  cwtest_advance_time_ms(11);
  t->stop_timer();

  EXPECT_CALL(*_stats, update_H_cache_latency_us(11000));
  t->on_success(&mock_req2);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlerStatsTest, RegistrationStatus)
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

  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(13);
  _caught_diam_tsx->stop_timer();

  // Build a UAA and expect a successful HTTP response.
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(13000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(13000));
  _caught_diam_tsx->on_response(uaa);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlerStatsTest, LocationInfo)
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

  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(16);
  _caught_diam_tsx->stop_timer();

  // Build an LIA and expect a successful HTTP response.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, send_reply(_, 200));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(16000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(16000));

  _caught_diam_tsx->on_response(lia);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}


TEST_F(HandlerStatsTest, LocationInfoOverload)
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

  _caught_diam_tsx->start_timer();
  cwtest_advance_time_ms(17);
  _caught_diam_tsx->stop_timer();

  // Build an LIA and expect a successful HTTP response.
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_TOO_BUSY,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);
  EXPECT_CALL(*_httpstack, record_penalty());
  EXPECT_CALL(*_httpstack, send_reply(_, 503));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(17000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(17000));

  _caught_diam_tsx->on_response(lia);

  _caught_diam_tsx = NULL;
  _caught_fd_msg = NULL;
}
