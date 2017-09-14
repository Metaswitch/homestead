/**
 * @file http_handlers_test.cpp UT for HTTP Handlers module.
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
#include "http_handlers.h"
#include "mockstatisticsmanager.hpp"
#include "sproutconnection.h"
#include "mock_health_checker.hpp"
#include "fakesnmp.hpp"
#include "base64.h"

//#include "fakehssconnection.hpp"
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
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::AtLeast;
using ::testing::Field;
using ::testing::Property;
using ::testing::AllOf;
using ::testing::ByRef;
using ::testing::ReturnNull;

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

// Fixture for HTTPHandlersTest.
class HTTPHandlersTest : public testing::Test
{
public:
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const std::string DEFAULT_SERVER_NAME;
  static const std::string PROVIDED_SERVER_NAME;
  static const std::string SERVER_NAME;
  static const std::string WILDCARD;
  static const std::string NEW_WILDCARD;
  static const std::string IMPI;
  static const std::string IMPU;
  static std::vector<std::string> IMPU_IN_VECTOR;
  static std::vector<std::string> IMPU2_IN_VECTOR;
  static std::vector<std::string> IMPU3_IN_VECTOR;
  static std::vector<std::string> IMPI_IN_VECTOR;
  static const std::string IMS_SUBSCRIPTION;
  static const std::string REGDATA_RESULT;
  static const std::string REGDATA_RESULT_INCLUDES_BARRING;
  static const std::string REGDATA_RESULT_UNREG;
  static const std::string REGDATA_RESULT_DEREG;
  static const std::string REGDATA_BLANK_RESULT_DEREG;
  static const std::string VISITED_NETWORK;
  static const std::string AUTH_TYPE_DEREG;
  static const std::string AUTH_TYPE_CAPAB;
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
  static const std::string IMPU5;
  static const std::string IMPU6;
  static std::vector<std::string> IMPU_TEST;
  static std::vector<std::string> IMPUS;
  static std::vector<std::string> IMPU_LIST;
  static std::vector<std::string> THREE_DEFAULT_IMPUS;
  static std::vector<std::string> THREE_DEFAULT_IMPUS2;
  static std::vector<std::string> ASSOCIATED_IDENTITY1_IN_VECTOR;
  static std::vector<std::string> IMPU_REG_SET;
  static std::vector<std::string> IMPU_REG_SET2;
  static std::vector<std::string> IMPU3_REG_SET;
  static std::vector<std::string> IMPU5_REG_SET;
  static const std::string IMPU_IMS_SUBSCRIPTION;
  static const std::string IMPU_IMS_SUBSCRIPTION2;
  static const std::string IMPU_IMS_SUBSCRIPTION_INVALID;
  static const std::string IMPU3_IMS_SUBSCRIPTION;
  static const std::string IMPU5_IMS_SUBSCRIPTION;
  static const std::string IMPU_IMS_SUBSCRIPTION_WITH_BARRING;
  static const std::string IMPU_IMS_SUBSCRIPTION_WITH_BARRING2;
  static const std::string IMPU_IMS_SUBSCRIPTION_WITH_BARRING3;
  static const std::string IMPU_IMS_SUBSCRIPTION_BARRING_INDICATION;
  static const std::string SCHEME_UNKNOWN;
  static const std::string SCHEME_DIGEST;
  static const std::string SCHEME_AKA;
  static const std::string SCHEME_AKAV2;
  static const std::string SIP_AUTHORIZATION;
  static const std::string HTTP_PATH_REG_TRUE;
  static const std::string HTTP_PATH_REG_FALSE;
  static std::vector<std::string> EMPTY_VECTOR;
  static const std::string DEREG_BODY_PAIRINGS;
  static const std::string DEREG_BODY_PAIRINGS2;
  static const std::string DEREG_BODY_PAIRINGS3;
  static const std::string DEREG_BODY_PAIRINGS4;
  static const std::string DEREG_BODY_PAIRINGS5;
  static const std::string DEREG_BODY_LIST;
  static const std::string DEREG_BODY_LIST2;
  static const std::string DEREG_BODY_LIST3;
  static const std::deque<std::string> NO_CFS;
  static const std::deque<std::string> CCFS;
  static const std::deque<std::string> ECFS;
  static const ChargingAddresses NO_CHARGING_ADDRESSES;
  static const ChargingAddresses FULL_CHARGING_ADDRESSES;
  static const std::string TEL_URI;
  static const std::string TEL_URI2;
  static const std::string TEL_URIS_IMS_SUBSCRIPTION;
  static const std::string TEL_URIS_IMS_SUBSCRIPTION_WITH_BARRING;
  static std::vector<std::string> TEL_URIS_IN_VECTOR;

  static Cx::Dictionary* _cx_dict;
  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static HttpResolver* _mock_resolver;
  static MockHssCacheProcessor* _cache;
  static MockHttpStack* _httpstack;
  static MockHttpConnection* _mock_http_conn;
  static SproutConnection* _sprout_conn;
  static MockHssConnection* _hss;



  // Used to catch diameter messages and transactions on the MockDiameterStack
  // so that we can inspect them.
  static struct msg* _caught_fd_msg;
  static Diameter::Transaction* _caught_diam_tsx;

  std::string test_str;
  int32_t test_i32;
  uint32_t test_u32;

  HTTPHandlersTest() {}
  virtual ~HTTPHandlersTest()
  {
    Mock::VerifyAndClear(_httpstack);
  }

  static void SetUpTestCase()
  {
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf", NULL);
    _cache = new MockHssCacheProcessor();
    _hss = new MockHssConnection();
    //_hss = new FakeHssConnection();
    _httpstack = new MockHttpStack();
    _mock_resolver = new FakeHttpResolver("1.2.3.4");
    _mock_http_conn = new MockHttpConnection(_mock_resolver);
    _sprout_conn = new SproutConnection(_mock_http_conn);
    _mock_stack = new MockDiameterStack();
    _cx_dict = new Cx::Dictionary();

    HssCacheTask::configure_hss_connection(_hss, DEFAULT_SERVER_NAME);
    HssCacheTask::configure_cache(_cache);

    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _cache; _cache = NULL;
    delete _hss; _hss = NULL;
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
        writer.String(JSON_VERSION.c_str());
        writer.Int(av.version);
      }
      writer.EndObject();
    }
    writer.EndObject();
    return sb.GetString();
  }

  static std::string build_icscf_json(int32_t rc,
                                      std::string scscf,
                                      ServerCapabilities capabs,
                                      std::string wildcard)
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
      if (!capabs.server_name.empty())
      {
        writer.String(JSON_SCSCF.c_str());
        writer.String(capabs.server_name.c_str());
      }
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

    if (!wildcard.empty())
    {
      writer.String(JSON_WILDCARD.c_str());
      writer.String(wildcard.c_str());
    }

    writer.EndObject();
    return sb.GetString();
  }

  MockHttpStack::Request make_request(const std::string& req_type,
                                      bool use_impi,
                                      bool use_server_name,
                                      bool use_wildcard)
  {
    std::string parameters = "";
    std::string server_name = "";
    std::string wildcard = "";

    if (use_impi)
    {
      parameters = "?private_id=" + IMPI;
    }

    if (use_server_name)
    {
      server_name = ", \"server_name\": \"" + SERVER_NAME + "\"";
    }

    if (use_wildcard)
    {
      wildcard = ", \"wildcard_identity\": \"" + WILDCARD + "\"";
    }

    return MockHttpStack::Request(_httpstack,
                                  "/impu/" + IMPU + "/reg-data",
                                  "",
                                  parameters,
                                  "{\"reqtype\": \"" + req_type +"\"" + server_name + wildcard + "}",
                                  htp_method_PUT);
  }

  // Template functions to test our processing when various error codes are returned by the HSS
  // from UARs and LIRs.
  static void registration_status_error_template(HssConnection::ResultCode hss_rc, int32_t http_rc)
  {
    // Build the HTTP request which will invoke a UAR to be sent to the HSS.
    MockHttpStack::Request req(_httpstack,
                                "/impi/" + IMPI + "/",
                                "registration-status",
                                "?impu=" + IMPU);

    ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
    ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

    // Once the task's run function is called, expect a UAR. We don't check the
    // contents of the UAR explicitly here, as this is done by other tests.
    HssConnection::UserAuthAnswer answer = HssConnection::UserAuthAnswer(hss_rc);
    EXPECT_CALL(*_hss, send_user_auth_request(_, _, _)).WillOnce(InvokeArgument<0>(ByRef(answer)));

    // Expect the correct HTTP code
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc, _));

    // Ensure that the HTTP body on the response is empty.
    EXPECT_EQ("", req.content());

    task->run();
  }

  static void location_info_error_template(HssConnection::ResultCode hss_rc, int32_t http_rc)
  {
    // Build the HTTP request which will invoke an LIR to be sent to the HSS.
    MockHttpStack::Request req(_httpstack,
                                "/impu/" + IMPU + "/",
                                "location",
                                "");


    ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
    ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

    // Once the task's run function is called, expect an LIR. We don't check the
    // contents of the LIR explicitly here, as this is done by other tests.
    HssConnection::LocationInfoAnswer answer = HssConnection::LocationInfoAnswer(hss_rc);
    EXPECT_CALL(*_hss, send_location_info_request(_, _, _)).WillOnce(InvokeArgument<0>(ByRef(answer)));

    // Expect the correct HTTP code
    EXPECT_CALL(*_httpstack, send_reply(_, http_rc, _));

    // Ensure that the HTTP body on the response is empty.
    EXPECT_EQ("", req.content());

    task->run();
  }
};

const std::string HTTPHandlersTest::DEST_REALM = "dest-realm";
const std::string HTTPHandlersTest::DEST_HOST = "dest-host";
const std::string HTTPHandlersTest::DEFAULT_SERVER_NAME = "sprout";
const std::string HTTPHandlersTest::PROVIDED_SERVER_NAME = "sprout-site2";
const std::string HTTPHandlersTest::SERVER_NAME = "scscf";
const std::string HTTPHandlersTest::WILDCARD = "sip:im!.*!@scscf";
const std::string HTTPHandlersTest::NEW_WILDCARD = "sip:newim!.*!@scscf";
const std::string HTTPHandlersTest::IMPI = "_impi@example.com";
const std::string HTTPHandlersTest::IMPU = "sip:impu@example.com";
const std::string HTTPHandlersTest::IMPU2 = "sip:impu2@example.com";
const std::string HTTPHandlersTest::IMPU3 = "sip:impu3@example.com";
const std::string HTTPHandlersTest::IMPU4 = "sip:impu4@example.com";
const std::string HTTPHandlersTest::IMPU5 = "sip:impu5@example.com";
const std::string HTTPHandlersTest::IMPU6 = "sip:impu6@example.com";
const std::string HTTPHandlersTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::REGDATA_RESULT = "<ClearwaterRegData>\n\t<RegistrationState>REGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HTTPHandlersTest::REGDATA_RESULT_INCLUDES_BARRING = "<ClearwaterRegData>\n\t<RegistrationState>REGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t\t<BarringIndication>1</BarringIndication>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU2 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HTTPHandlersTest::REGDATA_RESULT_DEREG = "<ClearwaterRegData>\n\t<RegistrationState>NOT_REGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HTTPHandlersTest::REGDATA_BLANK_RESULT_DEREG = "<ClearwaterRegData>\n\t<RegistrationState>NOT_REGISTERED</RegistrationState>\n</ClearwaterRegData>\n\n";
const std::string HTTPHandlersTest::REGDATA_RESULT_UNREG = "<ClearwaterRegData>\n\t<RegistrationState>UNREGISTERED</RegistrationState>\n\t<IMSSubscription>\n\t\t<PrivateID>" + IMPI + "</PrivateID>\n\t\t<ServiceProfile>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t\t<PublicIdentity>\n\t\t\t\t<Identity>" + IMPU4 + "</Identity>\n\t\t\t</PublicIdentity>\n\t\t</ServiceProfile>\n\t</IMSSubscription>\n</ClearwaterRegData>\n\n";
const std::string HTTPHandlersTest::VISITED_NETWORK = "visited-network.com";
const std::string HTTPHandlersTest::AUTH_TYPE_DEREG = "DEREG";
const std::string HTTPHandlersTest::AUTH_TYPE_CAPAB = "CAPAB";
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities HTTPHandlersTest::CAPABILITIES(mandatory_capabilities, optional_capabilities, "");
const ServerCapabilities HTTPHandlersTest::NO_CAPABILITIES(no_capabilities, no_capabilities, "");
const ServerCapabilities HTTPHandlersTest::CAPABILITIES_WITH_SERVER_NAME(no_capabilities, no_capabilities, SERVER_NAME);
const int32_t HTTPHandlersTest::AUTH_SESSION_STATE = 1;
const std::string HTTPHandlersTest::ASSOCIATED_IDENTITY1 = "associated_identity1@example.com";
const std::string HTTPHandlersTest::ASSOCIATED_IDENTITY2 = "associated_identity2@example.com";
std::vector<std::string> HTTPHandlersTest::ASSOCIATED_IDENTITIES = {ASSOCIATED_IDENTITY1, ASSOCIATED_IDENTITY2};
std::vector<std::string> HTTPHandlersTest::IMPU_TEST = {IMPU};
std::vector<std::string> HTTPHandlersTest::IMPUS = {IMPU, IMPU2};
std::vector<std::string> HTTPHandlersTest::IMPU_LIST = {IMPU2, IMPU};
std::vector<std::string> HTTPHandlersTest::THREE_DEFAULT_IMPUS = {IMPU, IMPU2, IMPU3};
std::vector<std::string> HTTPHandlersTest::THREE_DEFAULT_IMPUS2 = {IMPU, IMPU3, IMPU5};
std::vector<std::string> HTTPHandlersTest::IMPU_IN_VECTOR = {IMPU};
std::vector<std::string> HTTPHandlersTest::IMPU2_IN_VECTOR = {IMPU2};
std::vector<std::string> HTTPHandlersTest::IMPU3_IN_VECTOR = {IMPU3};
std::vector<std::string> HTTPHandlersTest::IMPI_IN_VECTOR = {IMPI};
std::vector<std::string> HTTPHandlersTest::ASSOCIATED_IDENTITY1_IN_VECTOR = {ASSOCIATED_IDENTITY1};
std::vector<std::string> HTTPHandlersTest::IMPU_REG_SET = {IMPU, IMPU4};
std::vector<std::string> HTTPHandlersTest::IMPU_REG_SET2 = {IMPU, IMPU2};
std::vector<std::string> HTTPHandlersTest::IMPU3_REG_SET = {IMPU3, IMPU2};
std::vector<std::string> HTTPHandlersTest::IMPU5_REG_SET = {IMPU5, IMPU6};
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU4 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION2 = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION_INVALID = "<?xml version=\"1.0\"?><IMSSubscriptio></IMSSubscriptio>";
const std::string HTTPHandlersTest::IMPU3_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU3 + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU5_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU5 + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU6 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION_WITH_BARRING = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION_WITH_BARRING2 = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + IMPU4 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION_WITH_BARRING3 = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity><PublicIdentity><Identity>" + IMPU4 + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::IMPU_IMS_SUBSCRIPTION_BARRING_INDICATION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity><BarringIndication>0</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + IMPU2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
std::vector<std::string> HTTPHandlersTest::EMPTY_VECTOR = {};
const std::string HTTPHandlersTest::DEREG_BODY_PAIRINGS = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                      "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HTTPHandlersTest::DEREG_BODY_LIST = "{\"registrations\":[{\"primary-impu\":\"" + IMPU3 + "\"},{\"primary-impu\":\"" + IMPU + "\"}]}";
// These are effectively the same as above, but depending on the exact code path the ordering of IMPUS can be different.
const std::string HTTPHandlersTest::DEREG_BODY_PAIRINGS2 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HTTPHandlersTest::DEREG_BODY_LIST2 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\"},{\"primary-impu\":\"" + IMPU3 + "\"}]}";
const std::string HTTPHandlersTest::SCHEME_UNKNOWN = "Unknwon";
const std::string HTTPHandlersTest::DEREG_BODY_PAIRINGS3 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU2 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HTTPHandlersTest::DEREG_BODY_LIST3 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU2 + "\"}]}";
const std::string HTTPHandlersTest::DEREG_BODY_PAIRINGS4 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HTTPHandlersTest::DEREG_BODY_PAIRINGS5 = "{\"registrations\":[{\"primary-impu\":\"" + IMPU4 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU4 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU4 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + IMPI +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY1 +
                                                                       "\"},{\"primary-impu\":\"" + IMPU3 + "\",\"impi\":\"" + ASSOCIATED_IDENTITY2 + "\"}]}";
const std::string HTTPHandlersTest::SCHEME_DIGEST = "SIP Digest";
const std::string HTTPHandlersTest::SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string HTTPHandlersTest::SCHEME_AKAV2 = "Digest-AKAv2-SHA-256";
const std::string HTTPHandlersTest::SIP_AUTHORIZATION = "Authorization";
const std::deque<std::string> HTTPHandlersTest::NO_CFS = {};
const std::deque<std::string> HTTPHandlersTest::ECFS = {"ecf1", "ecf"};
const std::deque<std::string> HTTPHandlersTest::CCFS = {"ccf1", "ccf2"};
const ChargingAddresses HTTPHandlersTest::NO_CHARGING_ADDRESSES(NO_CFS, NO_CFS);
const ChargingAddresses HTTPHandlersTest::FULL_CHARGING_ADDRESSES(CCFS, ECFS);
const std::string HTTPHandlersTest::TEL_URI = "tel:123";
const std::string HTTPHandlersTest::TEL_URI2 = "tel:321";
const std::string HTTPHandlersTest::TEL_URIS_IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + TEL_URI + "</Identity></PublicIdentity><PublicIdentity><Identity>" + TEL_URI2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string HTTPHandlersTest::TEL_URIS_IMS_SUBSCRIPTION_WITH_BARRING = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + TEL_URI + "</Identity><BarringIndication>1</BarringIndication></PublicIdentity><PublicIdentity><Identity>" + TEL_URI2 + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
std::vector<std::string> HTTPHandlersTest::TEL_URIS_IN_VECTOR = {TEL_URI, TEL_URI2};

const std::string HTTPHandlersTest::HTTP_PATH_REG_TRUE = "/registrations?send-notifications=true";
const std::string HTTPHandlersTest::HTTP_PATH_REG_FALSE = "/registrations?send-notifications=false";

msg* HTTPHandlersTest::_caught_fd_msg = NULL;
Cx::Dictionary* HTTPHandlersTest::_cx_dict = NULL;
Diameter::Stack* HTTPHandlersTest::_real_stack = NULL;
MockDiameterStack* HTTPHandlersTest::_mock_stack = NULL;
HttpResolver* HTTPHandlersTest::_mock_resolver = NULL;
MockHssCacheProcessor* HTTPHandlersTest::_cache = NULL;
//FakeHssConnection* HTTPHandlersTest::_hss = NULL;
MockHssConnection* HTTPHandlersTest::_hss = NULL;
MockHttpStack* HTTPHandlersTest::_httpstack = NULL;
MockHttpConnection* HTTPHandlersTest::_mock_http_conn = NULL;
SproutConnection* HTTPHandlersTest::_sprout_conn = NULL;

//
// Digest and AV tests
//

TEST_F(HTTPHandlersTest, ImpiDigestMainline)
{
  // Test that an IMPI Digest Task requests the AV from the HSS and returns it
  // on the response
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a fake digest to be returned from the HSS
  DigestAuthVector* digest = new DigestAuthVector();
  digest->ha1 = "ha1";
  digest->realm = "realm";
  digest->qop = "qop";

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                            digest,
                                            SCHEME_DIGEST);

  // Expect that the MAR has the correct IMPI, IMPU and scheme

  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(build_digest_json(*digest), req.content());
}

TEST_F(HTTPHandlersTest, ImpiDigestNoImpu)
{
  // Tests IMPI Digest task with no IMPU specified
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "");

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, HTTP_NOT_FOUND, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssTimeout)
{
  // Tests IMPI Digest task when HSS times out the request
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SERVER_UNAVAILABLE);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 503
  EXPECT_CALL(*_httpstack, send_reply(_, 503, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssBusy)
{
  // Tests IMPI Digest task when HSS times out the request
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::TIMEOUT);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 504
  EXPECT_CALL(*_httpstack, send_reply(_, 504, _));

  // For a timeout, we expect to record a penalty
  EXPECT_CALL(*_httpstack, record_penalty()).Times(1);

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssUserUnknown)
{
  // Tests IMPI Digest task when HSS returns user unknown
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::NOT_FOUND);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssOtherError)
{
  // Tests IMPI Digest task when HSS returns an unhandled error type
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::UNKNOWN);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 500
  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssUnknownScheme)
{
  // Tests IMPI Digest task when HSS returns SUCCESS but with unknown auth scheme
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                            NULL,
                                            SCHEME_UNKNOWN);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiDigestHssAKAReturned)
{
  // Tests IMPI Digest task when HSS returns SUCCESS but with AKA auth scheme
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "digest",
                             "?public_id=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiDigestTask* task = new ImpiDigestTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                            NULL,
                                            SCHEME_AKA);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_DIGEST)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiAvEmptyQoP)
{
  // Tests IMPI AV task when no QoP is specified
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "av",
                             "?impu=" + IMPU);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);


  // Create a fake digest to be returned from the HSS
  DigestAuthVector* digest = new DigestAuthVector();
  digest->ha1 = "ha1";
  digest->realm = "realm";
  digest->qop = "";

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                            digest,
                                            SCHEME_DIGEST);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_UNKNOWN)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(build_av_json(*digest), req.content());
}

TEST_F(HTTPHandlersTest, ImpiAvNoPublicId)
{
  // Tests IMPI AV Task with no Public ID gives a 404
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "av",
                             "");

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 404 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiAKA)
{
  // Tests AKAv1 Impi AV Task
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "aka",
                             "?impu=" + IMPU + "&resync-auth=" + base64_encode(SIP_AUTHORIZATION) + "&server-name=" + PROVIDED_SERVER_NAME);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a fake digest to be returned from the HSS
  AKAAuthVector* aka = new AKAAuthVector();
  aka->challenge = "challenge";
  aka->response = "response";
  aka->crypt_key = "crypt_key";
  aka->integrity_key = "integrity_key";

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                            aka,
                                            SCHEME_AKA);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_AKA)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(build_aka_json(*aka), req.content());
}

TEST_F(HTTPHandlersTest, ImpiAKAv2)
{
  // Tests AKAv1 Impi AV Task
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "aka2",
                             "?impu=" + IMPU + "&resync-auth=" + base64_encode(SIP_AUTHORIZATION) + "&server-name=" + PROVIDED_SERVER_NAME);

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a fake digest to be returned from the HSS
  AKAAuthVector* aka = new AKAAuthVector();
  aka->challenge = "challenge";
  aka->response = "response";
  aka->crypt_key = "crypt_key";
  aka->integrity_key = "integrity_key";
  aka->version = 2;

  // Create an MAA* to return
  HssConnection::MultimediaAuthAnswer answer =
    HssConnection::MultimediaAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                        aka,
                                        SCHEME_AKAV2);

  // Expect that the MAR has the correct IMPI, IMPU and scheme
  EXPECT_CALL(*_hss, send_multimedia_auth_request(_,
    AllOf(Field(&HssConnection::MultimediaAuthRequest::impi, IMPI),
          Field(&HssConnection::MultimediaAuthRequest::impu, IMPU),
          Field(&HssConnection::MultimediaAuthRequest::scheme, SCHEME_AKAV2)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(build_aka_json(*aka), req.content());
}

TEST_F(HTTPHandlersTest, ImpiAuthInvalidScheme)
{
  // Tests Impi AV Task with invalid auth scheme
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "invalid",
                             "");

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpiAKANoImpu)
{
  // Tests Impi AV Task with no impu gets 404
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "aka",
                             "");

  ImpiTask::Config cfg(SCHEME_UNKNOWN, SCHEME_DIGEST, SCHEME_AKA, SCHEME_AKAV2);
  ImpiAvTask* task = new ImpiAvTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();
}

//
// ImpiRegistrationStatusTask tests
//

TEST_F(HTTPHandlersTest, ImpiRegStatusServerName)
{
  // Tests ImpiRegistrationStatusTask sends UAR to HSS and then sends correct
  // response
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
  ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a UAA to return
  HssConnection::UserAuthAnswer answer =
    HssConnection::UserAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                  DIAMETER_SUCCESS,
                                  SERVER_NAME,
                                  NO_CAPABILITIES);

  // Expect that the UAR has the correct IMPI, IMPU and visited network
  EXPECT_CALL(*_hss, send_user_auth_request(_,
    AllOf(Field(&HssConnection::UserAuthRequest::impi, IMPI),
          Field(&HssConnection::UserAuthRequest::impu, IMPU),
          Field(&HssConnection::UserAuthRequest::visited_network, DEST_REALM)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the UAA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES, ""), req.content());
}

TEST_F(HTTPHandlersTest, ImpiRegStatusCapabilities)
{
  // Tests ImpiRegistrationStatusTask when UAA has capabilities with no server name
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
  ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a UAA to return
  HssConnection::UserAuthAnswer answer =
    HssConnection::UserAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                  DIAMETER_SUCCESS,
                                  "",
                                  CAPABILITIES);

  // Expect that the UAR has the correct IMPI, IMPU and visited network
  EXPECT_CALL(*_hss, send_user_auth_request(_,
    AllOf(Field(&HssConnection::UserAuthRequest::impi, IMPI),
          Field(&HssConnection::UserAuthRequest::impu, IMPU),
          Field(&HssConnection::UserAuthRequest::visited_network, DEST_REALM)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the UAA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, "", CAPABILITIES, ""), req.content());
}

TEST_F(HTTPHandlersTest, ImpiRegStatusCapabilitiesWithServerName)
{
  // Tests ImpiRegistrationStatusTask when the UAA has capabilities with a server name
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
  ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a UAA to return
  HssConnection::UserAuthAnswer answer =
    HssConnection::UserAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                  DIAMETER_SUCCESS,
                                  "",
                                  CAPABILITIES_WITH_SERVER_NAME);

  // Expect that the UAR has the correct IMPI, IMPU and visited network
  EXPECT_CALL(*_hss, send_user_auth_request(_,
    AllOf(Field(&HssConnection::UserAuthRequest::impi, IMPI),
          Field(&HssConnection::UserAuthRequest::impu, IMPU),
          Field(&HssConnection::UserAuthRequest::visited_network, DEST_REALM)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the UAA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, "", CAPABILITIES_WITH_SERVER_NAME, ""), req.content());
}

TEST_F(HTTPHandlersTest, ImpiRegStatusPassesHealthCheck)
{
  // Tests that a 200 reponse triggers the health-checker
  MockHealthChecker* hc = new MockHealthChecker();
  HssCacheTask::configure_health_checker(hc);

  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "registration-status",
                             "?impu=" + IMPU);

  ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
  ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a UAA to return
  HssConnection::UserAuthAnswer answer =
    HssConnection::UserAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                  DIAMETER_SUCCESS,
                                  SERVER_NAME,
                                  NO_CAPABILITIES);

  // Expect that the UAR has the correct IMPI, IMPU and visited network
  EXPECT_CALL(*_hss, send_user_auth_request(_,
    AllOf(Field(&HssConnection::UserAuthRequest::impi, IMPI),
          Field(&HssConnection::UserAuthRequest::impu, IMPU),
          Field(&HssConnection::UserAuthRequest::visited_network, DEST_REALM)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200 and the health-checker is notified
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  EXPECT_CALL(*hc, health_check_passed()).Times(1);

  task->run();

  // Need to clear the health_checker
  HssCacheTask::configure_health_checker(NULL);
  delete hc;
}

TEST_F(HTTPHandlersTest, ImpiRegStatusOptParams)
{
  // Tests that optional parameters are passed to the HSS on the UAR
  MockHttpStack::Request req(_httpstack,
                             "/impi/" + IMPI,
                             "registration-status",
                             "?impu=" + IMPU + "&visited-network=" + VISITED_NETWORK + "&auth-type=" + AUTH_TYPE_DEREG + "&sos=true");

  ImpiRegistrationStatusTask::Config cfg(DEST_REALM);
  ImpiRegistrationStatusTask* task = new ImpiRegistrationStatusTask(req, &cfg, FAKE_TRAIL_ID);

  // Create a UAA to return
  HssConnection::UserAuthAnswer answer =
    HssConnection::UserAuthAnswer(HssConnection::ResultCode::SUCCESS,
                                  DIAMETER_SUCCESS,
                                  SERVER_NAME,
                                  NO_CAPABILITIES);

  // Expect that the UAR has the correct IMPI, IMPU and visited network
  EXPECT_CALL(*_hss, send_user_auth_request(_,
    AllOf(Field(&HssConnection::UserAuthRequest::impi, IMPI),
          Field(&HssConnection::UserAuthRequest::impu, IMPU),
          Field(&HssConnection::UserAuthRequest::visited_network, VISITED_NETWORK),
          Field(&HssConnection::UserAuthRequest::authorization_type, AUTH_TYPE_DEREG)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();
}

// The following tests verify UAA error codes map to correct HTTP error codes
TEST_F(HTTPHandlersTest, ImpiRegStatusUserUnknown)
{
  registration_status_error_template(HssConnection::ResultCode::NOT_FOUND, HTTP_NOT_FOUND);
}

TEST_F(HTTPHandlersTest, ImpiRegStatusForbidden)
{
  registration_status_error_template(HssConnection::ResultCode::FORBIDDEN, HTTP_FORBIDDEN);
}

TEST_F(HTTPHandlersTest, ImpiRegStatusTimeout)
{
  // For a timeout, we expect to record a penalty
  EXPECT_CALL(*_httpstack, record_penalty()).Times(1);

  registration_status_error_template(HssConnection::ResultCode::TIMEOUT, HTTP_GATEWAY_TIMEOUT);
}

TEST_F(HTTPHandlersTest, ImpiRegStatusServerUnavailable)
{
  registration_status_error_template(HssConnection::ResultCode::SERVER_UNAVAILABLE, HTTP_SERVER_UNAVAILABLE);
}

TEST_F(HTTPHandlersTest, ImpiRegStatusOtherError)
{
  registration_status_error_template(HssConnection::ResultCode::UNKNOWN, HTTP_SERVER_ERROR);
}

//
// Location Info tests
//

TEST_F(HTTPHandlersTest, LocationInfoMainline)
{
  // Tests mainline LocationInfo task
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "location",
                             "");

  ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
  ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an LIA to return
  HssConnection::LocationInfoAnswer answer =
    HssConnection::LocationInfoAnswer(HssConnection::ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      SERVER_NAME,
                                      NO_CAPABILITIES,
                                      "");

  // Check the contents of the LIR
  EXPECT_CALL(*_hss, send_location_info_request(_, Field(&HssConnection::LocationInfoRequest::impu, IMPU), _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the LIA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES, ""), req.content());
}

TEST_F(HTTPHandlersTest, LocationInfoServerCapabilitiesNoServerName)
{
  // Tests a LocationInfo task when server capabilities are returned without a server name
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "location",
                             "");

  ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
  ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an LIA to return
  HssConnection::LocationInfoAnswer answer =
    HssConnection::LocationInfoAnswer(HssConnection::ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      "",
                                      CAPABILITIES,
                                      "");

  // Check the contents of the LIR
  EXPECT_CALL(*_hss, send_location_info_request(_, Field(&HssConnection::LocationInfoRequest::impu, IMPU), _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the LIA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, "", CAPABILITIES, ""), req.content());
}

TEST_F(HTTPHandlersTest, LocationInfoServerCapabilitiesWithServerName)
{
  // Tests a LocationInfo task when server capabilities are returned with a server name
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU,
                             "location",
                             "");

  ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
  ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an LIA to return
  HssConnection::LocationInfoAnswer answer =
    HssConnection::LocationInfoAnswer(HssConnection::ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      "",
                                      CAPABILITIES_WITH_SERVER_NAME,
                                      "");

  // Check the contents of the LIR
  EXPECT_CALL(*_hss, send_location_info_request(_, Field(&HssConnection::LocationInfoRequest::impu, IMPU), _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the LIA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, "", CAPABILITIES_WITH_SERVER_NAME, ""), req.content());
}

TEST_F(HTTPHandlersTest, LocationInfoWithWildcard)
{
  // Tests LocationInfo with wildcarded public identity
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "");

  ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
  ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an LIA to return
  HssConnection::LocationInfoAnswer answer =
    HssConnection::LocationInfoAnswer(HssConnection::ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      SERVER_NAME,
                                      NO_CAPABILITIES,
                                      WILDCARD);

  // Check the contents of the LIR
  EXPECT_CALL(*_hss, send_location_info_request(_, Field(&HssConnection::LocationInfoRequest::impu, IMPU), _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the LIA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES, WILDCARD), req.content());
}

TEST_F(HTTPHandlersTest, LocationInfoOptParams)
{
  // Tests LocationInfo with optional parameters
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/",
                             "location",
                             "?originating=true&auth-type=" + AUTH_TYPE_CAPAB);

  ImpuLocationInfoTask::Config cfg = ImpuLocationInfoTask::Config();
  ImpuLocationInfoTask* task = new ImpuLocationInfoTask(req, &cfg, FAKE_TRAIL_ID);

  // Create an LIA to return
  HssConnection::LocationInfoAnswer answer =
    HssConnection::LocationInfoAnswer(HssConnection::ResultCode::SUCCESS,
                                      DIAMETER_SUCCESS,
                                      SERVER_NAME,
                                      NO_CAPABILITIES,
                                      WILDCARD);

  // Check the contents of the LIR
  EXPECT_CALL(*_hss, send_location_info_request(_,
    AllOf(Field(&HssConnection::LocationInfoRequest::impu, IMPU),
          Field(&HssConnection::LocationInfoRequest::originating, "true"),
          Field(&HssConnection::LocationInfoRequest::authorization_type, AUTH_TYPE_CAPAB)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 200
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Check the body matches the LIA
  EXPECT_EQ(build_icscf_json(DIAMETER_SUCCESS, SERVER_NAME, CAPABILITIES, WILDCARD), req.content());
}

TEST_F(HTTPHandlersTest, LocationInfoNotFound)
{
  location_info_error_template(HssConnection::ResultCode::NOT_FOUND, HTTP_NOT_FOUND);
}

TEST_F(HTTPHandlersTest, LocationInfoTimeout)
{
  // For a timeout, we expect to record a penalty
  EXPECT_CALL(*_httpstack, record_penalty()).Times(1);

  location_info_error_template(HssConnection::ResultCode::TIMEOUT, HTTP_GATEWAY_TIMEOUT);
}

TEST_F(HTTPHandlersTest, LocationInfoServerUnavailable)
{
  location_info_error_template(HssConnection::ResultCode::SERVER_UNAVAILABLE, HTTP_SERVER_UNAVAILABLE);
}

TEST_F(HTTPHandlersTest, LocationInfoUnknownError)
{
  location_info_error_template(HssConnection::ResultCode::UNKNOWN, HTTP_SERVER_ERROR);
}

//
// ImpuRegData tests
//

TEST_F(HTTPHandlersTest, ImpuReadRegDataMainline)
{
  // Test that GET request returns cached IRS
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "",
                             htp_method_GET);
  ImpuRegDataTask::Config cfg(true, 3600);
  ImpuReadRegDataTask* task = new ImpuReadRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->add_associated_impi(IMPI);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);

  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // HTTP response is sent straight back - no state is changed.
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuReadRegDataCacheGetNotFound)
{
  // Test that GET request not foudn in cache results in 404
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "",
                             htp_method_GET);
  ImpuRegDataTask::Config cfg(true, 3600);
  ImpuReadRegDataTask* task = new ImpuReadRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Set up the cache to hit an error
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::NOT_FOUND));

  // 404 error expected
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();

  // Expect no body in 404 response
  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuReadRegDataNonGet)
{
  // Test that a non-GET request is rejected
  MockHttpStack::Request req(_httpstack,
                             "/impu/" + IMPU + "/reg-data",
                             "",
                             "",
                             "",
                             htp_method_PUT);

  ImpuRegDataTask::Config cfg(true, 3600);
  ImpuReadRegDataTask* task = new ImpuReadRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Bad Method response
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInitialReg)
{
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200),
          Property(&ImplicitRegistrationSet::get_associated_impis, IMPI_IN_VECTOR)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInitialRegNoServerName)
{
  MockHttpStack::Request req = make_request("reg", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInitialRegCacheGetNotFound)
{
  // Tests that if we get a NOT_FOUND error from the cache on an initial
  // register, we still send the SAR to the HSS and continue processing
  MockHttpStack::Request req = make_request("reg", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Set up the cache to return NOT_FOUND
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::NOT_FOUND));

  // Create IRS to be returned from the cache whenthe above is not found
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  EXPECT_CALL(*_cache, create_implicit_registration_set())
    .WillOnce(Return(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));



  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInitialRegCacheGetError)
{
  MockHttpStack::Request req = make_request("reg", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Set up the cache to hit an error
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::ERROR));

  // 504 error expected
  EXPECT_CALL(*_httpstack, send_reply(_, 504, _));

  task->run();

  // Expect no body in 504 response
  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInitialRegCachePutError)
{
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We simulate a cache error when trying to cache it
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<2>(Store::Status::ERROR));

  // Expect 503 response
  EXPECT_CALL(*_httpstack, send_reply(_, 503, _));

  task->run();

  // Expect no body in error response
  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataReReg)
{
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::RE_REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataReRegNoCache)
{
  MockHttpStack::Request req = make_request("reg", true, true, false);
  req.add_header_to_incoming_req("Cache-control", "no-cache");

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);
  irs->set_ttl(7200);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::RE_REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataReRegCached)
{
  // Tests that a new enough record in the cache will not trigger a SAR
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->set_ttl(7200);
  irs->add_associated_impi(IMPI);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // No SAR is made, and not new data added to cache

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataReRegNewBinding)
{
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  // A new binding triggers a REGISTRATION request, not a RE_REGISTRATION
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataRegIncludesBarring)
{
  // Tests that the first unbarred public id is used when putting data into the
  // cache
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION_WITH_BARRING);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION_WITH_BARRING,
                                          "");

  // Check the contents of the SAR, and respond with our SAA
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect it to be put in the cache with an updated TTL and state REGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::REGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  // Build the expected response and check it matches the actual response
  EXPECT_EQ(REGDATA_RESULT_INCLUDES_BARRING, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallWildcardWithSAR)
{
  // Tests that an SAA with a wildcard triggers a new lookup in the cache
  // The initial request doesn't include a wildcard
  MockHttpStack::Request req = make_request("call", false, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::NEW_WILDCARD,
                                          NO_CHARGING_ADDRESSES,
                                          "",
                                          WILDCARD);

  // Check the contents of the SAR, and respond with our SAA
  // The SAR is of type UNREGISTERED_USER because we've got NOT_REGISTERED from
  // the cache.
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect another cache lookup for the new wildcard impu
  // Create new IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs2 = new FakeImplicitRegistrationSet(IMPU);
  irs2->set_reg_state(RegistrationState::REGISTERED);

  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs2));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallNewWildcard)
{
  // Tests that an SAA with a new wildcard triggers a new lookup in the cache
  MockHttpStack::Request req = make_request("call", false, false, true);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::NEW_WILDCARD,
                                          NO_CHARGING_ADDRESSES,
                                          "",
                                          NEW_WILDCARD);

  // Check the contents of the SAR, and respond with our SAA
  // The SAR is of type UNREGISTERED_USER because we've got NOT_REGISTERED from
  // the cache.
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER),
          Field(&HssConnection::ServerAssignmentRequest::wildcard_impu, WILDCARD)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect another cache lookup for the new wildcard impu
  // Create new IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs2 = new FakeImplicitRegistrationSet(IMPU);
  irs2->set_reg_state(RegistrationState::REGISTERED);

  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, NEW_WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs2));

  // Expect 200 response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallNewWildcardNotFound)
{
  // Tests that an SAA with a new wildcard triggers a new lookup in the cache,
  // and if that doesn't find anything we send another SAR with the new wildcard
  MockHttpStack::Request req = make_request("call", false, false, true);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::NEW_WILDCARD,
                                          NO_CHARGING_ADDRESSES,
                                          "",
                                          NEW_WILDCARD);

  // Check the contents of the SAR, and respond with our SAA
  // The SAR is of type UNREGISTERED_USER because we've got NOT_REGISTERED from
  // the cache.
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER),
          Field(&HssConnection::ServerAssignmentRequest::wildcard_impu, WILDCARD)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We now expect another cache lookup for the new wildcard impu, which will
  // return NOT_FOUND
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, NEW_WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::NOT_FOUND));

  // Create IRS to be returned from the cache when we fail to find the above
  FakeImplicitRegistrationSet* irs2 = new FakeImplicitRegistrationSet("");
  EXPECT_CALL(*_cache, create_implicit_registration_set())
    .WillOnce(Return(irs2));

  // Now, we check that we send a new SAR to the HSS with the new wildcard,
  // and then end the test by timing out that request
  HssConnection::ServerAssignmentAnswer answer2 =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::TIMEOUT);

  // A timeout also gives us a penalty
  EXPECT_CALL(*_httpstack, record_penalty()).Times(1);

  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER),
          Field(&HssConnection::ServerAssignmentRequest::wildcard_impu, NEW_WILDCARD)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer2)));

  // Don't care about the response
  EXPECT_CALL(*_httpstack, send_reply(_, _, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallWildcardLoop)
{
  // Tests that if the SAA reports a new wildcard but it hasn't been updated,
  // we send a 503
  MockHttpStack::Request req = make_request("call", false, false, true);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Create an SAA with which the mock hss will respond to our SAR
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::NEW_WILDCARD,
                                          NO_CHARGING_ADDRESSES,
                                          "",
                                          WILDCARD);

  // Check the contents of the SAR, and respond with our SAA
  // The SAR is of type UNREGISTERED_USER because we've got NOT_REGISTERED from
  // the cache.
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER),
          Field(&HssConnection::ServerAssignmentRequest::wildcard_impu, WILDCARD)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect the request to be rejected as the new wildcard is the same
  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));

  task->run();
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallMainline)
{
  // Tests that a "call" request for a registered sub doesn't trigger an SAR
  MockHttpStack::Request req = make_request("call", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallWildcard)
{
  // Tests a "call" request for a wildcard impu
  MockHttpStack::Request req = make_request("call", true, false, true);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, WILDCARD, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallUnregisteredService)
{
  // Tests "call" request handling for unregistered service
  MockHttpStack::Request req = make_request("call", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::UNREGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Set up the cache to return our IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_UNREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataCallNewUnregisteredService)
{
  // Tests "call" request handling for unregistered service for a subscriber
  // whose data is not already in the cache
  MockHttpStack::Request req = make_request("call", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Get NOT_FOUND from the cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::NOT_FOUND));

  // Create IRS to be returned from the cache when we fail to find the above
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  EXPECT_CALL(*_cache, create_implicit_registration_set())
    .WillOnce(Return(irs));


  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::UNREGISTERED_USER)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is cached with state UNREGISTERED
  EXPECT_CALL(*_cache, put_implicit_registration_set(_, _, _,
    AllOf(Property(&ImplicitRegistrationSet::get_reg_state, RegistrationState::UNREGISTERED),
          Property(&ImplicitRegistrationSet::get_ttl, 7200)),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_UNREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregUser)
{
  // Tests user-initiated de-registration
  MockHttpStack::Request req = make_request("dereg-user", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::USER_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregTimeout)
{
  // Tests timeout-initiated de-registration
  MockHttpStack::Request req = make_request("dereg-timeout", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::TIMEOUT_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregAdmin)
{
  // Tests administrative de-registration
  MockHttpStack::Request req = make_request("dereg-admin", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregNoIMPI)
{
  // Tests that if an IMPI is not explicitly provided on a deregistration we use
  // the one from the cached user-data
  MockHttpStack::Request req = make_request("dereg-admin", false, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregCacheError)
{
  // Tests that if the cache delete operation fails, we send an appropriate error
  MockHttpStack::Request req = make_request("dereg-admin", false, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<2>(Store::Status::ERROR));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 503, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregCacheNotFound)
{
  // Tests that a NOT_FOUND error on deletion results in a 200 OK
  MockHttpStack::Request req = make_request("dereg-admin", false, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup use in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Cache delete gives a NOT_FOUND error
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<2>(Store::Status::NOT_FOUND));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregUnregSub)
{
  // Tests that an unregistered user is deregistered wih the HSS
  MockHttpStack::Request req = make_request("dereg-admin", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::UNREGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Lookup irs in cache
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Data is deleted from cache
  // Check that the deletion request is using the correct service profile (as
  // that's how the cache knows what to delete)
  EXPECT_CALL(*_cache, delete_implicit_registration_set(_, _, _,
    Property(&ImplicitRegistrationSet::get_ims_sub_xml, IMPU_IMS_SUBSCRIPTION),
    FAKE_TRAIL_ID))
    .WillOnce(DoAll(InvokeArgument<1>(), InvokeArgument<0>()));

  // Check the response
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregAuthFailedRegistered)
{
  // Tests auth failure flow. This should only affect the HSS and not the cache,
  // and should not change the registered state (as it just means a subscriber
  // has failed to log in with a new binding)
  MockHttpStack::Request req = make_request("dereg-auth-failed", false, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send an auth failure SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::AUTHENTICATION_FAILURE)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // No further cache operations - just expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregAuthFailedNotRegistered)
{
  // Tests auth failure flow. This should only affect the HSS and not the cache,
  // and should not change the registered state (as it just means a subscriber
  // has failed to log in with a new binding)
  MockHttpStack::Request req = make_request("dereg-auth-failed", false, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send an auth failure SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::AUTHENTICATION_FAILURE)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // No further cache operations - just expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregAuthTimeout)
{
  // Tests auth timeout flow. This should only affect the HSS and not the cache,
  // and should not change the registered state (as it just means a subscriber
  // has failed to log in with a new binding)
  MockHttpStack::Request req = make_request("dereg-auth-timeout", false, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send an auth timeout SAR, which gets SUCCESS back
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::AUTHENTICATION_TIMEOUT)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // No further cache operations - just expect a 200 OK
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));

  task->run();

  EXPECT_EQ(REGDATA_RESULT_DEREG, req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataDeregInvalid)
{
  // Tests that an attempt to deregister a not registered sub gets a
  // 400 Bad Request
  MockHttpStack::Request req = make_request("dereg-user", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // No SAR, just a 400 Bad Request
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInvalidXML)
{
  // Tests that getting invalid xml from the HSS results in a 500 response
  MockHttpStack::Request req = make_request("reg", true, false, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Cache doesn't find anything, and so creates an empty IRS
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<1>(Store::Status::NOT_FOUND));

  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  EXPECT_CALL(*_cache, create_implicit_registration_set())
    .WillOnce(Return(irs));

  // Then send a SAR, which gets SUCCESS back but with invalid XML
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SUCCESS,
                                          NO_CHARGING_ADDRESSES,
                                          IMPU_IMS_SUBSCRIPTION_INVALID,
                                          "");
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, DEFAULT_SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // We don't cache this, and instead send a 500
  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInvalidPUT)
{
  // Tests that a PUT without a req-type is rejected
  MockHttpStack::Request req = MockHttpStack::Request(_httpstack,
                                                      "/impu/" + IMPU + "/reg-data",
                                                      "",
                                                      "",
                                                      "{}",
                                                      htp_method_PUT);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 400
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataInvalidMethod)
{
  // Tests that a non-GET or -PUT request is rejected
  MockHttpStack::Request req = MockHttpStack::Request(_httpstack,
                                                      "/impu/" + IMPU + "/reg-data",
                                                      "",
                                                      "",
                                                      "{}",
                                                      htp_method_POST);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Expect a 400
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataHssNotFound)
{
  // Tests that a NOT_FOUND error from the HSS triggers a 404 response
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send a SAR, which gets a NOT_FOUND error
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::NOT_FOUND);
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataHssUnavailable)
{
  // Tests that a SERVER_UNAVAILABLE error from the HSS triggers a 503 response
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send a SAR, which gets a SERVER_UNAVAILABLE error
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::SERVER_UNAVAILABLE);
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 503
  EXPECT_CALL(*_httpstack, send_reply(_, 503, _));

  task->run();

  EXPECT_EQ("", req.content());
}

TEST_F(HTTPHandlersTest, ImpuRegDataHssUnknownError)
{
  // Tests that a TIMEOUT error from the HSS triggers a 504 response
  MockHttpStack::Request req = make_request("reg", true, true, false);

  ImpuRegDataTask::Config cfg(true, 3600, 7200);
  ImpuRegDataTask* task = new ImpuRegDataTask(req, &cfg, FAKE_TRAIL_ID);

  // Create IRS to be returned from the cache
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  irs->set_ims_sub_xml(IMPU_IMS_SUBSCRIPTION);
  irs->set_reg_state(RegistrationState::NOT_REGISTERED);
  irs->set_charging_addresses(NO_CHARGING_ADDRESSES);
  irs->add_associated_impi(IMPI);

  // Expect a cache lookup will return IRS in state NOT_REGISTERED
  EXPECT_CALL(*_cache, get_implicit_registration_set_for_impu(_, _, IMPU, FAKE_TRAIL_ID))
    .WillOnce(InvokeArgument<0>(irs));

  // Then send a SAR, which gets a NOT_FOUND error
  HssConnection::ServerAssignmentAnswer answer =
    HssConnection::ServerAssignmentAnswer(HssConnection::ResultCode::UNKNOWN);
  EXPECT_CALL(*_hss, send_server_assignment_request(_,
    AllOf(Field(&HssConnection::ServerAssignmentRequest::impi, IMPI),
          Field(&HssConnection::ServerAssignmentRequest::impu, IMPU),
          Field(&HssConnection::ServerAssignmentRequest::server_name, SERVER_NAME),
          Field(&HssConnection::ServerAssignmentRequest::type, Cx::ServerAssignmentType::REGISTRATION)),
    _))
    .WillOnce(InvokeArgument<0>(ByRef(answer)));

  // Expect a 500
  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));

  task->run();

  EXPECT_EQ("", req.content());
}
