/**
 * @file diameter_handlers_test.cpp UT for DiameterHandlers module.
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
#include "mockhttpconnection.hpp"
#include "fakehttpresolver.hpp"
#include "fake_implicit_reg_set.h"
#include "diameter_handlers.h"
#include "sproutconnection.h"

#include "mockhssconnection.hpp"
#include "mockhsscacheprocessor.hpp"
#include "mockimssubscription.hpp"

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::WithArgs;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::Field;
using ::testing::AllOf;
using ::testing::ReturnNull;

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

// Fixture for DiameterHandlersTest.
class DiameterHandlersTest : public testing::Test
{
public:
  static const std::string SERVER_NAME;
  static const std::string IMPI;
  static const std::string IMPU;
  static std::vector<std::string> IMPU_IN_VECTOR;
  static std::vector<std::string> IMPI_IN_VECTOR;
  static const std::string IMS_SUBSCRIPTION;
  static const ServerCapabilities CAPABILITIES;
  static const ServerCapabilities NO_CAPABILITIES;
  static const ServerCapabilities CAPABILITIES_WITH_SERVER_NAME;
  static const int32_t AUTH_SESSION_STATE;
  static const std::string ASSOCIATED_IDENTITY1;
  static const std::string ASSOCIATED_IDENTITY2;
  static std::vector<std::string> ASSOCIATED_IDENTITIES;
  static const std::string IMPU2;
  static const std::string IMPU3;
  static const std::string IMPU4;
  static std::vector<std::string> IMPUS;
  static std::vector<std::string> ASSOCIATED_IDENTITY1_IN_VECTOR;
  static const std::string IMPU_IMS_SUBSCRIPTION;
  static const std::string IMPU_IMS_SUBSCRIPTION2;
  static const std::string IMPU3_IMS_SUBSCRIPTION;
  static const std::string IMPU_IMS_SUBSCRIPTION_WITH_BARRING;
  static const std::string IMPU_IMS_SUBSCRIPTION_BARRING_INDICATION;
  static const std::string HTTP_PATH_REG_TRUE;
  static const std::string HTTP_PATH_REG_FALSE;
  static std::vector<std::string> EMPTY_VECTOR;
  static const std::string DEREG_BODY_PAIRINGS;
  static const std::string DEREG_BODY_PAIRINGS3;
  static const std::string DEREG_BODY_PAIRINGS4;
  static const std::string DEREG_BODY_LIST;
  static const std::deque<std::string> NO_CFS;
  static const std::deque<std::string> CCFS;
  static const std::deque<std::string> ECFS;
  static const ChargingAddresses NO_CHARGING_ADDRESSES;
  static const ChargingAddresses FULL_CHARGING_ADDRESSES;
  static const std::string TEL_URI;
  static const std::string TEL_URI2;
  static const std::string TEL_URIS_IMS_SUBSCRIPTION;
  static const std::string TEL_URIS_IMS_SUBSCRIPTION_WITH_BARRING;

  static Cx::Dictionary* _cx_dict;
  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static HttpResolver* _mock_resolver;
  static MockHssCacheProcessor* _cache;
  static MockHttpStack* _httpstack;
  static MockHttpConnection* _mock_http_conn;
  static SproutConnection* _sprout_conn;


  // Used to catch diameter messages and transactions on the MockDiameterStack
  // so that we can inspect them.
  static struct msg* _caught_fd_msg;
  static Diameter::Transaction* _caught_diam_tsx;

  std::string test_str;
  int32_t test_i32;
  uint32_t test_u32;

  DiameterHandlersTest() {}
  virtual ~DiameterHandlersTest()
  {
    Mock::VerifyAndClear(_httpstack);
  }

  static void SetUpTestCase()
  {
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf", NULL);
    _cache = new MockHssCacheProcessor();
    _httpstack = new MockHttpStack();
    _mock_resolver = new FakeHttpResolver("1.2.3.4");
    _mock_http_conn = new MockHttpConnection(_mock_resolver);
    _sprout_conn = new SproutConnection(_mock_http_conn);
    TRC_ERROR("sr2sr2 abuot to do diameter");
    _mock_stack = new MockDiameterStack();
    TRC_ERROR("sr2sr2 abuot to do diameterdict");
    _cx_dict = new Cx::Dictionary();
  
    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _cache; _cache = NULL;
    delete _httpstack; _httpstack = NULL;
    delete _sprout_conn; _sprout_conn = NULL;
    delete _mock_resolver; _mock_resolver = NULL;
    delete _mock_stack; _mock_stack = NULL;
    delete _cx_dict; _cx_dict = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
  }


  // We frequently invoke the following method on the send method of our
  // MockDiameterStack in order to catch the Diameter message we're trying
  // to send.
  static void store_msg(struct msg* msg)
  {
    _caught_fd_msg = msg;
  }

  void rtr_template(int32_t dereg_reason,
                    std::string http_path,
                    std::string body,
                    HTTPCode http_ret_code,
                    bool use_impus)
  {
    // This is a template function for an RTR test
    Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                           _mock_stack,
                                           dereg_reason,
                                           IMPI,
                                           ASSOCIATED_IDENTITIES,
                                           use_impus ? IMPUS : EMPTY_VECTOR,
                                           AUTH_SESSION_STATE);

    // The free_on_delete flag controls whether we want to free the underlying
    // fd_msg structure when we delete this RTR. We don't, since this will be
    // freed when the answer is freed later in the test. If we leave this flag set
    // then the request will be freed twice.
    rtr._free_on_delete = false;

    RegistrationTerminationTask::Config cfg(_cache, _cx_dict, _sprout_conn);
    RegistrationTerminationTask* task = new RegistrationTerminationTask(_cx_dict, &rtr._fd_msg, &cfg, FAKE_TRAIL_ID);

    // We have to make sure the message is pointing at the mock stack.
    task->_msg._stack = _mock_stack;
    task->_rtr._stack = _mock_stack;

    std::vector<std::string> impis = { IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2 };

    // Expect to send a diameter message.
    EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
      .Times(1)
      .WillOnce(WithArgs<0>(Invoke(store_msg)));

    if (dereg_reason <= REMOVE_SCSCF)
    {
      // Valid dereg reason
      // Create the IRSs that will be returned
      // (the default IMPU of the IRS that IMPU2 is part of is IMPU3)
      FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
      irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
      irs->set_reg_state(RegistrationState::NOT_REGISTERED);
      irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
      irs->set_associated_impis(IMPI_IN_VECTOR);

      FakeImplicitRegistrationSet* irs2 = new FakeImplicitRegistrationSet(IMPU3);
      irs->set_ims_sub_xml(IMPU3_IMS_SUBSCRIPTION);
      irs->set_reg_state(RegistrationState::NOT_REGISTERED);
      irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
      irs->set_associated_impis(IMPI_IN_VECTOR);

      std::vector<ImplicitRegistrationSet*> irss = { irs2, irs };

      // The cache lookup depends on whether we have a list of impus and the reason
      if ((use_impus) && ((dereg_reason == PERMANENT_TERMINATION) ||
                          (dereg_reason == REMOVE_SCSCF) ||
                          (dereg_reason == SERVER_CHANGE) ||
                          (dereg_reason == NEW_SERVER_ASSIGNED)))
      {
        // Expect a cache lookup using the provided list of IMPUs
        EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impus(_, _, IMPUS, FAKE_TRAIL_ID))
          .WillOnce(InvokeArgument<0>(irss));
      }
      else
      {
        // Expect a cache lookup using the list of IMPIs
        EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impis(_, _, impis, FAKE_TRAIL_ID))
          .WillOnce(InvokeArgument<0>(irss));
      }

      // Expect a delete to be sent to Sprout.
      EXPECT_CALL(*_mock_http_conn, send_delete(http_path, _, body))
        .Times(1)
        .WillOnce(Return(http_ret_code)).RetiresOnSaturation();

      // Expect deletions for each IRS
      EXPECT_CALL(*_cache, delete_implicit_registration_sets(_, _, irss, FAKE_TRAIL_ID))
        .WillOnce(InvokeArgument<0>());
    }
    else
    {
      // Invalid dereg reason - we'll send a FAILURE response
    }

    // Run the task
    task->run();

    // Turn the caught Diameter msg structure into a RTA and confirm its contents.
    Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
    Cx::RegistrationTerminationAnswer rta(msg);
    EXPECT_TRUE(rta.result_code(test_i32));
    if (http_ret_code == HTTP_OK && dereg_reason <= REMOVE_SCSCF)
    {
      EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
    }
    else
    {
      EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
    }
    EXPECT_EQ(impis, rta.associated_identities());
    EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
  }

  // PPR function templates
  void ppr_setup(PushProfileTask** ptask,
		 PushProfileTask::Config** pcfg,
                 std::string impi,
                 std::string ims_subscription,
                 ChargingAddresses charging_addresses)
  {
    Cx::PushProfileRequest ppr(_cx_dict,
                               _mock_stack,
                               impi,
                               ims_subscription,
                               charging_addresses,
                               AUTH_SESSION_STATE);

    // The free_on_delete flag controls whether we want to free the underlying
    // fd_msg structure when we delete this PPR. We don't, since this will be
    // freed when the answer is freed later in the test. If we leave this flag set
    // then the request will be freed twice.
    ppr._free_on_delete = false;

    *pcfg = new PushProfileTask::Config(_cache, _cx_dict);
    *ptask = new PushProfileTask(_cx_dict, &ppr._fd_msg, *pcfg, FAKE_TRAIL_ID);

    // We have to make sure the message is pointing at the mock stack.
    (*ptask)->_msg._stack = _mock_stack;
    (*ptask)->_ppr._stack = _mock_stack;
  }

  void ppr_expect_ppa()
  {
    // Expect to send a PPA
    EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
      .Times(1)
      .WillOnce(WithArgs<0>(Invoke(store_msg)));
  }

  void ppr_check_ppa(int success_or_failure)
  {  
    Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
    Cx::PushProfileAnswer ppa(msg);
    EXPECT_TRUE(ppa.result_code(test_i32));
    EXPECT_EQ(success_or_failure, test_i32);
    EXPECT_EQ(AUTH_SESSION_STATE, ppa.auth_session_state());
  }

  void ppr_tear_down(PushProfileTask::Config* pcfg)
  {
    delete pcfg;
  }
};

const std::string DiameterHandlersTest::SERVER_NAME = "scscf";
const std::string DiameterHandlersTest::IMPI = "_impi@example.com";
const std::string DiameterHandlersTest::IMPU = "sip:impu@example.com";
const std::string DiameterHandlersTest::IMPU2 = "sip:impu2@example.com";
const std::string DiameterHandlersTest::IMPU3 = "sip:impu3@example.com";
const std::string DiameterHandlersTest::IMPU4 = "sip:impu4@example.com";
const std::string DiameterHandlersTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities DiameterHandlersTest::CAPABILITIES(mandatory_capabilities, optional_capabilities, "");
const ServerCapabilities DiameterHandlersTest::NO_CAPABILITIES(no_capabilities, no_capabilities, "");
const ServerCapabilities DiameterHandlersTest::CAPABILITIES_WITH_SERVER_NAME(no_capabilities, no_capabilities, SERVER_NAME);
const int32_t DiameterHandlersTest::AUTH_SESSION_STATE = 1;
const std::string DiameterHandlersTest::ASSOCIATED_IDENTITY1 = "associated_identity1@example.com";
const std::string DiameterHandlersTest::ASSOCIATED_IDENTITY2 = "associated_identity2@example.com";
std::vector<std::string> DiameterHandlersTest::ASSOCIATED_IDENTITIES = {ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
std::vector<std::string> DiameterHandlersTest::IMPUS = {IMPU, IMPU2};
std::vector<std::string> DiameterHandlersTest::IMPU_IN_VECTOR = {IMPU};
std::vector<std::string> DiameterHandlersTest::IMPI_IN_VECTOR = {IMPI};
std::vector<std::string> DiameterHandlersTest::ASSOCIATED_IDENTITY1_IN_VECTOR = {ASSOCIATED_IDENTITY1};
const std::string DiameterHandlersTest::IMPU_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU4 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string DiameterHandlersTest::IMPU_IMS_SUBSCRIPTION2 = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string DiameterHandlersTest::IMPU3_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU3 + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string DiameterHandlersTest::IMPU_IMS_SUBSCRIPTION_WITH_BARRING = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string DiameterHandlersTest::IMPU_IMS_SUBSCRIPTION_BARRING_INDICATION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity><BarringIndication>0</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
std::vector<std::string> DiameterHandlersTest::EMPTY_VECTOR = {};
const std::string DiameterHandlersTest::DEREG_BODY_PAIRINGS = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string DiameterHandlersTest::DEREG_BODY_LIST = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\"},{\"primary-impu\":\"" + IMPU + "\"}]}";
// These are effectively the same as above, but depending on the exact code path the ordering of IMPUS can be different.
const std::string DiameterHandlersTest::DEREG_BODY_PAIRINGS3 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string DiameterHandlersTest::DEREG_BODY_PAIRINGS4 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";

const std::deque<std::string> DiameterHandlersTest::NO_CFS = {};
const std::deque<std::string> DiameterHandlersTest::ECFS = {"ecf1", "ecf"};
const std::deque<std::string> DiameterHandlersTest::CCFS = {"ccf1", "ccf2"};
const ChargingAddresses DiameterHandlersTest::NO_CHARGING_ADDRESSES(NO_CFS, NO_CFS);
const ChargingAddresses DiameterHandlersTest::FULL_CHARGING_ADDRESSES(CCFS, ECFS);
const std::string DiameterHandlersTest::TEL_URI = "tel:123";
const std::string DiameterHandlersTest::TEL_URI2 = "tel:321";
const std::string DiameterHandlersTest::TEL_URIS_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + TEL_URI + "</Identity></PublicIdentity><PublicIdentity><Identity>" + TEL_URI2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string DiameterHandlersTest::TEL_URIS_IMS_SUBSCRIPTION_WITH_BARRING = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + TEL_URI + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + TEL_URI2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";

const std::string DiameterHandlersTest::HTTP_PATH_REG_TRUE = "/registrations?send-notifications=true";
const std::string DiameterHandlersTest::HTTP_PATH_REG_FALSE = "/registrations?send-notifications=false";

msg* DiameterHandlersTest::_caught_fd_msg = NULL;
Cx::Dictionary* DiameterHandlersTest::_cx_dict = NULL;
Diameter::Stack* DiameterHandlersTest::_real_stack = NULL;
MockDiameterStack* DiameterHandlersTest::_mock_stack = NULL;
HttpResolver* DiameterHandlersTest::_mock_resolver = NULL;
MockHssCacheProcessor* DiameterHandlersTest::_cache = NULL;
MockHttpStack* DiameterHandlersTest::_httpstack = NULL;
MockHttpConnection* DiameterHandlersTest::_mock_http_conn = NULL;
SproutConnection* DiameterHandlersTest::_sprout_conn = NULL;

//
// RegistrationTermination tests
//

// Test mainline RTRs with various reasons
TEST_F(DiameterHandlersTest, RTRPermanentTermination)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_OK, true);
}

TEST_F(DiameterHandlersTest, RTRRemoveSCSCF)
{
  rtr_template(REMOVE_SCSCF, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST, HTTP_OK, true);
}

TEST_F(DiameterHandlersTest, RTRPermanentTerminationNoImpus)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_OK, false);
}

TEST_F(DiameterHandlersTest, RTRRemoveSCSCFNoIMPUS)
{
  rtr_template(REMOVE_SCSCF, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST, HTTP_OK, false);
}

TEST_F(DiameterHandlersTest, RTRServerChange)
{
  rtr_template(SERVER_CHANGE, HTTP_PATH_REG_TRUE, DEREG_BODY_LIST, HTTP_OK, false);
}

TEST_F(DiameterHandlersTest,RTRNewServerAssigned)
{
  rtr_template(NEW_SERVER_ASSIGNED, HTTP_PATH_REG_FALSE, DEREG_BODY_LIST, HTTP_OK, false);
}

TEST_F(DiameterHandlersTest, RTRUnknownReason)
{
  rtr_template(9, "", "", 0, true);
}

// Test RTRs with HTTP errors from Sprout
TEST_F(DiameterHandlersTest, RTRHTTPBadMethod)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_BADMETHOD, true);
}

TEST_F(DiameterHandlersTest, RTRHTTPBadResult)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_BAD_REQUEST, true);
}

TEST_F(DiameterHandlersTest, RTRHTTPServerError)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, HTTP_SERVER_ERROR, true);
}

TEST_F(DiameterHandlersTest, RTRHTTPUnknownError)
{
  rtr_template(PERMANENT_TERMINATION, HTTP_PATH_REG_FALSE, DEREG_BODY_PAIRINGS, 999, true);
}

TEST_F(DiameterHandlersTest, RTRIncludesBarredImpus)
{
  // Test that the correct delete request is passed to Sprout and the correct
  // data is removed from the cache when the first impu in an IRS is barred
  // (and so is not the default IMPU for that IRS)
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPU_IN_VECTOR,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationTask::Config cfg(_cache, _cx_dict, _sprout_conn);
  RegistrationTerminationTask* task = new RegistrationTerminationTask(_cx_dict, &rtr._fd_msg, &cfg, FAKE_TRAIL_ID);

  // We have to make sure the message is pointing at the mock stack.
  task->_msg._stack = _mock_stack;
  task->_rtr._stack = _mock_stack;

  // Expect to send a diameter message.
  EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::vector<std::string> impis = { IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2 };

  // Create the IRS that will be returned
  // The default IMPU of the IRS is IMPU2 as IMPU is barred
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU2);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION_WITH_BARRING);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->set_associated_impis(IMPI_IN_VECTOR);

  std::vector<ImplicitRegistrationSet*> irss = { irs };

  EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impus(_, _, IMPU_IN_VECTOR, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irss));

  // Expect a delete to be sent to Sprout.
  EXPECT_CALL(*_mock_http_conn, send_delete(HTTP_PATH_REG_FALSE, _, DEREG_BODY_PAIRINGS3))
    .Times(1)
    .WillOnce(Return(200)).RetiresOnSaturation();

  // Expect deletions for each IRS
  EXPECT_CALL(*_cache, delete_implicit_registration_sets(_, _, irss, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  task->run();

  // Turn the caught Diameter msg structure into a RTA and confirm its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(impis, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
}

TEST_F(DiameterHandlersTest, RTRIncludesBarringIndication)
{
  // Test that the correct delete request is passed to Sprout and the correct
  // data is removed from the cache when the first impu in an IRS is not barred
  // but has a barring indication
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPU_IN_VECTOR,
                                         AUTH_SESSION_STATE);

  // The free_on_delete flag controls whether we want to free the underlying
  // fd_msg structure when we delete this RTR. We don't, since this will be
  // freed when the answer is freed later in the test. If we leave this flag set
  // then the request will be freed twice.
  rtr._free_on_delete = false;

  RegistrationTerminationTask::Config cfg(_cache, _cx_dict, _sprout_conn);
  RegistrationTerminationTask* task = new RegistrationTerminationTask(_cx_dict, &rtr._fd_msg, &cfg, FAKE_TRAIL_ID);

  // We have to make sure the message is pointing at the mock stack.
  task->_msg._stack = _mock_stack;
  task->_rtr._stack = _mock_stack;

  // Expect to send a diameter message.
  EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::vector<std::string> impis = { IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2 };

  // Create the IRS that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION_BARRING_INDICATION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->set_associated_impis(IMPI_IN_VECTOR);

  std::vector<ImplicitRegistrationSet*> irss = { irs };

  EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impus(_, _, IMPU_IN_VECTOR, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irss));

  // Expect a delete to be sent to Sprout.
  EXPECT_CALL(*_mock_http_conn, send_delete(HTTP_PATH_REG_FALSE, _, DEREG_BODY_PAIRINGS4))
    .Times(1)
    .WillOnce(Return(200)).RetiresOnSaturation();

  // Expect deletions for each IRS
  EXPECT_CALL(*_cache, delete_implicit_registration_sets(_, _, irss, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  task->run();

  // Turn the caught Diameter msg structure into a RTA and confirm its contents.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
  EXPECT_EQ(impis, rta.associated_identities());
  EXPECT_EQ(AUTH_SESSION_STATE, rta.auth_session_state());
}

TEST_F(DiameterHandlersTest, RTRNoRegSets)
{
  // Test that no IRSs found for an RTR request result in no contact to Sprout
  // but still give SUCCESS on the RTA
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

  RegistrationTerminationTask::Config cfg(_cache, _cx_dict, _sprout_conn);
  RegistrationTerminationTask* task = new RegistrationTerminationTask(_cx_dict, &rtr._fd_msg, &cfg, FAKE_TRAIL_ID);

  // We have to make sure the message is pointing at the mock stack.
  task->_msg._stack = _mock_stack;
  task->_rtr._stack = _mock_stack;

  // Expect to send a diameter message.
  EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::vector<std::string> impis = { IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2 };

  // The cache returns an empty vectory
  std::vector<ImplicitRegistrationSet*> irss = {};
  EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impus(_, _, IMPUS, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irss));

  task->run();

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_SUCCESS, test_i32);
}

TEST_F(DiameterHandlersTest, RTRCacheError)
{
  // Test that a cache error triggers a Diameter failure response
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

  RegistrationTerminationTask::Config cfg(_cache, _cx_dict, _sprout_conn);
  RegistrationTerminationTask* task = new RegistrationTerminationTask(_cx_dict, &rtr._fd_msg, &cfg, FAKE_TRAIL_ID);

  // We have to make sure the message is pointing at the mock stack.
  task->_msg._stack = _mock_stack;
  task->_rtr._stack = _mock_stack;

  // Expect to send a diameter message.
  EXPECT_CALL(*_mock_stack, send(_, FAKE_TRAIL_ID))
    .Times(1)
    .WillOnce(WithArgs<0>(Invoke(store_msg)));

  std::vector<std::string> impis = { IMPI, ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2 };

  // The cache will return ERROR
  EXPECT_CALL(*_cache, get_implicit_registration_sets_for_impus(_, _, IMPUS, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::ERROR));

  task->run();

  // Turn the caught Diameter msg structure into a RTA and confirm the result
  // code is correct.
  Diameter::Message msg(_cx_dict, _caught_fd_msg, _mock_stack);
  Cx::RegistrationTerminationAnswer rta(msg);
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(DIAMETER_UNABLE_TO_COMPLY, test_i32);
}

//
// Push Profile tests
//

TEST_F(DiameterHandlersTest, PPRMainline)
{
  // Successful update on single IRS with charging addresses and XML
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMS_SUBSCRIPTION, FULL_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(FULL_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription
  EXPECT_CALL(*sub, get_irs_for_default_impu(IMPU)).WillOnce(Return(irs));

  // And that we'll set the charging addresses on the ImsSubscription
  EXPECT_CALL(*sub, set_charging_addrs(AllOf(Field(&ChargingAddresses::ecfs, FULL_CHARGING_ADDRESSES.ecfs),
                                             Field(&ChargingAddresses::ccfs, FULL_CHARGING_ADDRESSES.ccfs))))
    .Times(1);

  // We'll then save the ImsSubscription in the cache
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());
  
  ppr_expect_ppa();

  task->run();

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
  delete sub;
}


TEST_F(DiameterHandlersTest, PPRChangeIDs)
{
  // This PPR contains an IMS subscription and charging addresses. One IMPU
  // is being deleted from the IRS and one is being added. There is only one IRS
  // The update is successful
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMPU_IMS_SUBSCRIPTION, FULL_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  // The IRS has different XML to that on the PPR
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION2);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(FULL_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription
  EXPECT_CALL(*sub, get_irs_for_default_impu(IMPU)).WillOnce(Return(irs));

  // And that we'll set the charging addresses on the ImsSubscription
  EXPECT_CALL(*sub, set_charging_addrs(AllOf(Field(&ChargingAddresses::ecfs, FULL_CHARGING_ADDRESSES.ecfs),
                                             Field(&ChargingAddresses::ccfs, FULL_CHARGING_ADDRESSES.ccfs))))
    .Times(1);

  // We'll then save the ImsSubscription in the cache
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  ppr_expect_ppa();

  task->run();

  // Check that the IRS was updated with the new XML
  EXPECT_EQ(irs->get_ims_sub_xml(), IMPU_IMS_SUBSCRIPTION);

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
  delete sub;
}

TEST_F(DiameterHandlersTest, PPRChargingAddrs)
{
  // This PPR has a charging address but no IMS Sub. There is one IRS.
  // The update is successful.
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, "", FULL_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll set the charging addresses on the ImsSubscription
  EXPECT_CALL(*sub, set_charging_addrs(AllOf(Field(&ChargingAddresses::ecfs, FULL_CHARGING_ADDRESSES.ecfs),
                                             Field(&ChargingAddresses::ccfs, FULL_CHARGING_ADDRESSES.ccfs))))
    .Times(1);

  // We'll then save the ImsSubscription in the cache
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  ppr_expect_ppa();

  task->run();

  // Check that the IRS still has the correct XML
  EXPECT_EQ(irs->get_ims_sub_xml(), IMPU_IMS_SUBSCRIPTION);

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
  delete sub;
}

TEST_F(DiameterHandlersTest, PPRImsSub)
{
  // This PPR contains an IMS Sub but no charging addresses.
  // The update is successful.
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMS_SUBSCRIPTION, NO_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription
  EXPECT_CALL(*sub, get_irs_for_default_impu(IMPU)).WillOnce(Return(irs));

  // We'll then save the ImsSubscription in the cache
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  ppr_expect_ppa();

  task->run();

  // Check that the IRS was updated with the new XML
  EXPECT_EQ(irs->get_ims_sub_xml(), IMS_SUBSCRIPTION);

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
  delete sub;
}

TEST_F(DiameterHandlersTest, PPRIMSSubNoSIPURI)
{
  // This PPR contains an IMS Subscription with no SIP URIs.
  CapturingTestLogger log;

  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, TEL_URIS_IMS_SUBSCRIPTION, NO_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(TEL_URI);
  irs->set_ims_sub_xml(TEL_URIS_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription
  EXPECT_CALL(*sub, get_irs_for_default_impu(TEL_URI)).WillOnce(Return(irs));

  // We'll then save the ImsSubscription in the cache
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>());

  ppr_expect_ppa();

  task->run();

  // Check for the log indicating there were no SIP URIs in the IRS.
  EXPECT_TRUE(log.contains("No SIP URI in Implicit Registration Set"));

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
  delete sub;
}


TEST_F(DiameterHandlersTest, PPRCacheFailure)
{
  // This PPR contains an IMS Subscription. There is a cache failure
  // when attempting to update the cache. A PPA is sent indicating failure.
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMS_SUBSCRIPTION, NO_CHARGING_ADDRESSES);

  // Create the ImsSubscription that will be returned
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription
  EXPECT_CALL(*sub, get_irs_for_default_impu(IMPU)).WillOnce(Return(irs));

  // We'll then save the ImsSubscription in the cache, which will give an error
  EXPECT_CALL(*_cache, put_ims_subscription(_, _, sub, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::ERROR));

  ppr_expect_ppa();

  task->run();

  ppr_check_ppa(DIAMETER_UNABLE_TO_COMPLY);
  ppr_tear_down(pcfg);
  delete sub;
}

TEST_F(DiameterHandlersTest, PPRGetRegSetFailure)
{
  // This PPR contains an IMS Subscription. There is a failure in obtaining
  // the IMS Subscription from the cache.
  // A PPA is sent indicating failure.
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMS_SUBSCRIPTION, NO_CHARGING_ADDRESSES);

  // Expect that we'll look up the ImsSubscription for the provided IMPI, which
  // will fail
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::ERROR));

  ppr_expect_ppa();

  task->run();

  ppr_check_ppa(DIAMETER_UNABLE_TO_COMPLY);
  ppr_tear_down(pcfg);
}

TEST_F(DiameterHandlersTest, PPRNoImsSubNoChargingAddrs)
{
  // This PPR contains neither an IMS subscription or charging addresses.
  // A PPA is sent indicating success, since there is no need to update anything
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, "", NO_CHARGING_ADDRESSES);

  ppr_expect_ppa();

  task->run();

  ppr_check_ppa(DIAMETER_SUCCESS);
  ppr_tear_down(pcfg);
}

TEST_F(DiameterHandlersTest, PPRChangesDefaultRejected)
{
  // Test that when a PPR is received with a different default public id than
  // the one stored in the cache, it is rejected with a PPA with the error
  // DIAMETER_UNABLE_TO_COMPLY.
  PushProfileTask* task = NULL;
  PushProfileTask::Config* pcfg = NULL;
  ppr_setup(&task, &pcfg, IMPI, IMS_SUBSCRIPTION, FULL_CHARGING_ADDRESSES);

  MockImsSubscription* sub = new MockImsSubscription();

  // Expect that we'll look up the ImsSubscription for the provided IMPI
  EXPECT_CALL(*_cache, get_ims_subscription(_, _, IMPI, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(sub));

  // Expect that we'll request the IRS for the default IMPU from the ImsSubscription,
  // which doesn't find a match
  EXPECT_CALL(*sub, get_irs_for_default_impu(IMPU)).WillOnce(ReturnNull());

  ppr_expect_ppa();

  task->run();

  ppr_check_ppa(DIAMETER_UNABLE_TO_COMPLY);
  ppr_tear_down(pcfg);
}
