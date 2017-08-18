/**
 * @file hsprov_hss_connection_test.cpp UT for HsProvHssConnection.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */


#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"
#include "test_interposer.hpp"
#include "fakelogger.h"
#include <curl/curl.h>

#include "httpstack_utils.h"

#include "fakehttpresolver.hpp"
#include "mockstatisticsmanager.hpp"
#include "hsprov_hss_connection.h"
#include "mockhsprovstore.hpp"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::Field;
using ::testing::AllOf;
using ::testing::IsNull;

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

// Allows us to catch an MAA, UAA, LIA or SAA and check their contents
class MockAnswerCatcher
{
public:
  virtual ~MockAnswerCatcher() {};
  MOCK_METHOD1(got_maa, void(const HssConnection::MultimediaAuthAnswer&));
  MOCK_METHOD1(got_uaa, void(const HssConnection::UserAuthAnswer&));
  MOCK_METHOD1(got_lia, void(const HssConnection::LocationInfoAnswer&));
  MOCK_METHOD1(got_saa, void(const HssConnection::ServerAssignmentAnswer&));
};

// Fixture for HsProvHssConnectionTest.
class HsProvHssConnectionTest : public testing::Test
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

  static MockHsProvStore* _mock_store;
  static HssConnection::HsProvHssConnection* _hss_connection;

  static MockAnswerCatcher* _answer_catcher;
  static HssConnection::maa_cb MAA_CB;
  static HssConnection::uaa_cb UAA_CB;
  static HssConnection::lia_cb LIA_CB;
  static HssConnection::saa_cb SAA_CB;

  static StrictMock<MockStatisticsManager>* _stats;


  std::string test_str;
  int32_t test_i32;
  uint32_t test_u32;

  HsProvHssConnectionTest() {}
  virtual ~HsProvHssConnectionTest() {}

  static void SetUpTestCase()
  {
    _answer_catcher = new MockAnswerCatcher();
    _stats = new StrictMock<MockStatisticsManager>;
    _mock_store = new MockHsProvStore();
    _hss_connection = new HssConnection::HsProvHssConnection(_stats, _mock_store, SERVER_NAME);

    HssConnection::HssConnection::configure_auth_schemes(SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);

    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _hss_connection; _hss_connection = NULL;
    delete _stats; _stats = NULL;
    delete _answer_catcher; _answer_catcher = NULL;
    delete _mock_store; _mock_store = NULL;
  }
};

const std::string HsProvHssConnectionTest::DEST_REALM = "dest-realm";
const std::string HsProvHssConnectionTest::DEST_HOST = "dest-host";
const int HsProvHssConnectionTest::TIMEOUT_MS = 1000;

const std::string HsProvHssConnectionTest::IMPI = "_impi@example.com";
const std::string HsProvHssConnectionTest::IMPU = "sip:impu@example.com";
const std::string HsProvHssConnectionTest::SERVER_NAME = "scscf";
const std::string HsProvHssConnectionTest::AUTHORIZATION = "Authorization";
const std::string HsProvHssConnectionTest::VISITED_NETWORK = "visited-network.com";

const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities HsProvHssConnectionTest::CAPABILITIES(mandatory_capabilities, optional_capabilities, SERVER_NAME);

const std::string HsProvHssConnectionTest::IMS_SUB_XML = "xml";
const std::deque<std::string> HsProvHssConnectionTest::NO_CFS = {};
const std::deque<std::string> HsProvHssConnectionTest::ECFS = {"ecf1", "ecf"};
const std::deque<std::string> HsProvHssConnectionTest::CCFS = {"ccf1", "ccf2"};
const ChargingAddresses HsProvHssConnectionTest::NO_CHARGING_ADDRESSES(NO_CFS, NO_CFS);
const ChargingAddresses HsProvHssConnectionTest::FULL_CHARGING_ADDRESSES(CCFS, ECFS);

const std::string HsProvHssConnectionTest::CHALLENGE = "challenge";
const std::string HsProvHssConnectionTest::RESPONSE = "response";
const std::string HsProvHssConnectionTest::CRYPT_KEY = "crypt_key";
const std::string HsProvHssConnectionTest::INTEGRITY_KEY = "integrity_key";
const std::string HsProvHssConnectionTest::CHALLENGE_ENC = "Y2hhbGxlbmdl";
const std::string HsProvHssConnectionTest::RESPONSE_ENC = "726573706f6e7365";
const std::string HsProvHssConnectionTest::CRYPT_KEY_ENC = "63727970745f6b6579";
const std::string HsProvHssConnectionTest::INTEGRITY_KEY_ENC = "696e746567726974795f6b6579";

