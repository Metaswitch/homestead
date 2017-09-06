/**
 * @file hsprov_store_test.cpp UT for HsProvStore
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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

#include <hsprov_store.h>

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
const std::deque<std::string> NO_CFS = {};
const std::deque<std::string> CCF = {"ccf"};
const std::deque<std::string> CCFS = {"ccf1", "ccf2"};
const std::deque<std::string> ECF = {"ecf"};
const std::deque<std::string> ECFS = {"ecf1", "ecf2"};
const ChargingAddresses NO_CHARGING_ADDRS(NO_CFS, NO_CFS);
const ChargingAddresses FULL_CHARGING_ADDRS(CCFS, ECFS);
const ChargingAddresses CCFS_CHARGING_ADDRS(CCFS, ECF);
const ChargingAddresses ECFS_CHARGING_ADDRS(CCF, ECFS);
const std::vector<std::string> REG_DATA_COLUMNS = {
  "ims_subscription_xml",
  "primary_ccf",
  "secondary_ccf",
  "primary_ecf",
  "secondary_ecf"
};

MATCHER_P(OperationHasResult, expected_rc, "")
{
  CassandraStore::ResultCode actual_rc = arg->get_result_code();
  return (expected_rc == actual_rc);
}

// The class under test.
//
// We don't test the HsProvStore class directly as we need to use a
// MockCassandraConnectionPool that we can use to return MockCassandraClients.
// However all other methods are the real ones from HsProvStore.
class TestHsProvStore : public HsProvStore
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
class HsProvStoreInitializationTest : public ::testing::Test
{
public:
  HsProvStoreInitializationTest()
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

  virtual ~HsProvStoreInitializationTest()
  {
    _cache.stop();
    _cache.wait_stopped();
    delete _cm; _cm = NULL;
    delete _am; _am = NULL;
    delete _iter; _iter = NULL;
  }

  TestHsProvStore _cache;
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
    Utils::parse_ip_target(address, ai.address);
    ai.port = 1;
    ai.transport = IPPROTO_TCP;
    return ai;
  }
};


// Fixture for tests that make requests to the cache (but are not interested in
// testing initialization).
class HsProvStoreRequestTest : public HsProvStoreInitializationTest
{
public:
  HsProvStoreRequestTest() : HsProvStoreInitializationTest()
  {
    sem_init(&_sem, 0, 0);

    // success() will be called in almost every test, and most of the time we're
    // not testing it explicitly so we just want to remove the test warnings.
    // For those test cases where we care about success() being called, we will
    // override this EXPECT_CALL
    EXPECT_CALL(_resolver, success(_)).Times(testing::AnyNumber());

    _cache.start();
  }

  virtual ~HsProvStoreRequestTest() {}

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

class HsProvStoreLatencyTest : public HsProvStoreRequestTest
{
public:
  HsProvStoreLatencyTest() : HsProvStoreRequestTest()
  {
    cwtest_completely_control_time();
  }

  virtual ~HsProvStoreLatencyTest() { cwtest_reset_time(); }

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

TEST_F(HsProvStoreInitializationTest, Mainline)
{
  EXPECT_CALL(_client, connect()).Times(1);
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::OK, rc);
  rc = _cache.start();
  EXPECT_EQ(CassandraStore::OK, rc);
}


TEST_F(HsProvStoreInitializationTest, OneTransportException)
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


TEST_F(HsProvStoreInitializationTest, TwoTransportExceptions)
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


TEST_F(HsProvStoreInitializationTest, NotFoundException)
{
  cass::NotFoundException nfe;
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(nfe));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::NOT_FOUND, rc);
}


TEST_F(HsProvStoreInitializationTest, RowNotFoundException)
{
  CassandraStore::RowNotFoundException rnfe("muppets", "kermit");
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(rnfe));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::NOT_FOUND, rc);
}


TEST_F(HsProvStoreInitializationTest, UnavailableException)
{
  cass::UnavailableException ue;
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(ue));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::UNAVAILABLE, rc);
}


TEST_F(HsProvStoreInitializationTest, UnknownException)
{
  std::string ex("Made up exception");
  EXPECT_CALL(_client, set_keyspace(_)).Times(1).WillOnce(Throw(ex));

  // We expect the _resolver's success() method to be called because it is only
  // tracking connectivity (and a NotFoundException is not a connection error)
  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::UNKNOWN_ERROR, rc);
}


TEST_F(HsProvStoreInitializationTest, Connection)
{
  // If is_connected() returns true, connect() should not be called
  EXPECT_CALL(_client, is_connected()).WillOnce(Return(true));
  EXPECT_CALL(_client, connect()).Times(0);

  EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

  CassandraStore::ResultCode rc = _cache.connection_test();
  EXPECT_EQ(CassandraStore::OK, rc);
}


TEST_F(HsProvStoreRequestTest, GetRegDataMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["primary_ccf"] = "ccf1";
  columns["secondary_ccf"] = "ccf2";
  columns["primary_ecf"] = "ecf1";
  columns["secondary_ecf"] = "ecf2";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(REG_DATA_COLUMNS),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(CCFS, rec.result.charging_addrs.ccfs);
  EXPECT_EQ(ECFS, rec.result.charging_addrs.ecfs);
}


TEST_F(HsProvStoreRequestTest, GetRegDataUnregistered)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  // Test with a TTL of 3600
  make_slice(slice, columns, 3600);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(REG_DATA_COLUMNS),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);
}

// If we have User-Data XML, but no explicit registration state, that should
// still be treated as unregistered state.

TEST_F(HsProvStoreRequestTest, GetRegDataNoRegState)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(REG_DATA_COLUMNS),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);

}

// Invalid registration state is treated as NOT_REGISTERED

TEST_F(HsProvStoreRequestTest, GetRegDataInvalidRegState)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(REG_DATA_COLUMNS),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("", rec.result.xml);
}


TEST_F(HsProvStoreRequestTest, GetRegDataNotFound)
{
  CassandraStore::Operation* op =
    _cache.create_GetRegData("kermit");
  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);

  EXPECT_CALL(_client, get_slice(_, "kermit", _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}

TEST_F(HsProvStoreRequestTest, GetAuthVectorAllColsReturned)
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

  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
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


TEST_F(HsProvStoreRequestTest, GetAuthVectorNonDefaultableColsReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
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


TEST_F(HsProvStoreRequestTest, GetAuthVectorHa1NotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(HsProvStoreRequestTest, GetAuthVectorNoColsReturned)
{
  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(HsProvStoreRequestTest, GetAuthVectorPublicIdRequested)
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

  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
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


TEST_F(HsProvStoreRequestTest, GetAuthVectorPublicIdRequestedNotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetAuthVector("kermit", "gonzo");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(HsProvStoreRequestTest, HaGetMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(REG_DATA_COLUMNS),
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);
}


TEST_F(HsProvStoreRequestTest, HaGet2ndReadNotFoundException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::TWO))
    .WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(OperationHasResult(CassandraStore::NOT_FOUND)));
  execute_trx(op, trx);
}


TEST_F(HsProvStoreRequestTest, HaGet2ndReadUnavailableException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
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


TEST_F(HsProvStoreRequestTest, HaGet2ndReadTimedUutException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
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

TEST_F(HsProvStoreRequestTest, HaGetRetryUsesConsistencyOne)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  // We should ask for 2 clients from the pool, because a timeout is retried
  EXPECT_CALL(*_pool, get_client()).Times(2).WillRepeatedly(Return(&_client));

  {
    testing::InSequence s;

    cass::TimedOutException te;

    // The first attempt is consistency level TWO, and that throws a
    // TimedOutException
    EXPECT_CALL(_client, get_slice(_,
                                   "kermit",
                                   ColumnPathForTable("impu"),
                                   SpecificColumns(REG_DATA_COLUMNS),
                                   cass::ConsistencyLevel::TWO))
      .WillOnce(Throw(te)).RetiresOnSaturation();

    // The next atempt is consistency level ONE to ths same node. That also
    // throws a TimedOutException
    EXPECT_CALL(_client, get_slice(_,
                                   "kermit",
                                   ColumnPathForTable("impu"),
                                   SpecificColumns(REG_DATA_COLUMNS),
                                   cass::ConsistencyLevel::ONE))
      .WillOnce(Throw(te)).RetiresOnSaturation();
    EXPECT_CALL(_resolver, success(_targets[0])).Times(1);

    // Now, we expect the operation to be tried on the second target, with level
    // ONE straight away. This succeeds
    EXPECT_CALL(_client, get_slice(_,
                                   "kermit",
                                   ColumnPathForTable("impu"),
                                   SpecificColumns(REG_DATA_COLUMNS),
                                   cass::ConsistencyLevel::ONE))
      .WillOnce(SetArgReferee<0>(slice));
    EXPECT_CALL(_resolver, success(_targets[1])).Times(1);

    EXPECT_CALL(*trx, on_success(_))
      .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  }

  execute_trx(op, trx);

  EXPECT_EQ("<howdy>", rec.result.xml);
}


TEST(HsProvStoreGenerateTimestamp, CreatesMicroTimestamp)
{
  struct timespec ts;
  int rc;

  // Get the current time and check that generate_timestamp gives the same value
  // in microseconds (to with 100ms grace).
  rc = clock_gettime(CLOCK_REALTIME, &ts);
  ASSERT_EQ(0, rc);

  int64_t grace = 100000;
  int64_t us_curr = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  EXPECT_THAT(HsProvStore::generate_timestamp(),
              AllOf(Gt(us_curr - grace), Lt(us_curr + grace)));
}


ACTION_P(AdvanceTimeMs, ms) { cwtest_advance_time_ms(ms); }
ACTION_P2(CheckLatency, trx, ms) { trx->check_latency(ms * 1000); }


TEST_F(HsProvStoreLatencyTest, GetRecordsLatency)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
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

TEST_F(HsProvStoreLatencyTest, GetErrorRecordsLatency)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<HsProvStore::GetRegData, HsProvStore::GetRegData::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  CassandraStore::Operation* op = _cache.create_GetRegData("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(DoAll(AdvanceTimeMs(12), Throw(nfe)));

  EXPECT_CALL(*trx, on_failure(_)).WillOnce(CheckLatency(trx, 12));

  execute_trx(op, trx);
}

