/**
 * @file diameter_hss_connection_test.cpp UT for DiameterHssConnection.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
#include "fakelogger.h"
#include <curl/curl.h>

#include "httpstack_utils.h"

#include "mockdiameterstack.hpp"
#include "mockhttpstack.hpp"
#include "mockcache.hpp"
#include "mockhttpconnection.hpp"
#include "fakehttpresolver.hpp"
#include "handlers.h"
#include "mockstatisticsmanager.hpp"
#include "sproutconnection.h"
#include "mock_health_checker.hpp"
#include "fakesnmp.hpp"
#include "base64.h"

#include "diameter_hss_connection.h"

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::WithArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::AtLeast;
using ::testing::Field;
using ::testing::AllOf;
using ::testing::ByRef;
using ::testing::ReturnNull;
using ::testing::IsNull;
using ::testing::Pointee;
using ::testing::InvokeWithoutArgs;


// We pass AuthVector*s around, not the subclasses, so we need some custom
// matchers to check that an AuthVector* matches a given DigestAuthVector or
// AKAAuthVector
MATCHER_P3(IsDigestAndMatches, ha1, realm, qop, "")
{
  if (arg != NULL)
  {
    DigestAuthVector* digest = dynamic_cast<DigestAuthVector*>(arg);
    if (digest != NULL)
    {
      return ((digest->ha1 == ha1) &&
              (digest->realm == realm) &&
              (digest->qop == qop));
    }
  }
  return false;
}

MATCHER_P5(IsAKAAndMatches, version, challenge, response, crypt_key, integrity_key, "")
{
  if (arg != NULL)
  {
    AKAAuthVector* aka = dynamic_cast<AKAAuthVector*>(arg);
    if (aka != NULL)
    {
      return ((aka->version == version) &&
              (aka->challenge == challenge) &&
              (aka->response == response) &&
              (aka->crypt_key == crypt_key) &&
              (aka->integrity_key == integrity_key));
    }
    else
    {
      *result_listener << "which isn't an AKAAuthVector*";
    }
  }
  else
  {
    *result_listener << "which is NULL";
  }
  return false;
}

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

class MockAnswerCatcher
{
public:
  virtual ~MockAnswerCatcher() {};
  MOCK_METHOD1(got_maa, void(const HssConnection::MultimediaAuthAnswer&));
  MOCK_METHOD1(got_uaa, void(const HssConnection::UserAuthAnswer&));
  MOCK_METHOD1(got_lia, void(const HssConnection::LocationInfoAnswer&));
  MOCK_METHOD1(got_saa, void(const HssConnection::ServerAssignmentAnswer&));
};

// Fixture for DiameterHssConnectionTest.
class DiameterHssConnectionTest : public testing::Test
{
public:
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const int TIMEOUT_MS;

  static const std::string SCHEME_UNKNOWN;
  static const std::string SCHEME_DIGEST;
  static const std::string SCHEME_AKA;
  static const std::string SCHEME_AKAV2;


  static const std::string IMPI;
  static const std::string IMPU;
  static const std::string SERVER_NAME;
  static const std::string AUTHORIZATION;
  static const std::string VISITED_NETWORK;

  static const ServerCapabilities CAPABILITIES;

  static const std::string IMS_SUB_XML;
  static const std::deque<std::string> NO_CFS;
  static const std::deque<std::string> ECFS;
  static const std::deque<std::string> CCFS;
  static const ChargingAddresses NO_CHARGING_ADDRESSES;
  static const ChargingAddresses FULL_CHARGING_ADDRESSES;

  static const std::string CHALLENGE;
  static const std::string RESPONSE;
  static const std::string CRYPT_KEY;
  static const std::string INTEGRITY_KEY;
  static const std::string CHALLENGE_ENC;
  static const std::string RESPONSE_ENC;
  static const std::string CRYPT_KEY_ENC;
  static const std::string INTEGRITY_KEY_ENC;

  static Cx::Dictionary* _cx_dict;
  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static HssConnection::DiameterHssConnection* _hss_connection;

  static MockAnswerCatcher* _answer_catcher;
  static HssConnection::maa_cb MAA_CB;
  static HssConnection::uaa_cb UAA_CB;
  static HssConnection::lia_cb LIA_CB;
  static HssConnection::saa_cb SAA_CB;

  static StrictMock<MockStatisticsManager>* _stats;

  static SNMP::CxCounterTable* _mar_results_table;
  static SNMP::CxCounterTable* _sar_results_table;
  static SNMP::CxCounterTable* _uar_results_table;
  static SNMP::CxCounterTable* _lir_results_table;
  static SNMP::CxCounterTable* _ppr_results_table;
  static SNMP::CxCounterTable* _rtr_results_table;

  // Used to catch diameter messages and transactions on the MockDiameterStack
  // so that we can inspect them.
  static struct msg* _caught_fd_msg;
  static Diameter::Transaction* _caught_diam_tsx;

  std::string test_str;
  int32_t test_i32;
  uint32_t test_u32;

  DiameterHssConnectionTest() {}
  virtual ~DiameterHssConnectionTest() {}

  static void SetUpTestCase()
  {
    _answer_catcher = new MockAnswerCatcher();
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf", NULL);
    _mock_stack = new MockDiameterStack();
    _cx_dict = new Cx::Dictionary();
    _stats = new StrictMock<MockStatisticsManager>;
    _hss_connection = new HssConnection::DiameterHssConnection(_stats, _cx_dict, _mock_stack, DEST_REALM, DEST_HOST, TIMEOUT_MS);

    _mar_results_table = SNMP::CxCounterTable::create("", "");
    _sar_results_table = SNMP::CxCounterTable::create("", "");
    _uar_results_table = SNMP::CxCounterTable::create("", "");
    _lir_results_table = SNMP::CxCounterTable::create("", "");
    _ppr_results_table = SNMP::CxCounterTable::create("", "");
    _rtr_results_table = SNMP::CxCounterTable::create("", "");

    HssConnection::configure_cx_results_tables(_mar_results_table,
                                               _sar_results_table,
                                               _uar_results_table,
                                               _lir_results_table,
                                               _ppr_results_table,
                                               _rtr_results_table);

    HssConnection::DiameterHssConnection::configure_auth_schemes(SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  
    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _hss_connection; _hss_connection = NULL;
    delete _stats; _stats = NULL;
    delete _mock_stack; _mock_stack = NULL;
    delete _cx_dict; _cx_dict = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
    delete _answer_catcher; _answer_catcher = NULL;
    delete _mar_results_table; _mar_results_table = NULL;
    delete _sar_results_table; _sar_results_table = NULL;
    delete _uar_results_table; _uar_results_table = NULL;
    delete _lir_results_table; _lir_results_table = NULL;
    delete _ppr_results_table; _ppr_results_table = NULL;
    delete _rtr_results_table; _rtr_results_table = NULL;
  }


  // We frequently invoke the following method on the send method of our
  // MockDiameterStack in order to catch the Diameter message we're trying
  // to send.
  static void store_msg_tsx(struct msg* msg, Diameter::Transaction* tsx)
  {
    _caught_fd_msg = msg;
    _caught_diam_tsx = tsx;
  }
};

const std::string DiameterHssConnectionTest::DEST_REALM = "dest-realm";
const std::string DiameterHssConnectionTest::DEST_HOST = "dest-host";
const int DiameterHssConnectionTest::TIMEOUT_MS = 1000;

const std::string DiameterHssConnectionTest::IMPI = "_impi@example.com";
const std::string DiameterHssConnectionTest::IMPU = "sip:impu@example.com";
const std::string DiameterHssConnectionTest::SERVER_NAME = "scscf";
const std::string DiameterHssConnectionTest::AUTHORIZATION = "Authorization";
const std::string DiameterHssConnectionTest::VISITED_NETWORK = "visited-network.com";

const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities DiameterHssConnectionTest::CAPABILITIES(mandatory_capabilities, optional_capabilities, "");

const std::string DiameterHssConnectionTest::IMS_SUB_XML = "xml";
const std::deque<std::string> DiameterHssConnectionTest::NO_CFS = {};
const std::deque<std::string> DiameterHssConnectionTest::ECFS = {"ecf1", "ecf"};
const std::deque<std::string> DiameterHssConnectionTest::CCFS = {"ccf1", "ccf2"};
const ChargingAddresses DiameterHssConnectionTest::NO_CHARGING_ADDRESSES(NO_CFS, NO_CFS);
const ChargingAddresses DiameterHssConnectionTest::FULL_CHARGING_ADDRESSES(CCFS, ECFS);

const std::string DiameterHssConnectionTest::CHALLENGE = "challenge";
const std::string DiameterHssConnectionTest::RESPONSE = "response";
const std::string DiameterHssConnectionTest::CRYPT_KEY = "crypt_key";
const std::string DiameterHssConnectionTest::INTEGRITY_KEY = "integrity_key";
const std::string DiameterHssConnectionTest::CHALLENGE_ENC = "Y2hhbGxlbmdl";
const std::string DiameterHssConnectionTest::RESPONSE_ENC = "726573706f6e7365";
const std::string DiameterHssConnectionTest::CRYPT_KEY_ENC = "63727970745f6b6579";
const std::string DiameterHssConnectionTest::INTEGRITY_KEY_ENC = "696e746567726974795f6b6579";

const std::string DiameterHssConnectionTest::SCHEME_UNKNOWN = "Unknown";
const std::string DiameterHssConnectionTest::SCHEME_DIGEST = "SIP Digest";
const std::string DiameterHssConnectionTest::SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string DiameterHssConnectionTest::SCHEME_AKAV2 = "Digest-AKAv2-SHA-256";

msg* DiameterHssConnectionTest::_caught_fd_msg = NULL;
Cx::Dictionary* DiameterHssConnectionTest::_cx_dict = NULL;
Diameter::Stack* DiameterHssConnectionTest::_real_stack = NULL;
MockDiameterStack* DiameterHssConnectionTest::_mock_stack = NULL;
HssConnection::DiameterHssConnection* DiameterHssConnectionTest::_hss_connection = NULL;
StrictMock<MockStatisticsManager>* DiameterHssConnectionTest::_stats = NULL;
Diameter::Transaction* DiameterHssConnectionTest::_caught_diam_tsx = NULL;
MockAnswerCatcher* DiameterHssConnectionTest::_answer_catcher = NULL;

SNMP::CxCounterTable* DiameterHssConnectionTest::_mar_results_table = NULL;
SNMP::CxCounterTable* DiameterHssConnectionTest::_sar_results_table = NULL;
SNMP::CxCounterTable* DiameterHssConnectionTest::_uar_results_table = NULL;
SNMP::CxCounterTable* DiameterHssConnectionTest::_lir_results_table = NULL;
SNMP::CxCounterTable* DiameterHssConnectionTest::_ppr_results_table = NULL;
SNMP::CxCounterTable* DiameterHssConnectionTest::_rtr_results_table = NULL;

// These functions allow us to pass the answers to our _answer_catcher, which
// we use to check the contents of the answer
HssConnection::maa_cb DiameterHssConnectionTest::MAA_CB = [](const HssConnection::MultimediaAuthAnswer& maa) {
  _answer_catcher->got_maa(maa);
};

HssConnection::uaa_cb DiameterHssConnectionTest::UAA_CB = [](const HssConnection::UserAuthAnswer& uaa) {
  _answer_catcher->got_uaa(uaa);
};

HssConnection::lia_cb DiameterHssConnectionTest::LIA_CB = [](const HssConnection::LocationInfoAnswer& lia) {
  _answer_catcher->got_lia(lia);
};

HssConnection::saa_cb DiameterHssConnectionTest::SAA_CB = [](const HssConnection::ServerAssignmentAnswer& saa) {
  _answer_catcher->got_saa(saa);
};

//
// MultimediaAuthRequest tests
//

TEST_F(DiameterHssConnectionTest, SendMARDigest)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";
  AKAAuthVector aka;
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               0,
                               0,
                               SCHEME_DIGEST,
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_maa(
    AllOf(Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::MultimediaAuthAnswer::_sip_auth_scheme, SCHEME_DIGEST),
          Field(&HssConnection::MultimediaAuthAnswer::_auth_vector,
            IsDigestAndMatches("ha1", "realm", "qop"))))).Times(1).RetiresOnSaturation();


  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARAKAv1)
{
  // Create an AKAv1 MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_AKA,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_AKA, mar.sip_auth_scheme());
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response
  DigestAuthVector digest;
  AKAAuthVector aka;
  aka.challenge = CHALLENGE;
  aka.response = RESPONSE;
  aka.crypt_key = CRYPT_KEY;
  aka.integrity_key = INTEGRITY_KEY;

  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               0,
                               0,
                               SCHEME_AKA,
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct answer.
  // Note that the strings are now encoded.
  EXPECT_CALL(*_answer_catcher, got_maa(
    AllOf(Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::MultimediaAuthAnswer::_sip_auth_scheme, SCHEME_AKA),
          Field(&HssConnection::MultimediaAuthAnswer::_auth_vector,
            IsAKAAndMatches(1, CHALLENGE_ENC, RESPONSE_ENC, CRYPT_KEY_ENC, INTEGRITY_KEY_ENC))))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARAKAv2)
{
  // Create an AKAv1 MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_AKAV2,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a MAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::MultimediaAuthRequest mar(msg);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(mar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SCHEME_AKAV2, mar.sip_auth_scheme());
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response
  DigestAuthVector digest;
  AKAAuthVector aka;
  aka.challenge = CHALLENGE;
  aka.response = RESPONSE;
  aka.crypt_key = CRYPT_KEY;
  aka.integrity_key = INTEGRITY_KEY;
  aka.version = 2;

  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               0,
                               0,
                               SCHEME_AKAV2,
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct answer.
  // Note that the strings are now encoded.
  EXPECT_CALL(*_answer_catcher, got_maa(
    AllOf(Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::MultimediaAuthAnswer::_sip_auth_scheme, SCHEME_AKAV2),
          Field(&HssConnection::MultimediaAuthAnswer::_auth_vector,
            IsAKAAndMatches(2, CHALLENGE_ENC, RESPONSE_ENC, CRYPT_KEY_ENC, INTEGRITY_KEY_ENC))))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARRecvUnknownScheme)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response with unknown scheme
  DigestAuthVector digest;
  AKAAuthVector aka;
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_SUCCESS,
                               0,
                               0,
                               SCHEME_UNKNOWN,
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct ResultCode in the MAA
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::UNKNOWN_AUTH_SCHEME)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARRecvSERVER_UNAVAILABLE)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  //_caught_diam_tsx->start_timer();
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
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response
  DigestAuthVector digest;
  AKAAuthVector aka;
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               DIAMETER_UNABLE_TO_DELIVER,
                               0,
                               0,
                               "",
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct ResultCode in the MAA
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::SERVER_UNAVAILABLE)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARRecvNOT_FOUND)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response (the actual result is ignored here)
  DigestAuthVector digest;
  AKAAuthVector aka;
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               0,
                               VENDOR_ID_3GPP,
                               DIAMETER_ERROR_USER_UNKNOWN,
                               "",
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct ResultCode in the MAA
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendMARRecvUNKNOWN_ERROR)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);

  // We're now going to inject a response (the actual result is ignored here)
  DigestAuthVector digest;
  AKAAuthVector aka;
  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               7,
                               7777,
                               77777,
                               "",
                               digest,
                               aka);

  // Expect that we'll call the callback with the correct ResultCode in the MAA
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::UNKNOWN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_digest_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(maa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

//
// UserAuthRequest tests
//

TEST_F(DiameterHssConnectionTest, SendUARServerName)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  0,
                                  SERVER_NAME,
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    AllOf(Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::UserAuthAnswer::_server_capabilities, IsNull())))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARServerCapabilities)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_SUCCESS,
                                  0,
                                  0,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer, including
  // checking that we get the correct ServerCapabilities on the UAA
  EXPECT_CALL(*_answer_catcher, got_uaa(
    AllOf(Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_server_name, ""),
          Field(&HssConnection::UserAuthAnswer::_server_capabilities, Pointee(
            AllOf(Field(&ServerCapabilities::mandatory_capabilities, mandatory_capabilities),
                  Field(&ServerCapabilities::optional_capabilities, optional_capabilities))
          ))))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARUserUnknown)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  0,
                                  DIAMETER_ERROR_USER_UNKNOWN,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARIdentitiesDontMatch)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  0,
                                  DIAMETER_ERROR_USER_UNKNOWN,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARAuthRejected)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_AUTHORIZATION_REJECTED,
                                  0,
                                  0,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::FORBIDDEN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARRoamingNotAllowed)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  0,
                                  DIAMETER_ERROR_ROAMING_NOT_ALLOWED,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::FORBIDDEN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARTooBusy)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_TOO_BUSY,
                                  0,
                                  0,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::TIMEOUT)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARUnableToDeliver)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  DIAMETER_UNABLE_TO_DELIVER,
                                  0,
                                  0,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::SERVER_UNAVAILABLE)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendUARUnknownError)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

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
  EXPECT_EQ(0, test_i32);

  // We're now going to inject a response
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  7,
                                  7777,
                                  77777,
                                  "",
                                  CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::UNKNOWN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(uaa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

//
// LocationInfoRequest tests
//

TEST_F(DiameterHssConnectionTest, SendLIROriginating)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "true",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_TRUE(lir.originating(test_i32));
  EXPECT_EQ(0, test_i32);
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, ""),
          Field(&HssConnection::LocationInfoAnswer::_server_capabilities, IsNull())))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRAuthTypeCapabilities)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "false",
    "CAPAB"
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that auth-type "CAPAB" corresponds to a value of 2 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_TRUE(lir.auth_type(test_i32));
  EXPECT_EQ(2, test_i32);

  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             0,
                             "",
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_server_name, ""),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, ""),
          Field(&HssConnection::LocationInfoAnswer::_server_capabilities, Pointee(
            AllOf(Field(&ServerCapabilities::mandatory_capabilities, mandatory_capabilities),
                  Field(&ServerCapabilities::optional_capabilities, optional_capabilities))
          ))))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRWildcardImpu)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_SUCCESS,
                             0,
                             0,
                             SERVER_NAME,
                             CAPABILITIES,
                             "wildcard");

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, "wildcard"),
          Field(&HssConnection::LocationInfoAnswer::_server_capabilities, IsNull())))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRUnregisteredService)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             VENDOR_ID_3GPP,
                             DIAMETER_UNREGISTERED_SERVICE,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_UNREGISTERED_SERVICE),
          Field(&HssConnection::LocationInfoAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, ""),
          Field(&HssConnection::LocationInfoAnswer::_server_capabilities, IsNull())))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRIdentityNotRegistered)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             VENDOR_ID_3GPP,
                             DIAMETER_ERROR_IDENTITY_NOT_REGISTERED,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_ERROR_IDENTITY_NOT_REGISTERED),
          Field(&HssConnection::LocationInfoAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, ""),
          Field(&HssConnection::LocationInfoAnswer::_server_capabilities, IsNull())))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRUserUnknown)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             VENDOR_ID_3GPP,
                             DIAMETER_ERROR_USER_UNKNOWN,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRTooBusy)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_TOO_BUSY,
                             0,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::TIMEOUT)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRUnableToDeliver)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             DIAMETER_UNABLE_TO_DELIVER,
                             0,
                             0,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SERVER_UNAVAILABLE)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendLIRUnknown)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "",
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the UAR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  // Note that originating "true" corresponds to a value of 0 in the AVP
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::LocationInfoRequest lir(msg);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(lir.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_FALSE(lir.auth_type(test_i32));


  // We're now going to inject a response
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             7,
                             7777,
                             77777,
                             SERVER_NAME,
                             CAPABILITIES);

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::UNKNOWN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(lia);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

//
// ServerAssignmentRequest tests
//

TEST_F(DiameterHssConnectionTest, SendSARMainline)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(Cx::ServerAssignmentType::REGISTRATION, test_i32);

  // We're now going to inject a response
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_SUCCESS,
                                 0,
                                 0,
                                 IMS_SUB_XML,
                                 FULL_CHARGING_ADDRESSES);

  // Expect that we'll call the callback with the correct answer, including the
  // correct ChargingAddresses
  EXPECT_CALL(*_answer_catcher, got_saa(
    AllOf(Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::ServerAssignmentAnswer::_service_profile, IMS_SUB_XML),
          Field(&HssConnection::ServerAssignmentAnswer::_wildcard_impu, ""),
          Field(&HssConnection::ServerAssignmentAnswer::_charging_addrs,
            AllOf(Field(&ChargingAddresses::ccfs, CCFS),
                  Field(&ChargingAddresses::ecfs, ECFS)))))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(saa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendSARWildcardImpu)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(Cx::ServerAssignmentType::REGISTRATION, test_i32);

  // We're now going to inject a response
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 0,
                                 0,
                                 DIAMETER_ERROR_IN_ASSIGNMENT_TYPE,
                                 "",
                                 NO_CHARGING_ADDRESSES,
                                 "wildcard");

  // Expect that we'll call the callback with the correct answer and the
  // new wildcard impu
  EXPECT_CALL(*_answer_catcher, got_saa(
    AllOf(Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::NEW_WILDCARD),
          Field(&HssConnection::ServerAssignmentAnswer::_wildcard_impu, "wildcard"))))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(saa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendSARUnableToDeliver)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(Cx::ServerAssignmentType::REGISTRATION, test_i32);

  // We're now going to inject a response
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 DIAMETER_UNABLE_TO_DELIVER,
                                 0,
                                 0,
                                 "",
                                 NO_CHARGING_ADDRESSES,
                                 "");

  // Expect that we'll call the callback with the correct answer and the
  // new wildcard impu
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::SERVER_UNAVAILABLE)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(saa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendSARUserUnknown)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(Cx::ServerAssignmentType::REGISTRATION, test_i32);

  // We're now going to inject a response
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 0,
                                 VENDOR_ID_3GPP,
                                 DIAMETER_ERROR_USER_UNKNOWN,
                                 "",
                                 NO_CHARGING_ADDRESSES,
                                 "");

  // Expect that we'll call the callback with the correct answer and the
  // new wildcard impu
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(saa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

TEST_F(DiameterHssConnectionTest, SendSARUnknown)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Turn the caught Diameter msg structure into a UAR and check its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::ServerAssignmentRequest sar(msg);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
  EXPECT_EQ(DEST_REALM, test_str);
  EXPECT_TRUE(sar.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
  EXPECT_EQ(DEST_HOST, test_str);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(Cx::ServerAssignmentType::REGISTRATION, test_i32);

  // We're now going to inject a response
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 7,
                                 7777,
                                 77777,
                                 "",
                                 NO_CHARGING_ADDRESSES,
                                 "");

  // Expect that we'll call the callback with the correct answer and the
  // new wildcard impu
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::UNKNOWN)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_response(saa);

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}

//
// Timeout test
//

TEST_F(DiameterHssConnectionTest, DiameterTimeout)
{
  // All timeouts use the same code, so only one test here

  // Create an SAR
  HssConnection::ServerAssignmentRequest  request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    false,
    ""
  };

  // Expect diameter message to be sent with the correct timeout, and store the
  // sent message
  EXPECT_CALL(*_mock_stack, send(_, _, TIMEOUT_MS))
    .Times(1)
    .WillOnce(WithArgs<0,1>(Invoke(store_msg_tsx)));

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Check that we've caught the message and it's not null
  ASSERT_FALSE(_caught_diam_tsx == NULL);
  _caught_diam_tsx->start_timer();

  // Expect that we'll call the callback with a SERVER_UNAVAILABLE ResultCode
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::SERVER_UNAVAILABLE)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hss_latency_us(12000));
  EXPECT_CALL(*_stats, update_H_hss_subscription_latency_us(12000));
  cwtest_advance_time_ms(12);

  _caught_diam_tsx->on_timeout();

  _caught_fd_msg = NULL;
  delete _caught_diam_tsx; _caught_diam_tsx = NULL;
}