const std::string HsProvHssConnectionTest::SCHEME_UNKNOWN = "Unknown";
const std::string HsProvHssConnectionTest::SCHEME_DIGEST = "SIP Digest";
const std::string HsProvHssConnectionTest::SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string HsProvHssConnectionTest::SCHEME_AKAV2 = "Digest-AKAv2-SHA-256";

HssConnection::HsProvHssConnection* HsProvHssConnectionTest::_hss_connection = NULL;
StrictMock<MockStatisticsManager>* HsProvHssConnectionTest::_stats = NULL;
MockAnswerCatcher* HsProvHssConnectionTest::_answer_catcher = NULL;
MockHsProvStore* HsProvHssConnectionTest::_mock_store = NULL;

// These functions allow us to pass the answers to our _answer_catcher, which
// we use to check the contents of the answer
HssConnection::maa_cb HsProvHssConnectionTest::MAA_CB = [](const HssConnection::MultimediaAuthAnswer& maa) {
  _answer_catcher->got_maa(maa);
};

HssConnection::uaa_cb HsProvHssConnectionTest::UAA_CB = [](const HssConnection::UserAuthAnswer& uaa) {
  _answer_catcher->got_uaa(uaa);
};

HssConnection::lia_cb HsProvHssConnectionTest::LIA_CB = [](const HssConnection::LocationInfoAnswer& lia) {
  _answer_catcher->got_lia(lia);
};

HssConnection::saa_cb HsProvHssConnectionTest::SAA_CB = [](const HssConnection::ServerAssignmentAnswer& saa) {
  _answer_catcher->got_saa(saa);
};

//
// MultimediaAuthRequest tests
//

