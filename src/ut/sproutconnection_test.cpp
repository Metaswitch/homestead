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

class SproutConnectionTest : public testing::Test
{
  static MockHttpConnection* mock_http_conn;
  static SproutConnection* sprout_conn;
  static MockHttpRequest* mock_http_req;
  static std::vector<std::string> IMPIS;
  static std::vector<std::string> IMPUS;

  SproutConnectionTest()
  {
    TRC_ERROR("sr2sr2 create test object");
  }

  virtual ~SproutConnectionTest()
  {
    TRC_ERROR("sr2sr2 destroy with piointer %p", sprout_conn);
    Mock::VerifyAndClear(mock_http_conn);
  }

  static void SetUpTestCase()
  {
    // Common to whole suite
    TRC_ERROR("sr2sr2 setuptestcase");
    mock_http_conn = new MockHttpConnection();
    sprout_conn = new SproutConnection(mock_http_conn);
    mock_http_req = new MockHttpRequest();
  }

  static void TearDownTestCase()
  {
    TRC_ERROR("sr2sr2 testdown testcase");
    // We don't delete mock_http_conn as the SproutConnection does that for us
    delete mock_http_req; mock_http_req = nullptr;
    delete sprout_conn; sprout_conn = nullptr;
  }

  virtual void SetUp()
  {
    TRC_ERROR("sr2sr2 setup test");
  }
  virtual void TearDown()
  {
  TRC_ERROR("sr2sr2 teardown test");
  }

/*
  void pprsprout_connection(std::string impu, std::string user_data, HTTPCode http_ret_code)
  {
    // Expect a PUT to be sent to Sprout.
    std::string http_path = "/registrations/" + impu;
    std::string body =sprout_conn->ppr_create_body(user_data);

    EXPECT_CALL(*_mock_http_conn, send_put(http_path, body, _))
      .Times(1)
      .WillOnce(Return(http_ret_code));
  }*/
};

MockHttpConnection* SproutConnectionTest::mock_http_conn;
SproutConnection* SproutConnectionTest::sprout_conn;
MockHttpRequest* SproutConnectionTest::mock_http_req;
std::vector<std::string> SproutConnectionTest::IMPIS = { "_impi1@example.com", "_impi2@example.com" };
std::vector<std::string> SproutConnectionTest::IMPUS = { "sip:impu1@example.com", "sip:impu2@example.com" };

TEST_F(SproutConnectionTest, DeregisterBindingsWithNotifications)
{
  TRC_ERROR("sr2sr2 have conn");
  HttpResponse resp(HTTP_OK, "", {});
  // Expect a delete to be sent to Sprout.

  EXPECT_CALL(*mock_http_conn, create_request_proxy(HttpClient::RequestType::DELETE, "path"))
    .WillOnce(Return(mock_http_req));
    // TODO Create the MockHttpRequest at start of day, and return it here

  std::vector<std::string> vec = { "a" };
  std::vector<std::string> vec2 = {};

  //HttpCode result = sprout_conn->deregister_bindings(true, IMPUS, IMPIS, 0L);

  //EXPECT_EQ();

}

TEST_F(SproutConnectionTest, DeregisterBindingsWithNotifications2)
{
}
