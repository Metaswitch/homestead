/**
 * @file diameterstack_test.cpp UT for Sprout diameterstack module.
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

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test_utils.hpp"
#include <semaphore.h>

#include <cache.h>

using namespace std;
using namespace org::apache::cassandra;

using ::testing::Return;
using ::testing::Throw;
using ::testing::_;
using ::testing::Mock;

class MockClient : public Cache::CacheClientInterface
{
public:
  MOCK_METHOD1(set_keyspace, void(const std::string& keyspace));
  MOCK_METHOD2(batch_mutate, void(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > > & mutation_map, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD5(get_slice, void(std::vector<cass::ColumnOrSuperColumn> & _return, const std::string& key, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD4(remove, void(const std::string& key, const cass::ColumnPath& column_path, const int64_t timestamp, const cass::ConsistencyLevel::type consistency_level));
};

class TestCache : public Cache
{
public:
  MOCK_METHOD0(get_client, Cache::CacheClientInterface*());
  MOCK_METHOD0(release_client, void());
};

class TestTransaction : public Cache::Transaction
{
public:
  TestTransaction(Cache::Request* req, sem_t* sem) :
    Cache::Transaction(req), _sem(sem)
  {}

  ~TestTransaction()
  {
    sem_post(_sem);
  }

  MOCK_METHOD0(on_success, void());
  MOCK_METHOD2(on_failure, void(Cache::ResultCode error, std::string& text));

private:
  sem_t* _sem;
};

class CacheInitializationTest : public ::testing::Test
{
public:
  CacheInitializationTest()
  {
    _cache.initialize();
    _cache.configure("localhost", 1234, 1); // Start with one worker thread.
  }

  virtual ~CacheInitializationTest()
  {
    _cache.stop();
    _cache.wait_stopped();
  }

  TestCache _cache;
  MockClient _client;
};


TEST_F(CacheInitializationTest, Mainline)
{
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Return(&_client));
  EXPECT_CALL(_cache, release_client()).Times(1);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::ResultCode::OK, rc);
}


TEST_F(CacheInitializationTest, TransportException)
{
  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(te));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::ResultCode::CONNECTION_ERROR, rc);
}


TEST_F(CacheInitializationTest, NotFoundException)
{
  org::apache::cassandra::NotFoundException nfe;
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(nfe));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::ResultCode::NOT_FOUND, rc);
}


TEST_F(CacheInitializationTest, UnknownException)
{
  Cache::RowNotFoundException rnfe("muppets", "kermit");
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(rnfe));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::ResultCode::UNKNOWN_ERROR, rc);
}

class CacheRequestTest : public CacheInitializationTest
{
public:
  CacheRequestTest() : CacheInitializationTest()
  {
    sem_init(&_sem, 0, 0);

    EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Return(&_client));
    EXPECT_CALL(_cache, release_client()).Times(1);
    _cache.start();

    Mock::VerifyAndClear(&_cache);
  }

  virtual ~CacheRequestTest() {}

  TestTransaction* make_trx(Cache::Request* req)
  {
    return new TestTransaction(req, &_sem);
  }

  void wait()
  {
    struct timespec ts;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &ts);
    ASSERT_EQ(0, rc);
    ts.tv_sec += 1;
    rc = sem_timedwait(&_sem, &ts);
    ASSERT_EQ(0, rc);
  }

  sem_t _sem;
};

TEST_F(CacheRequestTest, PutIMSSubscriptionMainline)
{
  Cache::PutIMSSubscription *req =
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000, 300);
  TestTransaction *trx = make_trx(req);

  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Return(&_client));
  EXPECT_CALL(_client, batch_mutate(_, ConsistencyLevel::ONE));

  EXPECT_CALL(*trx, on_success()).Times(1);
  _cache.send(trx);

  wait();
}
