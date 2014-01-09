/**
 * @file cx_test.cpp UT for Sprout Cx module.
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

/// Fixture for CxTest.
class CxTest : public testing::Test
{
public:
  static const std::string DEST_REALM;
  static const std::string DEST_HOST;
  static const std::string IMPI;
  static const std::string IMPU;
  static const std::string SERVER_NAME;
  static const std::string SIP_AUTH_SCHEME_DIGEST;
  static const std::string SIP_AUTH_SCHEME_AKA;
  static const std::string SIP_AUTHORIZATION;

  static Diameter::Stack* _real_stack;
  static MockDiameterStack* _mock_stack;
  static Cx::Dictionary* _cx_dict;

  static void SetUpTestCase()
  {
    _real_stack = Diameter::Stack::get_instance();
    _real_stack->initialize();
    _real_stack->configure(UT_DIR + "/diameterstack.conf");
    _real_stack->start();
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

  static Diameter::Message launder_message(const Diameter::Message& msg)
  {
    struct msg* msg_to_build = msg.msg();
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

    return Diameter::Message(_cx_dict, parsed_msg);
  }
};

const std::string CxTest::DEST_REALM = "dest-realm";
const std::string CxTest::DEST_HOST = "dest-host";
const std::string CxTest::IMPI = "impi@example.com";
const std::string CxTest::IMPU = "sip:impu@example.com";
const std::string CxTest::SERVER_NAME = "sip:example.com";
const std::string CxTest::SIP_AUTH_SCHEME_DIGEST = "SIP Digest";
const std::string CxTest::SIP_AUTH_SCHEME_AKA = "Digest-AKAv1-MD5";
const std::string CxTest::SIP_AUTHORIZATION = "authorization";

Diameter::Stack* CxTest::_real_stack = NULL;
MockDiameterStack* CxTest::_mock_stack = NULL;
Cx::Dictionary* CxTest::_cx_dict = NULL;

TEST_F(CxTest, MARTest)
{
  Cx::MultimediaAuthRequest mar(_cx_dict,
                                DEST_REALM,
                                DEST_HOST,
                                IMPI,
                                IMPU,
                                SERVER_NAME,
                                SIP_AUTH_SCHEME_DIGEST);
  Diameter::Message msg = launder_message(mar);
  Cx::MultimediaAuthRequest mar2 = Cx::MultimediaAuthRequest(msg);
  EXPECT_EQ(IMPI, mar2.impi());
  EXPECT_EQ(IMPU, mar2.impu());
  // TODO: Check rest of parameters.
}

TEST_F(CxTest, MARAuthorizationTest)
{
  Cx::MultimediaAuthRequest mar(_cx_dict,
                                DEST_REALM,
                                DEST_HOST,
                                IMPI,
                                IMPU,
                                SERVER_NAME,
                                SIP_AUTH_SCHEME_AKA,
                                SIP_AUTHORIZATION);
  Diameter::Message msg = launder_message(mar);
  mar = Cx::MultimediaAuthRequest(msg);
  EXPECT_EQ(IMPI, mar.impi());
  EXPECT_EQ(IMPU, mar.impu());
  // TODO: Check rest of parameters.
}

TEST_F(CxTest, SARTest)
{
  Cx::ServerAssignmentRequest sar(_cx_dict,
                                  DEST_REALM,
                                  DEST_HOST,
                                  IMPI,
                                  IMPU,
                                  SERVER_NAME);
  Diameter::Message msg = launder_message(sar);
  sar = Cx::ServerAssignmentRequest(msg);
  // TODO: Check parameters.
}

// TODO: Check other message types.
