/**
 * @file cx_test.cpp UT for Homestead Cx module.
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

#include <stdexcept>
#include "test_utils.hpp"

#include <freeDiameter/freeDiameter-host.h>
#include <freeDiameter/libfdcore.h>

#include "diameterstack.h"
#include "mockdiameterstack.hpp"
#include "cx.h"
#include "handlers.h"

/// Fixture for CxTest.
class CxTest : public testing::Test
{
public:
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const std::string IMPI;
  static const std::string IMPU;
  static const std::string SERVER_NAME;
  static const std::string SERVER_NAME_IN_CAPAB;
  static const std::string SIP_AUTH_SCHEME_DIGEST;
  static const std::string SIP_AUTH_SCHEME_AKA;
  static const std::string SIP_AUTHORIZATION;
  static const std::string IMS_SUBSCRIPTION;
  static const std::string VISITED_NETWORK_IDENTIFIER;
  static const std::string AUTHORIZATION_TYPE_REG;
  static const std::string AUTHORIZATION_TYPE_DEREG;
  static const std::string AUTHORIZATION_TYPE_CAPAB;
  static const std::string ORIGINATING_TRUE;
  static const std::string ORIGINATING_FALSE;
  static const std::string EMPTY_STRING;
  static std::vector<std::string> EMPTY_STRING_VECTOR;
  static const int32_t RESULT_CODE_SUCCESS;
  static const int32_t EXPERIMENTAL_RESULT_CODE_SUCCESS;
  static const int32_t AUTH_SESSION_STATE;
  static const std::vector<std::string> IMPIS;
  static const Cx::ServerAssignmentType TIMEOUT_DEREGISTRATION;
  static const Cx::ServerAssignmentType UNREGISTERED_USER;
  static std::vector<std::string> IMPUS;
  static std::vector<std::string> ASSOCIATED_IDENTITIES;
  static const ServerCapabilities CAPABILITIES;
  static const ServerCapabilities NO_CAPABILITIES;
  static const ServerCapabilities CAPABILITIES_WITH_SERVER_NAME;
  static const std::deque<std::string> NO_CFS;
  static const std::deque<std::string> CCFS;
  static const std::deque<std::string> ECFS;
  static const ChargingAddresses NO_CHARGING_ADDRESSES;
  static const ChargingAddresses FULL_CHARGING_ADDRESSES;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;

  std::string test_str;
  int32_t test_i32;

  static void SetUpTestCase()
  {
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf", NULL);
    _mock_stack = new MockDiameterStack();
    _cx_dict = new Cx::Dictionary();
  }

  static void TearDownTestCase()
  {
    delete _cx_dict; _cx_dict = NULL;
    delete _mock_stack; _mock_stack = NULL;
    _real_stack->stop();
    _real_stack->wait_stopped();
    _real_stack = NULL;
  }

  CxTest() {}
  ~CxTest() {}

  static void launder_message(Diameter::Message& msg)
  {
    struct msg* msg_to_build = msg.fd_msg();
    uint8_t* buffer;
    size_t len;
    int rc = fd_msg_bufferize(msg_to_build, &buffer, &len);
    if (rc != 0)
    {
      std::stringstream ss;
      ss << "fd_msg_bufferize failed: " << rc;
      throw new std::runtime_error(ss.str());
    }

    struct msg* parsed_msg = NULL;
    rc = fd_msg_parse_buffer(&buffer, len, &parsed_msg);
    if (rc != 0)
    {
      std::stringstream ss;
      ss << "fd_msg_parse_buffer failed: " << rc;
      throw new std::runtime_error(ss.str());
    }

    struct fd_pei error_info;
    rc = fd_msg_parse_dict(parsed_msg, fd_g_config->cnf_dict, &error_info);
    if (rc != 0)
    {
      std::stringstream ss;
      ss << "fd_msg_parse_dict failed: " << rc << " - " << error_info.pei_errcode;
      throw new std::runtime_error(ss.str());
    }

    fd_msg_free(msg_to_build);
    msg._fd_msg = parsed_msg;
    msg._free_on_delete = true;
    msg._master_msg = &msg;

    return;
  }

  void check_common_request_fields(const Diameter::Message& msg)
  {
    EXPECT_TRUE(msg.get_i32_from_avp(_cx_dict->SESSION_ID, test_i32));
    EXPECT_NE(0, test_i32);
    EXPECT_EQ(10415, msg.vendor_id());
    Diameter::AVP::iterator vendor_spec_app_ids = msg.begin(_cx_dict->VENDOR_SPECIFIC_APPLICATION_ID);
    EXPECT_TRUE(vendor_spec_app_ids != msg.end());
    vendor_spec_app_ids->get_i32_from_avp(_cx_dict->VENDOR_ID, test_i32);
    EXPECT_EQ(10415, test_i32);
    vendor_spec_app_ids->get_i32_from_avp(_cx_dict->AUTH_APPLICATION_ID, test_i32);
    EXPECT_EQ(16777216, test_i32);
    EXPECT_TRUE(msg.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
    EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
    EXPECT_TRUE(msg.get_str_from_avp(_cx_dict->ORIGIN_HOST, test_str));
    EXPECT_EQ("origin-host", test_str);
    EXPECT_TRUE(msg.get_str_from_avp(_cx_dict->ORIGIN_REALM, test_str));
    EXPECT_EQ("origin-realm", test_str);
    EXPECT_TRUE(msg.get_str_from_avp(_cx_dict->DESTINATION_REALM, test_str));
    EXPECT_EQ(DEST_REALM, test_str);
    EXPECT_TRUE(msg.get_str_from_avp(_cx_dict->DESTINATION_HOST, test_str));
    EXPECT_EQ(DEST_HOST, test_str);
    return;
  }
};

const std::string CxTest::DEST_REALM = "dest-realm";
const std::string CxTest::DEST_HOST = "dest-host";
const std::string CxTest::IMPI = "impi@example.com";
const std::string CxTest::IMPU = "sip:impu@example.com";
const std::string CxTest::SERVER_NAME = "sip:example.com";
const std::string CxTest::SERVER_NAME_IN_CAPAB = "sip:example2.com";
const std::string CxTest::SIP_AUTH_SCHEME_DIGEST = "SIP Digest";
const std::string CxTest::SIP_AUTH_SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string CxTest::SIP_AUTHORIZATION = "authorization";
const std::string CxTest::IMS_SUBSCRIPTION = "<some interesting xml>";
const std::string CxTest::VISITED_NETWORK_IDENTIFIER = "visited-network";
const std::string CxTest::AUTHORIZATION_TYPE_REG = "REG";
const std::string CxTest::AUTHORIZATION_TYPE_DEREG = "DEREG";
const std::string CxTest::AUTHORIZATION_TYPE_CAPAB = "CAPAB";
const std::string CxTest::ORIGINATING_TRUE = "true";
const std::string CxTest::ORIGINATING_FALSE = "false";
const std::string CxTest::EMPTY_STRING = "";
std::vector<std::string> CxTest::EMPTY_STRING_VECTOR = {};
const int32_t CxTest::RESULT_CODE_SUCCESS = 2001;
const int32_t CxTest::EXPERIMENTAL_RESULT_CODE_SUCCESS = 5001;
const int32_t CxTest::AUTH_SESSION_STATE = 1;
const std::vector<std::string> CxTest::IMPIS {"private_id1", "private_id2"};
const Cx::ServerAssignmentType CxTest::TIMEOUT_DEREGISTRATION = Cx::TIMEOUT_DEREGISTRATION;
const Cx::ServerAssignmentType CxTest::UNREGISTERED_USER = Cx::UNREGISTERED_USER;
std::vector<std::string> CxTest::IMPUS {"public_id1", "public_id2"};
std::vector<std::string> CxTest::ASSOCIATED_IDENTITIES {"associated_id1", "associated_id2", "associated_id3"};
const std::vector<int32_t> mandatory_capabilities = {1, 3};
const std::vector<int32_t> optional_capabilities = {2, 4};
const std::vector<int32_t> no_capabilities = {};
const ServerCapabilities CxTest::CAPABILITIES(mandatory_capabilities, optional_capabilities, EMPTY_STRING);
const ServerCapabilities CxTest::NO_CAPABILITIES(no_capabilities, no_capabilities, EMPTY_STRING);
const ServerCapabilities CxTest::CAPABILITIES_WITH_SERVER_NAME(no_capabilities, no_capabilities, SERVER_NAME_IN_CAPAB);
const std::deque<std::string> CxTest::NO_CFS = {};
const std::deque<std::string> CxTest::ECFS = {"ecf1", "ecf"};
const std::deque<std::string> CxTest::CCFS = {"ccf1", "ccf2"};
const ChargingAddresses CxTest::NO_CHARGING_ADDRESSES(NO_CFS, NO_CFS);
const ChargingAddresses CxTest::FULL_CHARGING_ADDRESSES(CCFS, ECFS);

Diameter::Stack* CxTest::_real_stack = NULL;
MockDiameterStack* CxTest::_mock_stack = NULL;
Cx::Dictionary* CxTest::_cx_dict = NULL;

//
// Multimedia Authorization Requests
//

TEST_F(CxTest, MARTest)
{
  Cx::MultimediaAuthRequest mar(_cx_dict,
                                _mock_stack,
                                DEST_REALM,
                                DEST_HOST,
                                IMPI,
                                IMPU,
                                SERVER_NAME,
                                SIP_AUTH_SCHEME_DIGEST);
  launder_message(mar);
  check_common_request_fields(mar);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SIP_AUTH_SCHEME_DIGEST, mar.sip_auth_scheme());
  EXPECT_EQ(EMPTY_STRING, mar.sip_authorization());
  EXPECT_TRUE(mar.sip_number_auth_items(test_i32));
  EXPECT_EQ(1, test_i32);
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
}

TEST_F(CxTest, MARAuthorizationTest)
{
  Cx::MultimediaAuthRequest mar(_cx_dict,
                                _mock_stack,
                                DEST_REALM,
                                DEST_HOST,
                                IMPI,
                                IMPU,
                                SERVER_NAME,
                                SIP_AUTH_SCHEME_AKA,
                                SIP_AUTHORIZATION);
  launder_message(mar);
  check_common_request_fields(mar);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  EXPECT_EQ(SIP_AUTH_SCHEME_AKA, mar.sip_auth_scheme());
  EXPECT_EQ(SIP_AUTHORIZATION, mar.sip_authorization());
  EXPECT_TRUE(mar.sip_number_auth_items(test_i32));
  EXPECT_EQ(1, test_i32);
  EXPECT_TRUE(mar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
}

//
// Multimedia Authorization Answers
//

TEST_F(CxTest, MAATest)
{
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  AKAAuthVector aka;
  aka.challenge = "sure."; // Chosen to ensure that it will encode to
                           // Base64 that needs to be padded with an
                           // equals sign.
  aka.response = "response";
  aka.crypt_key = "crypt_key";
  aka.integrity_key = "integrity_key";

  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               RESULT_CODE_SUCCESS,
                               SIP_AUTH_SCHEME_AKA,
                               digest,
                               aka);
  launder_message(maa);
  EXPECT_TRUE(maa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_EQ(SIP_AUTH_SCHEME_AKA, maa.sip_auth_scheme());

  DigestAuthVector maa_digest = maa.digest_auth_vector();
  EXPECT_EQ(digest.ha1, maa_digest.ha1);
  EXPECT_EQ(digest.realm, maa_digest.realm);
  EXPECT_EQ(digest.qop, maa_digest.qop);

  // The AKA values should be decoded from base64/hex.
  AKAAuthVector maa_aka = maa.aka_auth_vector();
  EXPECT_EQ("c3VyZS4=", maa_aka.challenge);
  EXPECT_EQ("726573706f6e7365", maa_aka.response);
  EXPECT_EQ("63727970745f6b6579", maa_aka.crypt_key);
  EXPECT_EQ("696e746567726974795f6b6579", maa_aka.integrity_key);
}

TEST_F(CxTest, MAATestNoAuthScheme)
{
  DigestAuthVector digest;
  digest.ha1 = "ha1";
  digest.realm = "realm";
  digest.qop = "qop";

  AKAAuthVector aka;
  aka.challenge = "sure."; // Chosen to ensure that it will encode to
                           // Base64 that needs to be padded with an
                           // equals sign.
  aka.response = "response";
  aka.crypt_key = "crypt_key";
  aka.integrity_key = "integrity_key";

  Cx::MultimediaAuthAnswer maa(_cx_dict,
                               _mock_stack,
                               RESULT_CODE_SUCCESS,
                               EMPTY_STRING,
                               digest,
                               aka);
  launder_message(maa);
  EXPECT_TRUE(maa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_EQ(EMPTY_STRING, maa.sip_auth_scheme());

  DigestAuthVector maa_digest = maa.digest_auth_vector();
  EXPECT_EQ(digest.ha1, maa_digest.ha1);
  EXPECT_EQ(digest.realm, maa_digest.realm);
  EXPECT_EQ(digest.qop, maa_digest.qop);

  // The AKA values should be decoded from base64/hex.
  AKAAuthVector maa_aka = maa.aka_auth_vector();
  EXPECT_EQ("c3VyZS4=", maa_aka.challenge);
  EXPECT_EQ("726573706f6e7365", maa_aka.response);
  EXPECT_EQ("63727970745f6b6579", maa_aka.crypt_key);
  EXPECT_EQ("696e746567726974795f6b6579", maa_aka.integrity_key);
}

//
// Server Assignment Requests
//

TEST_F(CxTest, SARTest)
{
  Cx::ServerAssignmentRequest sar(_cx_dict,
                                  _mock_stack,
                                  DEST_HOST,
                                  DEST_REALM,
                                  IMPI,
                                  IMPU,
                                  SERVER_NAME,
                                  TIMEOUT_DEREGISTRATION);
  launder_message(sar);
  check_common_request_fields(sar);
  EXPECT_EQ(IMPI, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(TIMEOUT_DEREGISTRATION, test_i32);
  EXPECT_TRUE(sar.user_data_already_available(test_i32));
  EXPECT_EQ(0, test_i32);
}

TEST_F(CxTest, SARNoImpiTest)
{
  Cx::ServerAssignmentRequest sar(_cx_dict,
                                  _mock_stack,
                                  DEST_HOST,
                                  DEST_REALM,
                                  EMPTY_STRING,
                                  IMPU,
                                  SERVER_NAME,
                                  UNREGISTERED_USER);
  launder_message(sar);
  check_common_request_fields(sar);
  EXPECT_EQ(EMPTY_STRING, sar.impi());
  EXPECT_EQ(IMPU, sar.impu());
  EXPECT_TRUE(sar.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  EXPECT_TRUE(sar.server_assignment_type(test_i32));
  EXPECT_EQ(UNREGISTERED_USER, test_i32);
  EXPECT_TRUE(sar.user_data_already_available(test_i32));
  EXPECT_EQ(0, test_i32);
}

//
// Server Assignment Answers
//

TEST_F(CxTest, SAATest)
{
  ChargingAddresses charging_addrs;
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 RESULT_CODE_SUCCESS,
                                 IMS_SUBSCRIPTION,
                                 FULL_CHARGING_ADDRESSES);
  launder_message(saa);
  EXPECT_TRUE(saa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_TRUE(saa.user_data(test_str));
  EXPECT_EQ(IMS_SUBSCRIPTION, test_str);
  saa.charging_addrs(charging_addrs);
  EXPECT_EQ(CCFS, charging_addrs.ccfs);
  EXPECT_EQ(ECFS, charging_addrs.ecfs);
}

TEST_F(CxTest, SAATestNoChargingAddresses)
{
  ChargingAddresses charging_addrs;
  Cx::ServerAssignmentAnswer saa(_cx_dict,
                                 _mock_stack,
                                 RESULT_CODE_SUCCESS,
                                 IMS_SUBSCRIPTION,
                                 NO_CHARGING_ADDRESSES);
  launder_message(saa);
  EXPECT_TRUE(saa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_TRUE(saa.user_data(test_str));
  EXPECT_EQ(IMS_SUBSCRIPTION, test_str);
  saa.charging_addrs(charging_addrs);
  EXPECT_TRUE(charging_addrs.empty());
}

//
// User Authorization Requests
//

TEST_F(CxTest, UARTest)
{
  Cx::UserAuthorizationRequest uar(_cx_dict,
                                   _mock_stack,
                                   DEST_HOST,
                                   DEST_REALM,
                                   IMPI,
                                   IMPU,
                                   VISITED_NETWORK_IDENTIFIER,
                                   AUTHORIZATION_TYPE_REG);
  launder_message(uar);
  check_common_request_fields(uar);
  EXPECT_EQ(IMPI, uar.impi());
  EXPECT_EQ(IMPU, uar.impu());
  EXPECT_TRUE(uar.visited_network(test_str));
  EXPECT_EQ(VISITED_NETWORK_IDENTIFIER, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(0, test_i32);
}

TEST_F(CxTest, UARAuthTypeDeregTest)
{
  Cx::UserAuthorizationRequest uar(_cx_dict,
                                   _mock_stack,
                                   DEST_HOST,
                                   DEST_REALM,
                                   IMPI,
                                   IMPU,
                                   VISITED_NETWORK_IDENTIFIER,
                                   AUTHORIZATION_TYPE_DEREG);
  launder_message(uar);
  check_common_request_fields(uar);
  EXPECT_EQ(IMPI, uar.impi());
  EXPECT_EQ(IMPU, uar.impu());
  EXPECT_TRUE(uar.visited_network(test_str));
  EXPECT_EQ(VISITED_NETWORK_IDENTIFIER, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(1, test_i32);
}

TEST_F(CxTest, UARAuthTypeCapabTest)
{
  Cx::UserAuthorizationRequest uar(_cx_dict,
                                   _mock_stack,
                                   DEST_HOST,
                                   DEST_REALM,
                                   IMPI,
                                   IMPU,
                                   VISITED_NETWORK_IDENTIFIER,
                                   AUTHORIZATION_TYPE_CAPAB);
  launder_message(uar);
  check_common_request_fields(uar);
  EXPECT_EQ(IMPI, uar.impi());
  EXPECT_EQ(IMPU, uar.impu());
  EXPECT_TRUE(uar.visited_network(test_str));
  EXPECT_EQ(VISITED_NETWORK_IDENTIFIER, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(2, test_i32);
}

TEST_F(CxTest, UARNoAuthTypeTest)
{
  Cx::UserAuthorizationRequest uar(_cx_dict,
                                   _mock_stack,
                                   DEST_HOST,
                                   DEST_REALM,
                                   IMPI,
                                   IMPU,
                                   VISITED_NETWORK_IDENTIFIER,
                                   EMPTY_STRING);
  launder_message(uar);
  check_common_request_fields(uar);
  EXPECT_EQ(IMPI, uar.impi());
  EXPECT_EQ(IMPU, uar.impu());
  EXPECT_TRUE(uar.visited_network(test_str));
  EXPECT_EQ(VISITED_NETWORK_IDENTIFIER, test_str);
  EXPECT_TRUE(uar.auth_type(test_i32));
  EXPECT_EQ(0, test_i32);
}

//
// User Authorization Answers
//

TEST_F(CxTest, UAATest)
{
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  RESULT_CODE_SUCCESS,
                                  EXPERIMENTAL_RESULT_CODE_SUCCESS,
                                  SERVER_NAME,
                                  CAPABILITIES);
  launder_message(uaa);
  EXPECT_TRUE(uaa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(uaa.experimental_result_code());
  EXPECT_TRUE(uaa.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = uaa.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, UAATestExperimentalResultCode)
{
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  0,
                                  EXPERIMENTAL_RESULT_CODE_SUCCESS,
                                  SERVER_NAME,
                                  CAPABILITIES);
  launder_message(uaa);
  EXPECT_FALSE(uaa.result_code(test_i32));
  EXPECT_EQ(EXPERIMENTAL_RESULT_CODE_SUCCESS, uaa.experimental_result_code());
  EXPECT_TRUE(uaa.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = uaa.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, UAATestNoServerName)
{
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  RESULT_CODE_SUCCESS,
                                  0,
                                  EMPTY_STRING,
                                  CAPABILITIES);
  launder_message(uaa);
  EXPECT_TRUE(uaa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(uaa.experimental_result_code());
  EXPECT_FALSE(uaa.server_name(test_str));
  ServerCapabilities capabilities = uaa.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, UAATestNoCapabilities)
{
  Cx::UserAuthorizationAnswer uaa(_cx_dict,
                                  _mock_stack,
                                  RESULT_CODE_SUCCESS,
                                  0,
                                  SERVER_NAME,
                                  NO_CAPABILITIES);
  launder_message(uaa);
  EXPECT_TRUE(uaa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(uaa.experimental_result_code());
  EXPECT_TRUE(uaa.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = uaa.server_capabilities();
  EXPECT_EQ(NO_CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(NO_CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, UAATestCapabilitiesWithServerName)
{
  Cx::LocationInfoAnswer uaa(_cx_dict,
                             _mock_stack,
                             RESULT_CODE_SUCCESS,
                             0,
                             EMPTY_STRING,
                             CAPABILITIES_WITH_SERVER_NAME);
  launder_message(uaa);
  EXPECT_TRUE(uaa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(uaa.experimental_result_code());
  EXPECT_FALSE(uaa.server_name(test_str));
  ServerCapabilities capabilities = uaa.server_capabilities();
  EXPECT_EQ(SERVER_NAME_IN_CAPAB, capabilities.server_name);
  EXPECT_EQ(CAPABILITIES_WITH_SERVER_NAME.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES_WITH_SERVER_NAME.optional_capabilities,
            capabilities.optional_capabilities);
}

//
// Location Info Requests
//

TEST_F(CxTest, LIRTest)
{
  Cx::LocationInfoRequest lir(_cx_dict,
                              _mock_stack,
                              DEST_HOST,
                              DEST_REALM,
                              ORIGINATING_TRUE,
                              IMPU,
                              AUTHORIZATION_TYPE_CAPAB);
  launder_message(lir);
  check_common_request_fields(lir);
  EXPECT_TRUE(lir.originating(test_i32));
  EXPECT_EQ(0, test_i32);
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_TRUE(lir.auth_type(test_i32));
  EXPECT_EQ(2, test_i32);
}

TEST_F(CxTest, LIRWrongOptionalParamsTest)
{
  Cx::LocationInfoRequest lir(_cx_dict,
                              _mock_stack,
                              DEST_HOST,
                              DEST_REALM,
                              ORIGINATING_FALSE,
                              IMPU,
                              AUTHORIZATION_TYPE_REG);
  launder_message(lir);
  check_common_request_fields(lir);
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.auth_type(test_i32));
}

TEST_F(CxTest, LIRNoOptionalParamsTest)
{
  Cx::LocationInfoRequest lir(_cx_dict,
                              _mock_stack,
                              DEST_HOST,
                              DEST_REALM,
                              EMPTY_STRING,
                              IMPU,
                              EMPTY_STRING);
  launder_message(lir);
  check_common_request_fields(lir);
  EXPECT_FALSE(lir.originating(test_i32));
  EXPECT_EQ(IMPU, lir.impu());
  EXPECT_FALSE(lir.auth_type(test_i32));
}

//
// Location Info Answers
//

TEST_F(CxTest, LIATest)
{
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             RESULT_CODE_SUCCESS,
                             EXPERIMENTAL_RESULT_CODE_SUCCESS,
                             SERVER_NAME,
                             CAPABILITIES);
  launder_message(lia);
  EXPECT_TRUE(lia.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(lia.experimental_result_code());
  EXPECT_TRUE(lia.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = lia.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, LIATestExperimentalResultCode)
{
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             0,
                             EXPERIMENTAL_RESULT_CODE_SUCCESS,
                             SERVER_NAME,
                             CAPABILITIES);
  launder_message(lia);
  EXPECT_FALSE(lia.result_code(test_i32));
  EXPECT_EQ(EXPERIMENTAL_RESULT_CODE_SUCCESS, lia.experimental_result_code());
  EXPECT_TRUE(lia.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = lia.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, LIATestNoServerName)
{
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             RESULT_CODE_SUCCESS,
                             0,
                             EMPTY_STRING,
                             CAPABILITIES);
  launder_message(lia);
  EXPECT_TRUE(lia.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(lia.experimental_result_code());
  EXPECT_FALSE(lia.server_name(test_str));
  ServerCapabilities capabilities = lia.server_capabilities();
  EXPECT_EQ(CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, LIATestNoCapabilities)
{
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             RESULT_CODE_SUCCESS,
                             0,
                             SERVER_NAME,
                             NO_CAPABILITIES);
  launder_message(lia);
  EXPECT_TRUE(lia.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(lia.experimental_result_code());
  EXPECT_TRUE(lia.server_name(test_str));
  EXPECT_EQ(SERVER_NAME, test_str);
  ServerCapabilities capabilities = lia.server_capabilities();
  EXPECT_EQ(NO_CAPABILITIES.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(NO_CAPABILITIES.optional_capabilities,
            capabilities.optional_capabilities);
}

TEST_F(CxTest, LIATestCapabilitiesWithServerName)
{
  Cx::LocationInfoAnswer lia(_cx_dict,
                             _mock_stack,
                             RESULT_CODE_SUCCESS,
                             0,
                             EMPTY_STRING,
                             CAPABILITIES_WITH_SERVER_NAME);
  launder_message(lia);
  EXPECT_TRUE(lia.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_FALSE(lia.experimental_result_code());
  EXPECT_FALSE(lia.server_name(test_str));
  ServerCapabilities capabilities = lia.server_capabilities();
  EXPECT_EQ(SERVER_NAME_IN_CAPAB, capabilities.server_name);
  EXPECT_EQ(CAPABILITIES_WITH_SERVER_NAME.mandatory_capabilities,
            capabilities.mandatory_capabilities);
  EXPECT_EQ(CAPABILITIES_WITH_SERVER_NAME.optional_capabilities,
            capabilities.optional_capabilities);
}

//
// Registration Termination Requests and Answers
//

TEST_F(CxTest, RTTest)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         PERMANENT_TERMINATION,
                                         IMPI,
                                         ASSOCIATED_IDENTITIES,
                                         IMPUS,
                                         AUTH_SESSION_STATE);
  launder_message(rtr);
  EXPECT_TRUE(rtr.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
  EXPECT_EQ(PERMANENT_TERMINATION, rtr.deregistration_reason());
  EXPECT_EQ(IMPI, rtr.impi());
  EXPECT_EQ(ASSOCIATED_IDENTITIES, rtr.associated_identities());
  EXPECT_EQ(IMPUS, rtr.impus());

  Cx::RegistrationTerminationAnswer rta(rtr,
                                        _cx_dict,
                                        DIAMETER_REQ_SUCCESS,
                                        AUTH_SESSION_STATE,
                                        ASSOCIATED_IDENTITIES);
  launder_message(rta);
  EXPECT_EQ(10415, rta.vendor_id());
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_TRUE(rta.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
  EXPECT_EQ(ASSOCIATED_IDENTITIES, rta.associated_identities());
}

TEST_F(CxTest, RTTestNoIMPUsNoAssocatedIdentities)
{
  Cx::RegistrationTerminationRequest rtr(_cx_dict,
                                         _mock_stack,
                                         SERVER_CHANGE,
                                         IMPI,
                                         EMPTY_STRING_VECTOR,
                                         EMPTY_STRING_VECTOR,
                                         AUTH_SESSION_STATE);
  launder_message(rtr);
  EXPECT_TRUE(rtr.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
  EXPECT_EQ(SERVER_CHANGE, rtr.deregistration_reason());
  EXPECT_EQ(IMPI, rtr.impi());
  EXPECT_EQ(EMPTY_STRING_VECTOR, rtr.associated_identities());
  EXPECT_EQ(EMPTY_STRING_VECTOR, rtr.impus());

  Cx::RegistrationTerminationAnswer rta(rtr,
                                        _cx_dict,
                                        DIAMETER_REQ_SUCCESS,
                                        AUTH_SESSION_STATE,
                                        EMPTY_STRING_VECTOR);
  launder_message(rta);
  EXPECT_EQ(10415, rta.vendor_id());
  EXPECT_TRUE(rta.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_TRUE(rta.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
  EXPECT_EQ(EMPTY_STRING_VECTOR, rta.associated_identities());
}

//
// Push Profile Requests and Answers
//

TEST_F(CxTest, PPTest)
{
  ChargingAddresses charging_addrs;
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             IMS_SUBSCRIPTION,
                             FULL_CHARGING_ADDRESSES,
                             AUTH_SESSION_STATE);
  launder_message(ppr);
  EXPECT_EQ(IMPI, ppr.impi());
  EXPECT_TRUE(ppr.user_data(test_str));
  EXPECT_EQ(IMS_SUBSCRIPTION, test_str);
  EXPECT_TRUE(ppr.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
  EXPECT_TRUE(ppr.charging_addrs(charging_addrs));
  EXPECT_EQ(CCFS, charging_addrs.ccfs);
  EXPECT_EQ(ECFS, charging_addrs.ecfs);

  Cx::PushProfileAnswer ppa(ppr,
                            _cx_dict,
                            DIAMETER_REQ_SUCCESS,
                            AUTH_SESSION_STATE);
  launder_message(ppa);
  EXPECT_EQ(10415, ppa.vendor_id());
  EXPECT_TRUE(ppa.result_code(test_i32));
  EXPECT_EQ(RESULT_CODE_SUCCESS, test_i32);
  EXPECT_TRUE(ppa.get_i32_from_avp(_cx_dict->AUTH_SESSION_STATE, test_i32));
  EXPECT_EQ(AUTH_SESSION_STATE, test_i32);
}

TEST_F(CxTest, PPTestNoChargingAddresses)
{
  ChargingAddresses charging_addrs;
  Cx::PushProfileRequest ppr(_cx_dict,
                             _mock_stack,
                             IMPI,
                             IMS_SUBSCRIPTION,
                             NO_CHARGING_ADDRESSES,
                             AUTH_SESSION_STATE);
  launder_message(ppr);
  EXPECT_FALSE(ppr.charging_addrs(charging_addrs));

  Cx::PushProfileAnswer ppa(ppr,
                            _cx_dict,
                            DIAMETER_REQ_SUCCESS,
                            AUTH_SESSION_STATE);
  launder_message(ppa);
}
