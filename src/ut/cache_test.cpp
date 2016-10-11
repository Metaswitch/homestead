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
#include <semaphore.h>
#include <time.h>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test_utils.hpp"
#include "test_interposer.hpp"
#include "fakelogger.h"

#include "mock_a_record_resolver.h"
#include "mock_cassandra_connection_pool.h"
#include "mock_cassandra_store.h"
#include "mockcommunicationmonitor.h"
#include "cass_test_utils.h"

#include <cache.h>

using ::testing::PrintToString;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Throw;
using ::testing::_;
using ::testing::Mock;
using ::testing::MakeMatcher;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::Invoke;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Gt;
using ::testing::Lt;
using ::testing::NiceMock;

using namespace CassTestUtils;

// Test constants.
const std::vector<std::string> IMPIS = {"somebody@example.com"};
const std::vector<std::string> EMPTY_IMPIS;
const std::deque<std::string> NO_CFS = {};
const std::deque<std::string> CCF = {"ccf"};
const std::deque<std::string> CCFS = {"ccf1", "ccf2"};
const std::deque<std::string> ECF = {"ecf"};
const std::deque<std::string> ECFS = {"ecf1", "ecf2"};
const ChargingAddresses NO_CHARGING_ADDRS(NO_CFS, NO_CFS);
const ChargingAddresses FULL_CHARGING_ADDRS(CCFS, ECFS);
const ChargingAddresses CCFS_CHARGING_ADDRS(CCFS, ECF);
const ChargingAddresses ECFS_CHARGING_ADDRS(CCF, ECFS);

// The class under test.
//
// We don't test the Cache class directly as we need to use a
// MockCassandraConnectionPool that we can use to return MockCassandraClients.
// However all other methods are the real ones from Cache.
class TestCache : public Cache
{
public:
  void set_conn_pool(CassandraStore::CassandraConnectionPool* pool)
  {
    delete _conn_pool;
    _conn_pool = pool;
  }
};

//
// TEST FIXTURES.
//

// Fixture for tests that cover cache initialization processing.
//
// In reality only the start() method is interesting, so the fixture handles
// calling initialize() and configure()
class CacheInitializationTest : public ::testing::Test
{
public:
  CacheInitializationTest()
  {
    _targets.push_back(create_target("10.0.0.1"));
    _targets.push_back(create_target("10.0.0.2"));
    _iter = new SimpleAddrIterator(_targets);

    _cache.set_conn_pool(_pool);
    _cache.configure_connection("localhost", 1234, _cm, &_resolver);
    _cache.configure_workers(NULL, 1, 0); // Start with one worker thread.

    // Each test should trigger exactly one lookup
    EXPECT_CALL(_resolver, resolve_iter(_,_,_)).WillOnce(Return(_iter));

    // The get_connection() method should just return the mock client whenever
    // it is called. In some tests we care about the number of times we'll call
    // this, so we'll override this expectation in those ones
    EXPECT_CALL(*_pool, get_client()).Times(testing::AnyNumber()).WillRepeatedly(Return(&_client));

    // We expect connect(), is_connected() and set_keyspace() to be called in
    // every test. By default, just mock them out so that we don't get warnings.
    EXPECT_CALL(_client, set_keyspace(_)).Times(testing::AnyNumber());
    EXPECT_CALL(_client, connect()).Times(testing::AnyNumber());
    EXPECT_CALL(_client, is_connected()).Times(testing::AnyNumber()).WillRepeatedly(Return(false));
  }

  virtual ~CacheInitializationTest()
  {
    _cache.stop();
    _cache.wait_stopped();
    delete _cm; _cm = NULL;
    delete _am; _am = NULL;
    delete _iter; _iter = NULL;
  }

  TestCache _cache;
  MockCassandraClient _client;
  MockCassandraConnectionPool* _pool = new MockCassandraConnectionPool();
  MockCassandraResolver _resolver;
  SimpleAddrIterator* _iter;
  AlarmManager* _am = new AlarmManager();
  NiceMock<MockCommunicationMonitor>* _cm = new NiceMock<MockCommunicationMonitor>(_am);

  // Some dummy targets for our resolver
  std::vector<AddrInfo> _targets;

  AddrInfo create_target(std::string address)
  {
    AddrInfo ai;
    BaseResolver::parse_ip_target(address, ai.address);
    ai.port = 1;
    ai.transport = IPPROTO_TCP;
    return ai;
  }
};


