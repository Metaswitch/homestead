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
using ::testing::InvokeWithoutArgs;

class MockClient : public Cache::CacheClientInterface
{
public:
  MOCK_METHOD1(set_keyspace, void(const std::string& keyspace));
  MOCK_METHOD2(batch_mutate, void(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > > & mutation_map, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD5(get_slice, void(std::vector<cass::ColumnOrSuperColumn> & _return, const std::string& key, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD4(remove, void(const std::string& key, const cass::ColumnPath& column_path, const int64_t timestamp, const cass::ConsistencyLevel::type consistency_level));
};

class ResultRecorderInterface
{
public:
  virtual void save(Cache::Request* req) = 0;
};

template<class R, class T>
class ResultRecorder : public ResultRecorderInterface
{
public:
  void save(Cache::Request* req)
  {
    dynamic_cast<R*>(req)->get_result(result);
  }

  T result;
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

  virtual ~TestTransaction()
  {
    sem_post(_sem);
  }

  MOCK_METHOD0(on_success, void());
  MOCK_METHOD2(on_failure, void(Cache::ResultCode error, std::string& text));

private:
  sem_t* _sem;
};

class RecordingTransaction : public TestTransaction
{
public:
  RecordingTransaction(Cache::Request* req,
                       sem_t* sem,
                       ResultRecorderInterface* recorder) :
    TestTransaction(req, sem),
    _recorder(recorder)
  {}

  virtual ~RecordingTransaction() {}

  void record_result()
  {
    _recorder->save(_req);
  }

private:
  ResultRecorderInterface* _recorder;
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

    EXPECT_CALL(_cache, get_client()).WillRepeatedly(Return(&_client));
    EXPECT_CALL(_cache, release_client()).WillRepeatedly(Return());
    _cache.start();
  }

  virtual ~CacheRequestTest() {}

  TestTransaction* make_trx(Cache::Request* req)
  {
    return new TestTransaction(req, &_sem);
  }

  RecordingTransaction* make_rec_trx(Cache::Request* req,
                                     ResultRecorderInterface *recorder)
  {
    return new RecordingTransaction(req, &_sem, recorder);
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

  void do_successful_trx(TestTransaction* trx)
  {
    EXPECT_CALL(*trx, on_success());
    _cache.send(trx);
    wait();
  }

  sem_t _sem;
};

typedef std::map<std::string, std::map<std::string, std::vector<org::apache::cassandra::Mutation>>> mutmap_t;

class MutationMapMatcher : public MatcherInterface<const mutmap_t&> {
public:
  MutationMapMatcher(const std::string& table,
                     const std::vector<std::string>& rows,
                     const std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl = 0) :
    _table(table),
    _rows(rows),
    _columns(columns),
    _timestamp(timestamp),
    _ttl(ttl)
  {}

  virtual bool MatchAndExplain(const mutmap_t& argument,
                               MatchResultListener* listener) const
  {
    // The mutation map passed to batch mutate is of the form:
    // { row: { table : [ Mutation ] } }.

    // First check we have the right number of rows.
    if (argument.size() != _rows.size())
    {
      *listener << "Expected " << _rows.size() << " rows, got " << argument.size();
      return false;
    }

    for(std::vector<std::string>::const_iterator row = _rows.begin();
        row != _rows.end();
        ++row)
    {
      mutmap_t::const_iterator row_mutation = argument.find(*row);
      if (row_mutation == argument.end())
      {
        *listener << "Row " << *row << " expected but not present";
        return false;
      }

      if (row_mutation->second.size() != 1)
      {
        *listener << "Multiple tables are being mutated (only one is expected)";
        return false;
      }

      const std::string& table = row_mutation->second.begin()->first;
      const std::vector<org::apache::cassandra::Mutation>& mut_vector =
        row_mutation->second.begin()->second;

      if (table != _table)
      {
        *listener << "Mutation modifies table " << table << " (expected " << _table << ")";
        return false;
      }

      if (mut_vector.size() != _columns.size())
      {
        *listener << "Expected " << _columns.size() << " columns, got "
                  << mut_vector.size() << " for " << _table << ":" << *row;
        return false;
      }

      for(std::vector<org::apache::cassandra::Mutation>::const_iterator mutation = mut_vector.begin();
          mutation != mut_vector.end();
          ++mutation)
      {
        if (!mutation->__isset.column_or_supercolumn ||
            mutation->__isset.deletion ||
            !mutation->column_or_supercolumn.__isset.column ||
            mutation->column_or_supercolumn.__isset.super_column ||
            mutation->column_or_supercolumn.__isset.counter_column ||
            mutation->column_or_supercolumn.__isset.counter_super_column)
        {
          *listener << " got a mutation that is not a single column change";
          return false;
        }

        const org::apache::cassandra::Column& column = mutation->column_or_supercolumn.column;
        const std::string curr_mutation_str = _table + ":" + *row + ":" + column.name;

        if (_columns.find(column.name) == _columns.end())
        {
          *listener << "Unexpected mutation for column " << curr_mutation_str;
          return false;
        }

        const std::string& expected_val = _columns.find(column.name)->second;

        if (!column.__isset.value)
        {
          *listener << curr_mutation_str << " value is not set";
          return false;
        }

        if (column.value != expected_val)
        {
          *listener << "Column " << curr_mutation_str
                    << " has wrong value (got " << column.value
                    << " , expected " << expected_val << ")";
          return false;
        }

        if (!column.__isset.timestamp)
        {
          *listener << curr_mutation_str << " timestamp is not set";
          return false;
        }

        if (_ttl != 0)
        {
          if (!column.__isset.ttl)
          {
            *listener << curr_mutation_str << " ttl is not set";
            return false;
          }

          if (column.ttl != _ttl)
          {
            *listener << curr_mutation_str << " has wrong ttl (expected "
              << _ttl << " got " << column.ttl << ")";
            return false;
          }
        }
        else
        {
          if (column.__isset.ttl)
          {
            *listener << curr_mutation_str << " ttl is set when it shouldn't be";
            return false;
          }
        }
      }
    }

    return true;
  }

  virtual void DescribeTo(::std::ostream* os) const
  {
    *os << "to write columns " << PrintToString(_columns) <<
           " to rows " << PrintToString(_rows) <<
           " in table " << _table;
  }

private:
  std::string _table;
  std::vector<std::string> _rows;
  std::map<std::string, std::string> _columns;
  int64_t _timestamp;
  int32_t _ttl;
};

inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::string& row,
            const std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl = 0)
{
  std::vector<std::string> rows(1, row);
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp, ttl));
}

inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::vector<std::string>& rows,
            const std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl = 0)
{
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp, ttl));
}



