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
#include <curl/curl.h>

#include "mockhttpstack.hpp"
#include "handlers.h"

#include "mockcache.hpp"

TEST(HandlersTest, SimpleMainline)
{
  MockHttpStack stack;
  MockHttpStack::Request req(&stack, "/", "ping");
  EXPECT_CALL(stack, send_reply(testing::_, 200));
  PingHandler* handler = new PingHandler(req);
  handler->run();
  EXPECT_EQ("OK", req.content());
}

#if 0
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;

// Transaction that would be implemented in the handlers.
class ExampleTransaction : public Cache::Transaction
{
  void on_success(Cache::Request* req)
  {
    std::string xml;
    dynamic_cast<Cache::GetIMSSubscription*>(req)->get_result(xml);
    std::cout << "GOT RESULT: " << xml << std::endl;
  }

  void on_failure(Cache::Request* req, Cache::ResultCode rc, std::string& text)
  {
    std::cout << "FAILED:" << std::endl << text << std::endl;
  }
};

// Start of the test code.
TEST(HandlersTest, ExampleTransaction)
{
  MockCache cache;
  MockCache::MockGetIMSSubscription mock_req;

  EXPECT_CALL(cache, create_GetIMSSubscription("kermit"))
    .WillOnce(Return(&mock_req));
  EXPECT_CALL(cache, send(_, &mock_req))
    .WillOnce(WithArgs<0>(Invoke(&mock_req, &Cache::Request::set_trx)));
  EXPECT_CALL(mock_req, get_result(_))
    .WillRepeatedly(SetArgReferee<0>("<some boring xml>"));

  // This would be in the code-under-test.
  ExampleTransaction* tsx = new ExampleTransaction;
  Cache::Request* req = cache.create_GetIMSSubscription("kermit");
  cache.send(tsx, req);

  // Back to the test code.
  Cache::Transaction* t = mock_req.get_trx();
  ASSERT_FALSE(t == NULL);
  t->on_success(&mock_req);
}
#endif