// Fixture for tests that make requests to the cache (but are not interested in
// testing initialization).
class CacheRequestTest : public CacheInitializationTest
{
public:
  CacheRequestTest() : CacheInitializationTest()
  {
    sem_init(&_sem, 0, 0);

    // success() will be called in almost every test, and most of the time we're
    // not testing it explicitly so we just want to remove the test warnings.
    // For those test cases where we care about success() being called, we will
    // override this EXPECT_CALL
    EXPECT_CALL(_resolver, success(_)).Times(testing::AnyNumber());

    _cache.start();
  }

  virtual ~CacheRequestTest() {}

  // Helper methods to make a TestTransaction or RecordingTransation. This
  // passes the semaphore into the transaction constructor - this is posted to
  // when the transaction completes.
  TestTransaction* make_trx()
  {
    return new TestTransaction(&_sem);
  }

  RecordingTransaction* make_rec_trx(ResultRecorderInterface *recorder)
  {
    return new RecordingTransaction(&_sem, recorder);
  }

  // Wait for a single request to finish.  This method asserts if the request
  // takes too long (> 1s) which implies the request has been dropped by the
  // cache.
  void wait()
  {
    struct timespec ts;
    int rc;
    rc = clock_gettime(CLOCK_REALTIME, &ts);
    ASSERT_EQ(0, rc);
    ts.tv_sec += 2;
    rc = sem_timedwait(&_sem, &ts);
    ASSERT_EQ(0, rc);
  }

  // Helper method to send an operation and wait for it to succeed.
  void execute_trx(CassandraStore::Operation* op, TestTransaction* trx)
  {
    CassandraStore::Transaction* _trx = trx; trx = NULL;
    _cache.do_async(op, _trx);
    wait();
  }

  // Semaphore that the main thread waits on while a transaction is outstanding.
  sem_t _sem;
};

class CacheLatencyTest : public CacheRequestTest
{
public:
  CacheLatencyTest() : CacheRequestTest()
  {
    cwtest_completely_control_time();
  }

  virtual ~CacheLatencyTest() { cwtest_reset_time(); }

  // This test mucks around with time so we override wait to jusr wait on the
  // semaphore rather than the safer timedwait (which spots script hangs).
  // Hopefully any functional hanging-type bugs will already have caused the
  // script to fail before the Latency tests are run.
  void wait()
  {
    sem_wait(&_sem);
  }
};


//
// TESTS
//

TEST_F(CacheInitializationTest, Mainline)
{
  EXPECT_CALL(_client, connect()).Times(1);
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::OK, rc);
  rc = _cache.start();
  EXPECT_EQ(CassandraStore::OK, rc);
}


TEST_F(CacheInitializationTest, OneTransportException)
{
  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_client, connect()).Times(2).WillOnce(Throw(te)).WillRepeatedly(Return());

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  {
    testing::InSequence s;

    EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
    EXPECT_CALL(_resolver, success(_targets[1])).Times(1);
  }

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::OK, rc);
}


TEST_F(CacheInitializationTest, TwoTransportExceptions)
{
  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_client, connect()).Times(2).WillRepeatedly(Throw(te));

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
  EXPECT_CALL(_resolver, blacklist(_targets[1])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::CONNECTION_ERROR, rc);
}


TEST_F(CacheInitializationTest, NotFoundException)
{
  cass::NotFoundException nfe;
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(nfe));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::NOT_FOUND, rc);
}


TEST_F(CacheInitializationTest, RowNotFoundException)
{
  CassandraStore::RowNotFoundException rnfe("muppets", "kermit");
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(rnfe));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::NOT_FOUND, rc);
}


TEST_F(CacheInitializationTest, UnavailableException)
{
  cass::UnavailableException ue;
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(ue));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::UNAVAILABLE, rc);
}


TEST_F(CacheInitializationTest, UnknownException)
{
  std::string ex("Made up exception");
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(ex));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::UNKNOWN_ERROR, rc);
}


TEST_F(CacheInitializationTest, Connection)
{
  // If is_connected() returns true, connect() should not be called
  EXPECT_CALL(_client, is_connected()).WillOnce(Return(true));
  EXPECT_CALL(_client, connect()).Times(0);

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::OK, rc);
}