TEST_F(CacheRequestTest, PutIMSSubscriptionMainline)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000, 300));

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impu", "kermit", columns, 1000, 300), _));

  do_successful_trx(trx);
}


TEST_F(CacheRequestTest, NoTTLOnPut)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client, batch_mutate(MutationMap("impu", "kermit", columns, 1000), _));

  do_successful_trx(trx);
}


TEST_F(CacheRequestTest, PutIMSSubMultipleIDs)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription(ids, "<xml>", 1000));

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client, batch_mutate(MutationMap("impu", ids, columns, 1000), _));

  do_successful_trx(trx);
}


TEST_F(CacheRequestTest, PutTransportEx)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(te));

  EXPECT_CALL(*trx, on_failure(Cache::ResultCode::CONNECTION_ERROR, _));
  _cache.send(trx);
  wait();
}

TEST_F(CacheRequestTest, PutInvalidRequestException)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  org::apache::cassandra::InvalidRequestException ire;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));

  EXPECT_CALL(*trx, on_failure(Cache::ResultCode::INVALID_REQUEST, _));
  _cache.send(trx);
  wait();
}


TEST_F(CacheRequestTest, PutNotFoundException)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  org::apache::cassandra::NotFoundException nfe;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(Cache::ResultCode::NOT_FOUND, _));
  _cache.send(trx);
  wait();
}


TEST_F(CacheRequestTest, PutRowNotFoundException)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  Cache::RowNotFoundException rnfe("muppets", "kermit");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(rnfe));

  EXPECT_CALL(*trx, on_failure(Cache::ResultCode::NOT_FOUND, _));
  _cache.send(trx);
  wait();
}


TEST_F(CacheRequestTest, PutUnknownException)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000));

  std::string ex("Made up exception");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ex));

  EXPECT_CALL(*trx, on_failure(Cache::ResultCode::UNKNOWN_ERROR, _));
  _cache.send(trx);
  wait();
}

