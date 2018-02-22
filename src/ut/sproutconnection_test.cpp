/**
 * @file sproutconnection_test.cpp UT for SproutConnection
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sproutconnection.h"
#include "mock_httpconnection.h"
#include "mock_http_request.h"

using ::testing::Mock;
using ::testing::Return;
using ::testing::_;

const SAS::TrailId FAKE_TRAIL_ID = 0x12345678;

class SproutConnectionTest : public testing::Test
{
  static const std::string IMPU;
  static const std::string IMPI;
  static const std::vector<std::string> IMPIS;
  static const std::vector<std::string> IMPUS;
  static const std::string IMS_SUBSCRIPTION;
  static const std::string dereg_body;
  static const std::string dereg_body_no_impis;
  static const std::string change_ids_body;

  SproutConnectionTest()
  {
    _mock_http_conn = new MockHttpConnection();
    _sprout_conn = new SproutConnection(_mock_http_conn);
    _mock_http_req = new MockHttpRequest();
  }

  virtual ~SproutConnectionTest()
  {
    Mock::VerifyAndClear(_mock_http_conn);
    Mock::VerifyAndClear(_mock_http_req);

    // We don't delete mock_http_conn as the SproutConnection does that for us
    delete _sprout_conn; _sprout_conn = nullptr;

    // We don't delete the MockHttpRequest, as that will be deleted when the
    // unique pointer returned from HttpConnection::create_request() goes out of
    //scope
  }

  MockHttpConnection* _mock_http_conn;
  SproutConnection* _sprout_conn;
  MockHttpRequest* _mock_http_req;
};

const std::string SproutConnectionTest::IMPU = "sip:impu@example.com";
const std::string SproutConnectionTest::IMPI = "_impi@example.com";
const std::vector<std::string> SproutConnectionTest::IMPIS = { "_impi1@example.com", "_impi2@example.com" };
const std::vector<std::string> SproutConnectionTest::IMPUS = { "sip:impu1@example.com", "sip:impu2@example.com" };
const std::string SproutConnectionTest::IMS_SUBSCRIPTION = "<?xml version=\"1.0\"?><IMSSubscription><PrivateID>" + IMPI + "</PrivateID><ServiceProfile><PublicIdentity><Identity>" + IMPU + "</Identity></PublicIdentity></ServiceProfile></IMSSubscription>";
const std::string SproutConnectionTest::dereg_body = "{\"registrations\":[{\"primary-impu\":\"sip:impu1@example.com\",\"impi\":\"_impi1@example.com\"},{\"primary-impu\":\"sip:impu1@example.com\",\"impi\":\"_impi2@example.com\"},{\"primary-impu\":\"sip:impu2@example.com\",\"impi\":\"_impi1@example.com\"},{\"primary-impu\":\"sip:impu2@example.com\",\"impi\":\"_impi2@example.com\"}]}";
const std::string SproutConnectionTest::dereg_body_no_impis = "{\"registrations\":[{\"primary-impu\":\"sip:impu1@example.com\"},{\"primary-impu\":\"sip:impu2@example.com\"}]}";
const std::string SproutConnectionTest::change_ids_body = "{\"user-data-xml\":\"<?xml version=\\\"1.0\\\"?><IMSSubscription><PrivateID>_impi@example.com</PrivateID><ServiceProfile><PublicIdentity><Identity>sip:impu@example.com</Identity></PublicIdentity></ServiceProfile></IMSSubscription>\"}";


TEST_F(SproutConnectionTest, DeregisterBindingsWithNotifications)
{
  // Create a response that will be returned
  HttpResponse resp(HTTP_OK, "", {});

  // Expect that the SproutConnection will create the correct HttpRequest
  EXPECT_CALL(*_mock_http_conn,
              create_request_proxy(HttpClient::RequestType::DELETE,
                                   "/registrations?send-notifications=true"))
    .WillOnce(Return(_mock_http_req));

  // Expect that the SproutConnection sets the correct fields on the HttpRequest
  EXPECT_CALL(*_mock_http_req, set_body(dereg_body)).Times(1);
  EXPECT_CALL(*_mock_http_req, set_sas_trail(FAKE_TRAIL_ID)).Times(1);

  // Expect that the request is sent, and set it to return the response
  EXPECT_CALL(*_mock_http_req, send()).WillOnce(Return(resp));

  // Actually deregister_bindings
  HTTPCode result = _sprout_conn->deregister_bindings(true, IMPUS, IMPIS, FAKE_TRAIL_ID);

  // Expect that we get the correct return code
  EXPECT_EQ(HTTP_OK, result);
}

TEST_F(SproutConnectionTest, DeregisterBindingsWithoutNotifications)
{
  // Create a response that will be returned. This one will return an error
  HttpResponse resp(HTTP_OK, "", {});

  // Expect that the SproutConnection will create the correct HttpRequest
  EXPECT_CALL(*_mock_http_conn,
              create_request_proxy(HttpClient::RequestType::DELETE,
                                   "/registrations?send-notifications=false"))
    .WillOnce(Return(_mock_http_req));

  // Expect that the SproutConnection sets the correct fields on the HttpRequest
  EXPECT_CALL(*_mock_http_req, set_body(dereg_body)).Times(1);
  EXPECT_CALL(*_mock_http_req, set_sas_trail(FAKE_TRAIL_ID)).Times(1);

  // Expect that the request is sent, and set it to return the response
  EXPECT_CALL(*_mock_http_req, send()).WillOnce(Return(resp));

  // Actually deregister_bindings
  HTTPCode result = _sprout_conn->deregister_bindings(false, IMPUS, IMPIS, FAKE_TRAIL_ID);

  // Expect that we get the correct return code
  EXPECT_EQ(HTTP_OK, result);
}

TEST_F(SproutConnectionTest, DeregisterBindingsEmptyImpis)
{
  // Create a response that will be returned
  HttpResponse resp(HTTP_OK, "", {});

  // Expect that the SproutConnection will create the correct HttpRequest
  EXPECT_CALL(*_mock_http_conn,
              create_request_proxy(HttpClient::RequestType::DELETE,
                                   "/registrations?send-notifications=false"))
    .WillOnce(Return(_mock_http_req));

  // Expect that the SproutConnection sets the correct fields on the HttpRequest
  EXPECT_CALL(*_mock_http_req, set_body(dereg_body_no_impis)).Times(1);
  EXPECT_CALL(*_mock_http_req, set_sas_trail(FAKE_TRAIL_ID)).Times(1);

  // Expect that the request is sent, and set it to return the response
  EXPECT_CALL(*_mock_http_req, send()).WillOnce(Return(resp));

  // Actually deregister_bindings
  HTTPCode result = _sprout_conn->deregister_bindings(false, IMPUS, {}, FAKE_TRAIL_ID);

  // Expect that we get the correct return code
  EXPECT_EQ(HTTP_OK, result);
}

TEST_F(SproutConnectionTest, DeregisterBindingsError)
{
  // Create an error response that will be returned
  HttpResponse resp(HTTP_SERVER_UNAVAILABLE, "", {});

  // Expect that the SproutConnection will create a request
  EXPECT_CALL(*_mock_http_conn, create_request_proxy(_, _))
    .WillOnce(Return(_mock_http_req));

  // Expect that the SproutConnection sets some fields on the HttpRequest
  EXPECT_CALL(*_mock_http_req, set_body(_)).Times(1);
  EXPECT_CALL(*_mock_http_req, set_sas_trail(_)).Times(1);

  // Expect that the request is sent, and set it to return the response
  EXPECT_CALL(*_mock_http_req, send()).WillOnce(Return(resp));

  // Actually deregister_bindings
  HTTPCode result = _sprout_conn->deregister_bindings(false, IMPUS, {}, FAKE_TRAIL_ID);

  // Expect that we get the correct return code
  EXPECT_EQ(HTTP_SERVER_UNAVAILABLE, result);
}

TEST_F(SproutConnectionTest, ChangeAssociatedIdentities)
{
  // Create a response that will be returned
  HttpResponse resp(HTTP_OK, "", {});

  // Expect that the SproutConnection will create the correct HttpRequest
  EXPECT_CALL(*_mock_http_conn,
              create_request_proxy(HttpClient::RequestType::PUT,
                                   "/registrations/" + IMPU))
    .WillOnce(Return(_mock_http_req));

  // Expect that the SproutConnection sets the correct fields on the HttpRequest
  EXPECT_CALL(*_mock_http_req, set_body(change_ids_body)).Times(1);
  EXPECT_CALL(*_mock_http_req, set_sas_trail(FAKE_TRAIL_ID)).Times(1);

  // Expect that the request is sent, and set it to return the response
  EXPECT_CALL(*_mock_http_req, send()).WillOnce(Return(resp));

  // Change the identities
  HTTPCode result = _sprout_conn->change_associated_identities(IMPU, IMS_SUBSCRIPTION, FAKE_TRAIL_ID);

  // Expect that we get the correct return code
  EXPECT_EQ(HTTP_OK, result);
}