TEST_F(HsProvHssConnectionTest, SendMAR)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect we'll request the digest from Cassandra
  MockHsProvStore::MockGetAuthVector mock_op;
  EXPECT_CALL(*_mock_store, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL, and specify an auth vector to be
  // returned
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  EXPECT_CALL(mock_op, get_result(_)).WillOnce(SetArgReferee<0>(digest));

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_maa(
    AllOf(Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::MultimediaAuthAnswer::_sip_auth_scheme, SCHEME_DIGEST),
          Field(&HssConnection::MultimediaAuthAnswer::_auth_vector,
            IsDigestAndMatches("ha1", "realm", "qop"))))).Times(1).RetiresOnSaturation();


  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_success(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendMARNotFound)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect we'll request the digest from Cassandra, and set the operation to
  // have the result NOT_FOUND
  MockHsProvStore::MockGetAuthVector mock_op;
  mock_op._cass_status = CassandraStore::NOT_FOUND;

  EXPECT_CALL(*_mock_store, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();


  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendMAROtherError)
{
  // Create a Digest MAR
  HssConnection::MultimediaAuthRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    SCHEME_DIGEST,
    AUTHORIZATION
  };

  // Expect we'll request the digest from Cassandra, and set the operation to
  // have the result CONNECTION_ERROR
  MockHsProvStore::MockGetAuthVector mock_op;
  mock_op._cass_status = CassandraStore::CONNECTION_ERROR;

  EXPECT_CALL(*_mock_store, create_GetAuthVector(IMPI, IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the MAR
  _hss_connection->send_multimedia_auth_request(MAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  // All other errors are treated as TIMEOUT, so that homestead sends a 504 response
  EXPECT_CALL(*_answer_catcher, got_maa(
    Field(&HssConnection::MultimediaAuthAnswer::_result_code, ::HssConnection::ResultCode::TIMEOUT)))
    .Times(1).RetiresOnSaturation();


  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

//
// UserAuthRequest tests
//

TEST_F(HsProvHssConnectionTest, SendUAR)
{
  // Create a UAR
  HssConnection::UserAuthRequest request = {
    IMPI,
    IMPU,
    VISITED_NETWORK,
    "0",
    false
  };

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_uaa(
    AllOf(Field(&HssConnection::UserAuthAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::UserAuthAnswer::_server_name, SERVER_NAME)))).Times(1).RetiresOnSaturation();

  // Send the UAR
  _hss_connection->send_user_auth_request(UAA_CB, request, FAKE_TRAIL_ID);
}

//
// LocationInfoRequest tests
//

TEST_F(HsProvHssConnectionTest, SendLIR)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra
  MockHsProvStore::MockGetRegData mock_op;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the LIR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll request the XML from the Operation, and return some non-
  // empty XML
  EXPECT_CALL(mock_op, get_xml(_)).WillOnce(SetArgReferee<0>(IMS_SUB_XML));

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    AllOf(Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_json_result, DIAMETER_SUCCESS),
          Field(&HssConnection::LocationInfoAnswer::_server_name, SERVER_NAME),
          Field(&HssConnection::LocationInfoAnswer::_wildcard_impu, "")))).Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_success(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendLIRNotFound)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra, and we get an error
  MockHsProvStore::MockGetRegData mock_op;
  mock_op._cass_status = CassandraStore::NOT_FOUND;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the LIR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendLIROtherError)
{
  // Create an LIR
  HssConnection::LocationInfoRequest request = {
    IMPU,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra, and we get an error
  MockHsProvStore::MockGetRegData mock_op;
  mock_op._cass_status = CassandraStore::CONNECTION_ERROR;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the LIR
  _hss_connection->send_location_info_request(LIA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  // All other errors are treated as TIMEOUT, so that homestead sends a 504 response
  EXPECT_CALL(*_answer_catcher, got_lia(
    Field(&HssConnection::LocationInfoAnswer::_result_code, ::HssConnection::ResultCode::TIMEOUT)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

//
// ServerAssignmentRequest tests
//

TEST_F(HsProvHssConnectionTest, SendSAR)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra
  MockHsProvStore::MockGetRegData mock_op;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll get the charging addresses and XML from the completed
  // operation
  EXPECT_CALL(mock_op, get_xml(_)).WillOnce(SetArgReferee<0>(IMS_SUB_XML));
  EXPECT_CALL(mock_op, get_charging_addrs(_)).WillOnce(SetArgReferee<0>(FULL_CHARGING_ADDRESSES));

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
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_success(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendSARNotFound)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra
  MockHsProvStore::MockGetRegData mock_op;
  mock_op._cass_status = CassandraStore::NOT_FOUND;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  // All other errors are treated as TIMEOUT, so that homestead sends a 504 response
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::NOT_FOUND)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendSARError)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::REGISTRATION,
    "true",
    ""
  };

  // Expect we'll request the reg data from Cassandra
  MockHsProvStore::MockGetRegData mock_op;
  mock_op._cass_status = CassandraStore::CONNECTION_ERROR;

  EXPECT_CALL(*_mock_store, create_GetRegData(IMPU))
    .WillOnce(Return(&mock_op));
  EXPECT_DO_ASYNC(*_mock_store, mock_op);

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);

  // Confirm the transaction is not NULL
  CassandraStore::Transaction* t = mock_op.get_trx();
  ASSERT_FALSE(t == NULL);
  t->start_timer();

  // Expect that we'll call the callback with the correct answer
  // All other errors are treated as TIMEOUT, so that homestead sends a 504 response
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::TIMEOUT)))
    .Times(1).RetiresOnSaturation();

  // Expect the stats to be updated
  EXPECT_CALL(*_stats, update_H_hsprov_latency_us(12000));
  cwtest_advance_time_ms(12);

  t->on_failure(&mock_op);
}

TEST_F(HsProvHssConnectionTest, SendSARDeReg)
{
  // Create an SAR
  HssConnection::ServerAssignmentRequest request = {
    IMPI,
    IMPU,
    SERVER_NAME,
    Cx::ServerAssignmentType::USER_DEREGISTRATION,
    "true",
    ""
  };

  // Expect that we'll call the callback with SUCCESS
  EXPECT_CALL(*_answer_catcher, got_saa(
    Field(&HssConnection::ServerAssignmentAnswer::_result_code, ::HssConnection::ResultCode::SUCCESS)))
    .Times(1).RetiresOnSaturation();

  // Send the SAR
  _hss_connection->send_server_assignment_request(SAA_CB, request, FAKE_TRAIL_ID);
}