TEST_F(CacheRequestTest, PutRegDataMainline)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000, 300);
  put_reg_data->with_xml("<xml>")
               .with_reg_state(RegistrationState::REGISTERED)
               .with_associated_impis(IMPIS)
               .with_charging_addrs(FULL_CHARGING_ADDRS);

  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["ims_subscription_xml"] = "<xml>";
  impu_columns["is_registered"] = "\x01";
  impu_columns["primary_ccf"] = "ccf1";
  impu_columns["secondary_ccf"] = "ccf2";
  impu_columns["primary_ecf"] = "ecf1";
  impu_columns["secondary_ecf"] = "ecf2";
  impu_columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", impu_columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, PutRegDataUnregistered)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000, 300);
  put_reg_data->with_xml("<xml>")
               .with_reg_state(RegistrationState::UNREGISTERED)
               .with_associated_impis(IMPIS)
               .with_charging_addrs(CCFS_CHARGING_ADDRS);

  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = std::string("\x00", 1);
  columns["primary_ccf"] = "ccf1";
  columns["secondary_ccf"] = "ccf2";
  columns["primary_ecf"] = "ecf";
  columns["secondary_ecf"] = "";
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, NoTTLOnPut)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>")
               .with_reg_state(RegistrationState::REGISTERED)
               .with_associated_impis(IMPIS)
               .with_charging_addrs(ECFS_CHARGING_ADDRS);

  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = "\x01";
  columns["primary_ccf"] = "ccf";
  columns["secondary_ccf"] = "";
  columns["primary_ecf"] = "ecf1";
  columns["secondary_ecf"] = "ecf2";
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, PutRegDataMultipleIDs)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("miss piggy");

  std::vector<CassandraStore::RowColumns> expected;

  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData(ids, 1000);
  put_reg_data->with_xml("<xml>")
               .with_reg_state(RegistrationState::REGISTERED)
               .with_associated_impis(IMPIS)
               .with_charging_addrs(NO_CHARGING_ADDRS);

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = "\x01";
  columns["primary_ccf"] = "";
  columns["secondary_ccf"] = "";
  columns["primary_ecf"] = "";
  columns["secondary_ecf"] = "";
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", columns));
  expected.push_back(CassandraStore::RowColumns("impu", "miss piggy", columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, PutRegDataNoXml)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("miss piggy");

  std::vector<CassandraStore::RowColumns> expected;

  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData(ids, 1000);
  put_reg_data->with_charging_addrs(FULL_CHARGING_ADDRS);

  std::map<std::string, std::string> columns;
  columns["primary_ccf"] = "ccf1";
  columns["secondary_ccf"] = "ccf2";
  columns["primary_ecf"] = "ecf1";
  columns["secondary_ecf"] = "ecf2";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", columns));
  expected.push_back(CassandraStore::RowColumns("impu", "miss piggy", columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, PutRegDataNoChargingAddresses)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("miss piggy");

  std::vector<CassandraStore::RowColumns> expected;

  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData(ids, 1000);
  put_reg_data->with_xml("<xml>");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", columns));
  expected.push_back(CassandraStore::RowColumns("impu", "miss piggy", columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}
// TODO move this up.
MATCHER_P(OperationHasResult, expected_rc, "")
{
  CassandraStore::ResultCode actual_rc = arg->get_result_code();
  return (expected_rc == actual_rc);
}


TEST_F(CacheRequestTest, PutOneTransportEx)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  apache::thrift::transport::TTransportException te;

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  {
    testing::InSequence s;

    EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(te)).RetiresOnSaturation();
    EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
    EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Return());
    EXPECT_CALL(_resolver, success(_targets[1])).Times(1);
  }

  EXPECT_CALL(*trx, on_success(_));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutTwoTransportEx)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  apache::thrift::transport::TTransportException te;

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
  EXPECT_CALL(_resolver, blacklist(_targets[1])).Times(1);
  EXPECT_CALL(_client, batch_mutate(_, _)).Times(2).WillRepeatedly(Throw(te));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::CONNECTION_ERROR)));
  EXPECT_CALL(*_cm, inform_failure(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutConnectTransportExThenPutTransportEx)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  apache::thrift::transport::TTransportException te;

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  EXPECT_CALL(_client, connect()).Times(2).WillOnce(Throw(te)).WillRepeatedly(Return());
  EXPECT_CALL(_client, batch_mutate(_, _)).Times(1).WillOnce(Throw(te));
  EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
  EXPECT_CALL(_resolver, blacklist(_targets[1])).Times(1);

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::CONNECTION_ERROR)));
  EXPECT_CALL(*_cm, inform_failure(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutTransportThenUnknownException)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  apache::thrift::transport::TTransportException te;
  std::string ex("Made up exception");

  // We should ask for 2 clients from the pool
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(te)).WillOnce(Throw(ex));
  EXPECT_CALL(_resolver, blacklist(_targets[0])).Times(1);
  EXPECT_CALL(_resolver, success(_targets[1])).Times(1);

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::UNKNOWN_ERROR)));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutInvalidRequestException)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  cass::InvalidRequestException ire;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);
  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::INVALID_REQUEST)));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutNotFoundException)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(nfe));

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);
  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutNoResultsException)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  CassandraStore::RowNotFoundException rnfe("muppets", "kermit");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(rnfe));

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);
  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutUnknownException)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  std::string ex("Made up exception");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ex));

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);
  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::UNKNOWN_ERROR)));
  EXPECT_CALL(*_cm, inform_success(_));
  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutsHaveConsistencyLevelOne)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);
  EXPECT_CALL(_client, batch_mutate(_, cass::ConsistencyLevel::ONE));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheRequestTest, PutAuthVectorMainline)
{
  DigestAuthVector av;
  av.ha1 = "somehash";
  av.realm = "themuppetshow.com";
  av.qop = "auth";

  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_PutAuthVector("gonzo", av, 1000);

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = av.ha1;
  columns["digest_realm"] = av.realm;
  columns["digest_qop"] = av.qop;

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, PutAsoocPublicIdMainline)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_PutAssociatedPublicID("gonzo", "kermit", 1000);

  std::map<std::string, std::string> columns;
  columns["public_id_kermit"] = "";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, PutAssocPublicIdTTL)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_PutAssociatedPublicID("gonzo", "kermit", 1000, 300);

  std::map<std::string, std::string> columns;
  columns["public_id_kermit"] = "";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000, 300), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, PutAssocPrivateIdMainline)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_PutAssociatedPrivateID({"kermit", "miss piggy"}, "gonzo", 1000);

  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__gonzo"] = "";

  expected.push_back(CassandraStore::RowColumns("impu", "kermit", impu_columns));
  expected.push_back(CassandraStore::RowColumns("impu", "miss piggy", impu_columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzo", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, DeletePublicId)
{
  std::vector<CassandraStore::RowColumns> expected;

  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  // The "kermit" IMPU row should be deleted entirely
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));

  // The "kermit" column should be deleted from the IMPI's row in the
  // IMPI mapping table
  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", deleted_impi_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, DeleteMultiPublicIds)
{
  std::vector<CassandraStore::RowColumns> expected;

  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePublicIDs(ids, IMPIS, 1000);

  // The "kermit", "gonzo" and "miss piggy" IMPU rows should be deleted entirely
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));
  EXPECT_CALL(_client, remove("gonzo", _, 1000, _));
  EXPECT_CALL(_client, remove("miss piggy", _, 1000, _));

  // Only the "kermit" column should be deleted from the IMPI's row in the
  // IMPI mapping table
  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "somebody@example.com", deleted_impi_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, DeletePrivateId)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePrivateIDs("kermit", 1000);

  EXPECT_CALL(_client,
              remove("kermit",
                     ColumnPathForTable("impi"),
                     1000,
                     cass::ConsistencyLevel::ONE));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, DeleteMultiPrivateIds)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePrivateIDs(ids, 1000);

  EXPECT_CALL(_client, remove("kermit", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("gonzo", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("miss piggy", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, DeleteIMPIMappings)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeleteIMPIMapping(ids, 1000);

  std::vector<CassandraStore::RowColumns> expected;
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));
  EXPECT_CALL(_client, remove("gonzo", _, 1000, _));
  EXPECT_CALL(_client, remove("miss piggy", _, 1000, _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}



TEST_F(CacheRequestTest, DeletesHaveConsistencyLevelOne)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  EXPECT_CALL(_client, remove(_, _, _, cass::ConsistencyLevel::ONE));
  EXPECT_CALL(_client, batch_mutate(_, cass::ConsistencyLevel::ONE));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, GetRegDataMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = "\x01";
  columns["primary_ccf"] = "ccf1";
  columns["secondary_ccf"] = "ccf2";
  columns["primary_ecf"] = "ecf1";
  columns["secondary_ecf"] = "ecf2";
  columns["associated_impi__somebody@example.com"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ(RegistrationState::REGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);
  EXPECT_EQ(CCFS, rec.result.charging_addrs.ccfs);
  EXPECT_EQ(ECFS, rec.result.charging_addrs.ecfs);
}

TEST_F(CacheRequestTest, GetRegDataTTL)
{
  std::map<std::string, std::string> columns;
  columns["is_registered"] = "\x01";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ(RegistrationState::REGISTERED, rec.result.state);
  EXPECT_EQ("", rec.result.xml);
  EXPECT_EQ(EMPTY_IMPIS, rec.result.impis);
}

TEST_F(CacheRequestTest, GetRegDataUnregistered)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = std::string("\x00", 1);
  columns["associated_impi__somebody@example.com"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  // Test with a TTL of 3600
  make_slice(slice, columns, 3600);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ(RegistrationState::UNREGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);
}

// If we have User-Data XML, but no explicit registration state, that should
// still be treated as unregistered state.

TEST_F(CacheRequestTest, GetRegDataNoRegState)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");
  requested_columns.push_back("is_registered");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = "";
  columns["associated_impi__somebody@example.com"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ(RegistrationState::UNREGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);

}

// Invalid registration state is treated as NOT_REGISTERED

TEST_F(CacheRequestTest, GetRegDataInvalidRegState)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "";
  columns["is_registered"] = "\x03";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ(RegistrationState::NOT_REGISTERED, rec.result.state);
  EXPECT_EQ("", rec.result.xml);
  EXPECT_EQ(EMPTY_IMPIS, rec.result.impis);
}


TEST_F(CacheRequestTest, GetRegDataNotFound)
{
  CassandraStore::Operation* op =
    _cache.create_GetRegData("kermit");
  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);

  EXPECT_CALL(_client, get_slice(_, "kermit", _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("", rec.result.xml);
  EXPECT_EQ(RegistrationState::NOT_REGISTERED, rec.result.state);
  EXPECT_EQ(EMPTY_IMPIS, rec.result.impis);
}

TEST_F(CacheRequestTest, GetAuthVectorAllColsReturned)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("digest_ha1");
  requested_columns.push_back("digest_realm");
  requested_columns.push_back("digest_qop");

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impi"),
                                 SpecificColumns(requested_columns),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("themuppetshow.com", rec.result.realm);
  EXPECT_EQ("auth", rec.result.qop);
}


TEST_F(CacheRequestTest, GetAuthVectorNonDefaultableColsReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("", rec.result.realm);
  EXPECT_EQ("", rec.result.qop);
}


TEST_F(CacheRequestTest, GetAuthVectorHa1NotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, GetAuthVectorNoColsReturned)
{
  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, GetAuthVectorPublicIdRequested)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("digest_ha1");
  requested_columns.push_back("digest_realm");
  requested_columns.push_back("digest_qop");
  requested_columns.push_back("public_id_gonzo");

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";
  columns["public_id_gonzo"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit", "gonzo");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 _,
                                 SpecificColumns(requested_columns),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("themuppetshow.com", rec.result.realm);
  EXPECT_EQ("auth", rec.result.qop);
}


TEST_F(CacheRequestTest, GetAuthVectorPublicIdRequestedNotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit", "gonzo");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, GetAssocPublicIDsMainline)
{
  std::map<std::string, std::string> columns;
  columns["public_id_gonzo"] = "";
  columns["public_id_miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> inner_slice;
  make_slice(inner_slice, columns);
  std::map<std::string, std::vector<cass::ColumnOrSuperColumn> > slice;
  slice["kermit"] = inner_slice;

  ResultRecorder<Cache::GetAssociatedPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAssociatedPublicIDs("kermit");

  std::vector<std::string> impis = {"kermit"};

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi"),
                             ColumnsWithPrefix("public_id_"),
                             _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  std::vector<std::string> expected_ids;
  expected_ids.push_back("gonzo");
  expected_ids.push_back("miss piggy");
  std::sort(expected_ids.begin(), expected_ids.end());
  std::sort(rec.result.begin(), rec.result.end());

  EXPECT_EQ(expected_ids, rec.result);
}


TEST_F(CacheRequestTest, GetAssocPublicIDsMultipleIDs)
{
  std::map<std::string, std::string> columns;
  std::map<std::string, std::string> columns2;
  columns["public_id_gonzo"] = "";
  columns2["public_id_miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> inner_slice;
  make_slice(inner_slice, columns);
  std::map<std::string, std::vector<cass::ColumnOrSuperColumn> > slice;
  slice["kermit"] = inner_slice;

  std::vector<cass::ColumnOrSuperColumn> inner_slice2;
  make_slice(inner_slice2, columns2);
  slice["miss_piggy"] = inner_slice2;

  ResultRecorder<Cache::GetAssociatedPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  std::vector<std::string> impis = {"kermit", "miss piggy"};
  CassandraStore::Operation* op = _cache.create_GetAssociatedPublicIDs(impis);

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi"),
                             ColumnsWithPrefix("public_id_"),
                             _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  std::vector<std::string> expected_ids;
  expected_ids.push_back("gonzo");
  expected_ids.push_back("miss piggy");
  std::sort(expected_ids.begin(), expected_ids.end());
  std::sort(rec.result.begin(), rec.result.end());

  EXPECT_EQ(expected_ids, rec.result);
}


TEST_F(CacheRequestTest, GetAssocPublicIDsNoResults)
{
  ResultRecorder<Cache::GetAssociatedPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAssociatedPublicIDs("kermit");

  std::vector<std::string> impis = {"kermit"};

  EXPECT_CALL(_client, multiget_slice(_, impis, _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice_multiget));

  // Expect on_success to fire, but results should be empty.
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_TRUE(rec.result.empty());
}

TEST_F(CacheRequestTest, GetAssociatedPrimaryPublicIDs)
{
  std::map<std::string, std::string> columns;
  columns["associated_primary_impu__kermit"] = "";
  columns["associated_primary_impu__miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> inner_slice;
  make_slice(inner_slice, columns);
  std::map<std::string, std::vector<cass::ColumnOrSuperColumn> > slice;
  slice["gonzo"] = inner_slice;

  ResultRecorder<Cache::GetAssociatedPrimaryPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAssociatedPrimaryPublicIDs("gonzo");

  std::vector<std::string> impis = {"gonzo"};

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi_mapping"),
                             ColumnsWithPrefix("associated_primary_impu__"),
                             _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  std::vector<std::string> expected_ids;
  expected_ids.push_back("kermit");
  expected_ids.push_back("miss piggy");
  std::sort(expected_ids.begin(), expected_ids.end());
  std::sort(rec.result.begin(), rec.result.end());

  EXPECT_EQ(expected_ids, rec.result);
}

TEST_F(CacheRequestTest, GetAssociatedPrimaryPublicIDsMultipleIMPIs)
{
  std::map<std::string, std::string> columns;
  std::map<std::string, std::string> columns2;
  columns["associated_primary_impu__kermit"] = "";
  columns2["associated_primary_impu__miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> inner_slice;
  make_slice(inner_slice, columns);
  std::map<std::string, std::vector<cass::ColumnOrSuperColumn> > slice;
  slice["gonzo"] = inner_slice;

  std::vector<cass::ColumnOrSuperColumn> inner_slice2;
  make_slice(inner_slice2, columns2);
  slice["gonzo2"] = inner_slice2;

  ResultRecorder<Cache::GetAssociatedPrimaryPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  std::vector<std::string> impis = {"gonzo", "gonzo2"};
  CassandraStore::Operation* op = _cache.create_GetAssociatedPrimaryPublicIDs(impis);

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi_mapping"),
                             ColumnsWithPrefix("associated_primary_impu__"),
                             _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  std::vector<std::string> expected_ids;
  expected_ids.push_back("kermit");
  expected_ids.push_back("miss piggy");
  std::sort(expected_ids.begin(), expected_ids.end());
  std::sort(rec.result.begin(), rec.result.end());

  EXPECT_EQ(expected_ids, rec.result);
}

TEST_F(CacheRequestTest, GetAssociatedPrimaryPublicIDsNoResults)
{
  ResultRecorder<Cache::GetAssociatedPrimaryPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAssociatedPrimaryPublicIDs("gonzo");

  std::vector<std::string> impis = {"gonzo"};

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi_mapping"),
                             ColumnsWithPrefix("associated_primary_impu__"),
                             _))
    .WillOnce(SetArgReferee<0>(empty_slice_multiget));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_TRUE(rec.result.empty());
}

TEST_F(CacheRequestTest, HaGetMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = "\x01";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, Cache::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);
}


TEST_F(CacheRequestTest, HaGet2ndReadNotFoundException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, HaGet2ndReadUnavailableException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  cass::UnavailableException ue;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(Throw(ue));

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::ONE))
    .WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, HaGet2ndReadTimedUutException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  cass::TimedOutException te;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(Throw(te));

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::ONE))
    .WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST(CacheGenerateTimestamp, CreatesMicroTimestamp)
{
  struct timespec ts;
  int rc;

  // Get the current time and check that generate_timestamp gives the same value
  // in microseconds (to with 100ms grace).
  rc = clock_gettime(CLOCK_REALTIME, &ts);
  ASSERT_EQ(0, rc);

  int64_t grace = 100000;
  int64_t us_curr = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  EXPECT_THAT(Cache::generate_timestamp(),
              AllOf(Gt(us_curr - grace), Lt(us_curr + grace)));
}