TEST_F(CacheRequestTest, PutsHaveConsistencyLevelOne)
{
  TestTransaction *trx = make_trx(
    new Cache::PutIMSSubscription("kermit", "<xml>", 1000, 300));

  EXPECT_CALL(_client, batch_mutate(_, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx);
}


TEST_F(CacheRequestTest, PutAuthVectorMainline)
{
  DigestAuthVector av;
  av.ha1 = "somehash";
  av.realm = "themuppetshow.com";
  av.qop = "auth";
  av.preferred = true;

  TestTransaction *trx = make_trx(
    new Cache::PutAuthVector("gonzo", av, 1000));

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = av.ha1;
  columns["digest_realm"] = av.realm;
  columns["digest_qop"] = av.qop;
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));

  do_successful_trx(trx);
}


TEST_F(CacheRequestTest, PutAsoocPublicIdMainline)
{
  TestTransaction *trx = make_trx(
    new Cache::PutAssociatedPublicID("gonzo", "kermit", 1000));

  std::map<std::string, std::string> columns;
  columns["public_id_kermit"] = "";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));

  do_successful_trx(trx);
}

MATCHER_P(ColumnPathForTable, table, std::string("Refers to table: ")+table)
{
  *result_listener << "Refers to table: " << arg.column_family;
  return (arg.column_family == table);
}

TEST_F(CacheRequestTest, DeletePublicId)
{
  TestTransaction *trx = make_trx(
    new Cache::DeletePublicIDs("kermit", 1000));

  EXPECT_CALL(_client,
              remove("kermit", ColumnPathForTable("impu"), 1000, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx);
}

TEST_F(CacheRequestTest, DeleteMultiPublicIds)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx(
    new Cache::DeletePublicIDs(ids, 1000));

  EXPECT_CALL(_client, remove("kermit", ColumnPathForTable("impu"), _, _));
  EXPECT_CALL(_client, remove("gonzo", ColumnPathForTable("impu"), _, _));
  EXPECT_CALL(_client, remove("miss piggy", ColumnPathForTable("impu"), _, _));

  do_successful_trx(trx);
}

TEST_F(CacheRequestTest, DeletePrivateId)
{
  TestTransaction *trx = make_trx(
    new Cache::DeletePrivateIDs("kermit", 1000));

  EXPECT_CALL(_client,
              remove("kermit", ColumnPathForTable("impi"), 1000, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx);
}

TEST_F(CacheRequestTest, DeleteMultiPrivateIds)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  TestTransaction *trx = make_trx(
    new Cache::DeletePrivateIDs(ids, 1000));

  EXPECT_CALL(_client, remove("kermit", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("gonzo", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("miss piggy", ColumnPathForTable("impi"), _, _));

  do_successful_trx(trx);
}

TEST_F(CacheRequestTest, DeletesHaveConsistencyLevelOne)
{
  TestTransaction *trx = make_trx(
    new Cache::DeletePublicIDs("kermit", 1000));

  EXPECT_CALL(_client, remove(_, _, _, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx);
}

const std::vector<cass::ColumnOrSuperColumn> empty_slice(0);

MATCHER_P(SpecificColumns,
          columns,
          std::string("requests columns: ")+PrintToString(columns))
{
  if (!arg.__isset.column_names || arg.__isset.slice_range)
  {
    *result_listener << "does not request specific columns"; return false;
  }

  std::vector<std::string> expected_columns = columns;
  std::vector<std::string> actual_columns = arg.column_names;

  std::sort(expected_columns.begin(), expected_columns.end());
  std::sort(actual_columns.begin(), actual_columns.end());

  if (expected_columns != actual_columns)
  {
    *result_listener << "requests columns " << PrintToString(actual_columns);
    return false;
  }

  return true;
}



TEST_F(CacheRequestTest, GetIMSSubscriptionMainline)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");


  cass::Column col;
  col.__set_name("ims_subscription_xml");
  col.__set_value("<howdy>");

  cass::ColumnOrSuperColumn csc;
  csc.__set_column(col);

  std::vector<cass::ColumnOrSuperColumn> slice(1, csc);

  ResultRecorder<Cache::GetIMSSubscription, std::string> rec;
  RecordingTransaction* trx = make_rec_trx(new Cache::GetIMSSubscription("kermit"),
                                           &rec);

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 SpecificColumns(requested_columns),
                                 _))

    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success())
    .WillOnce(InvokeWithoutArgs(trx, &RecordingTransaction::record_result));
  _cache.send(trx);
  wait();

  EXPECT_EQ("<howdy>", rec.result);
}