ACTION_P(AdvanceTimeMs, ms) { cwtest_advance_time_ms(ms); }
ACTION_P2(CheckLatency, trx, ms) { trx->check_latency(ms * 1000); }

TEST_F(CacheLatencyTest, PutRecordsLatency)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(AdvanceTimeMs(12));
  EXPECT_CALL(*trx, on_success(_)).WillOnce(CheckLatency(trx, 12));

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}


TEST_F(CacheLatencyTest, DeleteRecordsLatency)
{
  TestTransaction *trx = make_trx();
  CassandraStore::Operation* op =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  EXPECT_CALL(_client, remove(_, _, _, _));
  EXPECT_CALL(_client, batch_mutate(_, _)).WillRepeatedly(AdvanceTimeMs(13));
  EXPECT_CALL(*trx, on_success(_)).WillOnce(CheckLatency(trx, 13));

  execute_trx(op, trx);
}


TEST_F(CacheLatencyTest, GetRecordsLatency)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetRegData, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(DoAll(SetArgReferee<0>(slice),
                    AdvanceTimeMs(14)));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(DoAll(Invoke(trx, &RecordingTransaction::record_result),
                    CheckLatency(trx, 14)));

  execute_trx(op, trx);
}


TEST_F(CacheLatencyTest, ErrorRecordsLatency)
{
  TestTransaction *trx = make_trx();
  Cache::PutRegData* put_reg_data = _cache.create_PutRegData("kermit", 1000);
  put_reg_data->with_xml("<xml>");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, batch_mutate(_, _))
    .WillOnce(DoAll(AdvanceTimeMs(12), Throw(nfe)));
  EXPECT_CALL(*trx, on_failure(_)).WillOnce(CheckLatency(trx, 12));

  execute_trx((CassandraStore::Operation*)put_reg_data, trx);
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromImpi)
{
  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__somebody@example.com"] = "";
  impu_columns["associated_impi__gonzo"] = "";

  std::map<std::string, std::string> deleted_impu_columns;
  deleted_impu_columns["associated_impi__gonzo"] = "";

  std::vector<cass::ColumnOrSuperColumn> impu_slice;
  make_slice(impu_slice, impu_columns);

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";
  impi_columns["associated_primary_impu__miss piggy"] = "";

  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";

  std::vector<cass::ColumnOrSuperColumn> impi_slice;
  make_slice(impi_slice, impi_columns);

  TestTransaction* trx = make_trx();
  CassandraStore::Operation* op = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzo", 1000);

  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzo", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CassandraStore::RowColumns("impu", "kermit", deleted_impu_columns));
  expected.push_back(CassandraStore::RowColumns("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromMultipleImpis)
{
  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__somebody@example.com"] = "";
  impu_columns["associated_impi__gonzo"] = "";
  impu_columns["associated_impi__gonzo2"] = "";

  std::map<std::string, std::string> deleted_impu_columns;
  deleted_impu_columns["associated_impi__gonzo"] = "";
  deleted_impu_columns["associated_impi__gonzo2"] = "";

  std::vector<cass::ColumnOrSuperColumn> impu_slice;
  make_slice(impu_slice, impu_columns);

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";
  impi_columns["associated_primary_impu__miss piggy"] = "";

  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";

  std::vector<cass::ColumnOrSuperColumn> impi_slice;
  make_slice(impi_slice, impi_columns);

  TestTransaction* trx = make_trx();
  std::vector<std::string> impis;
  impis.push_back("gonzo");
  impis.push_back("gonzo2");
  CassandraStore::Operation* op = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, impis, 1000);

  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzo", deleted_impi_columns));
  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzo2", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CassandraStore::RowColumns("impu", "kermit", deleted_impu_columns));
  expected.push_back(CassandraStore::RowColumns("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}


TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromImpiCausingDeletion)
{
  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__gonzo"] = "";

  std::vector<cass::ColumnOrSuperColumn> impu_slice;
  make_slice(impu_slice, impu_columns);


  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  std::vector<cass::ColumnOrSuperColumn> impi_slice;
  make_slice(impi_slice, impi_columns);

  TestTransaction* trx = make_trx();
  CassandraStore::Operation* op = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzo", 1000);

  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzo", impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect full-column removal, twice
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));
  EXPECT_CALL(_client, remove("robin", _, 1000, _));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  execute_trx(op, trx);
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromWrongImpi)
{
  std::vector<CassandraStore::RowColumns> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__somebody@example.com"] = "";
  impu_columns["associated_impi__gonzo"] = "";

  std::map<std::string, std::string> deleted_impu_columns;
  deleted_impu_columns["associated_impi__gonzoooooo"] = "";

  std::vector<cass::ColumnOrSuperColumn> impu_slice;
  make_slice(impu_slice, impu_columns);

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";
  impi_columns["associated_primary_impu__miss piggy"] = "";

  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";

  std::vector<cass::ColumnOrSuperColumn> impi_slice;
  make_slice(impi_slice, impi_columns);

  TestTransaction* trx = make_trx();
  CassandraStore::Operation* op = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzoooooo", 1000);

  expected.push_back(CassandraStore::RowColumns("impi_mapping", "gonzoooooo", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CassandraStore::RowColumns("impu", "kermit", deleted_impu_columns));
  expected.push_back(CassandraStore::RowColumns("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  EXPECT_CALL(*trx, on_success(_));

  CapturingTestLogger log;
  execute_trx(op, trx);
  EXPECT_TRUE(log.contains("not all the provided IMPIs are associated with the IMPU"));
}

//
// Tests for listing IMPUs in the cache.
//

// Helper class to save off the IMPUs retrieved from the database.
class ListImpusRecorder : public ResultRecorderInterface
{
public:
  void save(CassandraStore::Operation* op)
  {
    impus = dynamic_cast<Cache::ListImpus*>(op)->get_impus_reference();
  }

  std::vector<std::string> impus;
};

// Tests listing IMPUs when there aren't any in the database.
TEST_F(CacheRequestTest, ListImpusNoResults)
{
  ListImpusRecorder rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_ListImpus();

  std::vector<cass::KeySlice> result;
  std::vector<std::string> requested_columns = {"ims_subscription_xml"};
  EXPECT_CALL(_client,
              get_range_slices(_,
                               ColumnPathForTable("impu"),
                               SpecificColumns(requested_columns),
                               KeysInRange("", ""),
                               _))
    .WillOnce(SetArgReferee<0>(result));
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));

  execute_trx(op, trx);

  EXPECT_TRUE(rec.impus.empty());
}

// Listing multiple IMPUs.
TEST_F(CacheRequestTest, ListImpusTwoResults)
{
  ListImpusRecorder rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_ListImpus();

  std::vector<cass::KeySlice> result(2);
  result[0].key = "Kermit1";
  make_slice(result[0].columns, {{"ims_subscription_xml", "<xml/>"}});
  result[1].key = "Kermit2";
  make_slice(result[1].columns, {{"ims_subscription_xml", "<xml/>"}});

  EXPECT_CALL(_client, get_range_slices(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(result));
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));

  execute_trx(op, trx);

  // Two IMPUs are returned and they are returned in the same order.
  EXPECT_EQ(2, rec.impus.size());
  EXPECT_EQ("Kermit1", rec.impus[0]);
  EXPECT_EQ("Kermit2", rec.impus[1]);
}

// Listing IMPUs where some of the rows have no columns (suggesting they have
// been deleted buy Cassandra hasn't aged out the row yet).
TEST_F(CacheRequestTest, ListImpusResultHasDeletedEntries)
{
  ListImpusRecorder rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_ListImpus();

  std::vector<cass::KeySlice> result(3);
  result[0].key = "Kermit";
  make_slice(result[0].columns, {{"ims_subscription_xml", "<xml/>"}});
  result[1].key = "Gonzo";
  // Result [1] does not have any columns.
  result[2].key = "Miss Piggy";
  make_slice(result[2].columns, {{"ims_subscription_xml", "<xml/>"}});

  EXPECT_CALL(_client, get_range_slices(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(result));
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));

  execute_trx(op, trx);

  // Only two results are returned.
  EXPECT_EQ(2, rec.impus.size());
}

// Check that the cache requests 1000 rows from Cassandra.
TEST_F(CacheRequestTest, ListImpusMaxCountSpecified)
{
  ListImpusRecorder rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_ListImpus();

  std::vector<cass::KeySlice> result;
  EXPECT_CALL(_client, get_range_slices(_, _, _, KeyRangeWithCount(1000), _))
    .WillOnce(SetArgReferee<0>(result));
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));

  execute_trx(op, trx);
}
