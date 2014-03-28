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
#include "fakelogger.hpp"

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

//
// TEST HARNESS CODE.
//

// Mock cassandra client that emulates the interface tot he C++ thrift bindings.
class MockClient : public Cache::CacheClientInterface
{
public:
  MOCK_METHOD1(set_keyspace, void(const std::string& keyspace));
  MOCK_METHOD2(batch_mutate, void(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > > & mutation_map, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD5(get_slice, void(std::vector<cass::ColumnOrSuperColumn> & _return, const std::string& key, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD5(multiget_slice, void(std::map<std::string, std::vector<cass::ColumnOrSuperColumn> > & _return, const std::vector<std::string>& keys, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level));
  MOCK_METHOD4(remove, void(const std::string& key, const cass::ColumnPath& column_path, const int64_t timestamp, const cass::ConsistencyLevel::type consistency_level));
};

const std::vector<std::string> IMPIS = {"somebody@example.com"};
const std::vector<std::string> EMPTY_IMPIS;


// The class under test.
//
// We don't test the Cache class directly as we need to override the get_client
// and release_client methods to use MockClient.  However all other methods are
// the real ones from Cache.
class TestCache : public Cache
{
public:
  MOCK_METHOD0(get_client, Cache::CacheClientInterface*());
  MOCK_METHOD0(release_client, void());
};


// Transaction object used by the testbed. This mocks the on_success and
// on_failure methods to allow testcases to control it's behaviour.
//
// The transaction is destroyed by the Cache on one of it's worker threads.
// When destroyed, this object posts to a semaphore which signals the main
// thread to continue executing the testcase.
class CacheTestTransaction : public Cache::Transaction
{
public:
  CacheTestTransaction(sem_t* sem) :
    Cache::Transaction(), _sem(sem)
  {}

  virtual ~CacheTestTransaction()
  {
    sem_post(_sem);
  }

  void check_latency(unsigned long expected_latency_us)
  {
    unsigned long actual_latency_us;
    bool rc;

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);

    cwtest_advance_time_ms(1);

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);
  }

  MOCK_METHOD1(on_success, void(Cache::Request*req));
  MOCK_METHOD3(on_failure, void(Cache::Request* req,
                                Cache::ResultCode error,
                                std::string& text));

private:
  sem_t* _sem;
};


// A class (and interface) that records the result of a cache request.
//
// In the template:
// -  R is the request class.
// -  T is the type of data returned by get_request().
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


// A specialized transaction that can be configured to record the result of a
// request on a recorder object.
class RecordingTransaction : public CacheTestTransaction
{
public:
  RecordingTransaction(sem_t* sem,
                       ResultRecorderInterface* recorder) :
    CacheTestTransaction(sem),
    _recorder(recorder)
  {}

  virtual ~RecordingTransaction() {}

  void record_result(Cache::Request* req)
  {
    _recorder->save(req);
  }

private:
  ResultRecorderInterface* _recorder;
};


// Fixture for tests that cover cache initialization processing.
//
// In reality only the start() method is interesting, so the fixture handles
// calling initialize() and configure()
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


// Fixture for tests that make requests to the cache (but are not interested in
// testing initialization).
class CacheRequestTest : public CacheInitializationTest
{
public:
  CacheRequestTest() : CacheInitializationTest()
  {
    sem_init(&_sem, 0, 0);

    // By default the cache just serves up the mock client each time.
    EXPECT_CALL(_cache, get_client()).WillRepeatedly(Return(&_client));
    EXPECT_CALL(_cache, release_client()).WillRepeatedly(Return());

    _cache.start();
  }

  virtual ~CacheRequestTest() {}

  // Helper methods to make a CacheTestTransaction or RecordingTransation. This
  // passes the semaphore into the transaction constructor - this is posted to
  // when the transaction completes.
  CacheTestTransaction* make_trx()
  {
    return new CacheTestTransaction(&_sem);
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
    ts.tv_sec += 1;
    rc = sem_timedwait(&_sem, &ts);
    ASSERT_EQ(0, rc);
  }

  // Helper method to send a transaction and wait for it to succeed.
  void do_successful_trx(CacheTestTransaction* trx, Cache::Request* req)
  {
    EXPECT_CALL(*trx, on_success(_));
    _cache.send(trx, req);
    wait();
  }

  // Semaphore that the main thread waits on while a transaction is outstanding.
  sem_t _sem;

  FakeLogger _log;
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
// TYPE DEFINITIONS AND CONSTANTS
//

// A mutation map as used in batch_mutate(). This is of the form:
// { row: { table : [ Mutation ] } }.
typedef std::map<std::string, std::map<std::string, std::vector<cass::Mutation>>> mutmap_t;

// A slice as returned by get_slice().
typedef std::vector<cass::ColumnOrSuperColumn> slice_t;

const slice_t empty_slice(0);

typedef std::map<std::string, std::vector<cass::ColumnOrSuperColumn>> multiget_slice_t;

const multiget_slice_t empty_slice_multiget;

// utlity functions to make a slice from a map of column names => values.
void make_slice(slice_t& slice,
                std::map<std::string, std::string>& columns,
                int32_t ttl = 0)
{
  for(std::map<std::string, std::string>::const_iterator it = columns.begin();
      it != columns.end();
      ++it)
  {
    cass::Column c;
    c.__set_name(it->first);
    c.__set_value(it->second);
    if (ttl != 0)
    {
      c.__set_ttl(ttl);
    }

    cass::ColumnOrSuperColumn csc;
    csc.__set_column(c);

    slice.push_back(csc);
  }
}


//
// MATCHERS
//

// A class that matches against a supplied mutation map.
class MultipleCfMutationMapMatcher : public MatcherInterface<const mutmap_t&> {
public:
  MultipleCfMutationMapMatcher(const std::vector<CFRowColumnValue>& expected):
    _expected(expected)
  {
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _expected.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _expected.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<CFRowColumnValue>::const_iterator expected = _expected.begin();
        expected != _expected.end();
        ++expected)
    {
      std::string row = expected->row;
      std::map<std::string, std::string> expected_columns = expected->columns;
      mutmap_t::const_iterator row_mut = mutmap.find(row);

      if (row_mut == mutmap.end())
      {
        *listener << row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = row + ":" + table;

      // Check we're modifying the right table.
      if (table != expected->cf)
      {
        *listener << "wrong table for " << row
                  << "(expected " << expected->cf
                  << ", got " << table << ")";
        return false;
      }

      // Check we've modifying the right number of columns for this row/table.
      if (row_table_mut.size() != expected_columns.size())
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected " << expected_columns.size()
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      for(std::vector<cass::Mutation>::const_iterator mutation = row_table_mut.begin();
          mutation != row_table_mut.end();
          ++mutation)
      {
        // We only allow mutations for a single column (not supercolumns,
        // counters, etc).
        if (!mutation->__isset.column_or_supercolumn ||
            mutation->__isset.deletion ||
            !mutation->column_or_supercolumn.__isset.column ||
            mutation->column_or_supercolumn.__isset.super_column ||
            mutation->column_or_supercolumn.__isset.counter_column ||
            mutation->column_or_supercolumn.__isset.counter_super_column)
        {
          *listener << row_table_name << " has a mutation that isn't a single column change";
          return false;
        }

        // By now we know we're dealing with a column mutation, so extract the
        // column itself and build a descriptive name.
        const cass::Column& column = mutation->column_or_supercolumn.column;
        const std::string row_table_column_name =
                                             row_table_name + ":" + column.name;

        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (expected_columns.find(column.name) == expected_columns.end())
        {
          *listener << "unexpected mutation " << row_table_column_name;
          return false;
        }

        const std::string& expected_value = expected_columns.find(column.name)->second;

        // Check it specifies the correct value.
        if (!column.__isset.value)
        {
          *listener << row_table_column_name << " does not have a value";
          return false;
        }

        if (column.value != expected_value)
        {
          *listener << row_table_column_name
                    << " has wrong value (expected " << expected_value
                    << " , got " << column.value << ")";
          return false;
        }
      }
    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
  }

private:
  std::vector<CFRowColumnValue> _expected;
};

// A class that matches against a supplied mutation map.
class BatchDeletionMatcher : public MatcherInterface<const mutmap_t&> {
public:
  BatchDeletionMatcher(const std::vector<CFRowColumnValue>& expected):
    _expected(expected)
  {
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _expected.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _expected.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<CFRowColumnValue>::const_iterator expected = _expected.begin();
        expected != _expected.end();
        ++expected)
    {
      std::string row = expected->row;
      std::map<std::string, std::string> expected_columns = expected->columns;
      mutmap_t::const_iterator row_mut = mutmap.find(row);

      if (row_mut == mutmap.end())
      {
        *listener << row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = row + ":" + table;

      // Check we're modifying the right table.
      if (table != expected->cf)
      {
        *listener << "wrong table for " << row
                  << "(expected " << expected->cf
                  << ", got " << table << ")";
        return false;
      }

      // Deletions should only consist of one mutation per row.
      if (row_table_mut.size() != 1)
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected 1"
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      const cass::Mutation* mutation = &row_table_mut.front();
      // We only allow mutations for a single column (not supercolumns,
      // counters, etc).
      if (!mutation->__isset.deletion)
      {
        *listener << row_table_name << " has a mutation that isn't a deletion";
        return false;
      }

      const cass::SlicePredicate* deletion = &mutation->deletion.predicate;

      // Check that the number of columns to be deleted is right
      if (deletion->column_names.size() != expected_columns.size())
      {
        *listener << deletion->column_names.size() << " columns deleted, expected " << expected_columns.size();
        return false;
      }

      // Loop over the columns and check that each of them
      // is expected
      for(std::vector<std::string>::const_iterator col = deletion->column_names.begin();
          col != deletion->column_names.end();
          ++col)
      {
        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (expected_columns.find(*col) == expected_columns.end())
        {
          *listener << "unexpected mutation " << *col;
          return false;
        }
      }

    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
  }

private:
  std::vector<CFRowColumnValue> _expected;
};

// A class that matches against a supplied mutation map.
class MutationMapMatcher : public MatcherInterface<const mutmap_t&> {
public:
  MutationMapMatcher(const std::string& table,
                     const std::vector<std::string>& rows,
                     const std::map<std::string, std::pair<std::string, int32_t> >& columns,
                     int64_t timestamp,
                     int32_t ttl = 0) :
    _table(table),
    _rows(rows),
    _columns(columns),
    _timestamp(timestamp)
  {};

  MutationMapMatcher(const std::string& table,
                     const std::vector<std::string>& rows,
                     const std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl = 0) :
    _table(table),
    _rows(rows),
    _timestamp(timestamp)
  {
    for(std::map<std::string, std::string>::const_iterator column = columns.begin();
        column != columns.end();
        ++column)
    {
      _columns[column->first].first = column->second;
      _columns[column->first].second = ttl;
    }
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _rows.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _rows.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<std::string>::const_iterator row = _rows.begin();
        row != _rows.end();
        ++row)
    {
      mutmap_t::const_iterator row_mut = mutmap.find(*row);

      if (row_mut == mutmap.end())
      {
        *listener << *row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << *row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = *row + ":" + table;

      // Check we're modifying the right table.
      if (table != _table)
      {
        *listener << "wrong table for " << *row
                  << "(expected " << _table
                  << ", got " << table << ")";
        return false;
      }

      // Check we've modifying the right number of columns for this row/table.
      if (row_table_mut.size() != _columns.size())
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected " << _columns.size()
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      for(std::vector<cass::Mutation>::const_iterator mutation = row_table_mut.begin();
          mutation != row_table_mut.end();
          ++mutation)
      {
        // We only allow mutations for a single column (not supercolumns,
        // counters, etc).
        if (!mutation->__isset.column_or_supercolumn ||
            mutation->__isset.deletion ||
            !mutation->column_or_supercolumn.__isset.column ||
            mutation->column_or_supercolumn.__isset.super_column ||
            mutation->column_or_supercolumn.__isset.counter_column ||
            mutation->column_or_supercolumn.__isset.counter_super_column)
        {
          *listener << row_table_name << " has a mutation that isn't a single column change";
          return false;
        }

        // By now we know we're dealing with a column mutation, so extract the
        // column itself and build a descriptive name.
        const cass::Column& column = mutation->column_or_supercolumn.column;
        const std::string row_table_column_name =
                                             row_table_name + ":" + column.name;

        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (_columns.find(column.name) == _columns.end())
        {
          *listener << "unexpected mutation " << row_table_column_name;
          return false;
        }

        const std::string& expected_value = _columns.find(column.name)->second.first;
        const int32_t& expected_ttl = _columns.find(column.name)->second.second;

        // Check it specifies the correct value.
        if (!column.__isset.value)
        {
          *listener << row_table_column_name << " does not have a value";
          return false;
        }

        if (column.value != expected_value)
        {
          *listener << row_table_column_name
                    << " has wrong value (expected " << expected_value
                    << " , got " << column.value << ")";
          return false;
        }

        // The timestamp must be set and correct.
        if (!column.__isset.timestamp)
        {
          *listener << row_table_column_name << " timestamp is not set";
          return false;
        }

        if (column.timestamp != _timestamp)
        {
          *listener << row_table_column_name
                    << " has wrong timestamp (expected " << _timestamp
                    << ", got " << column.timestamp << ")";
        }

        if (expected_ttl != 0)
        {
          // A TTL is expected. Check the field is present and correct.
          if (!column.__isset.ttl)
          {
            *listener << row_table_column_name << " ttl is not set";
            return false;
          }

          if (column.ttl != expected_ttl)
          {
            *listener << row_table_column_name
                      << " has wrong ttl (expected " << expected_ttl <<
                      ", got " << column.ttl << ")";
            return false;
          }
        }
        else
        {
          // A TLL is not expected, so check the field is not set.
          if (column.__isset.ttl)
          {
            *listener << row_table_column_name
                      << " ttl is incorrectly set (value is " << column.ttl << ")";
            return false;
          }
        }
      }
    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
    *os << "to write columns " << PrintToString(_columns) <<
           " to rows " << PrintToString(_rows) <<
           " in table " << _table;
  }

private:
  std::string _table;
  std::vector<std::string> _rows;
  std::map<std::string, std::pair<std::string, int32_t> > _columns;
  int64_t _timestamp;
  int32_t _ttl;
};


// Utility functions for creating MutationMapMatcher objects.
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

inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::string& row,
            const std::map<std::string, std::pair<std::string, int32_t> >& columns,
            int64_t timestamp)
{
  std::vector<std::string> rows(1, row);
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp));
}

inline Matcher<const mutmap_t&>
MutationMap(const std::vector<CFRowColumnValue>& expected)
{
  return MakeMatcher(new MultipleCfMutationMapMatcher(expected));
}

inline Matcher<const mutmap_t&>
DeletionMap(const std::vector<CFRowColumnValue>& expected)
{
  return MakeMatcher(new BatchDeletionMatcher(expected));
}


// Matcher that check whether the argument is a ColumnPath that refers to a
// single table.
MATCHER_P(ColumnPathForTable, table, std::string("refers to table ")+table)
{
  *result_listener << "refers to table " << arg.column_family;
  return (arg.column_family == table);
}

// Matcher that check whether the argument is a ColumnPath that refers to a
// single table.
MATCHER_P2(ColumnPath, table, column, std::string("refers to table ")+table)
{
  *result_listener << "refers to table " << arg.column_family;
  *result_listener << "refers to column " << arg.column;
  return ((arg.column_family == table) && (arg.column == column));
}


// Matcher that checks whether a SlicePredicate specifies a sequence of specific
// columns.
MATCHER_P(SpecificColumns,
          columns,
          std::string("specifies columns ")+PrintToString(columns))
{
  if (!arg.__isset.column_names || arg.__isset.slice_range)
  {
    *result_listener << "does not specify individual columns";
    return false;
  }

  // Compare the expected and received columns (sorting them before the
  // comparison to ensure a consistent order).
  std::vector<std::string> expected_columns = columns;
  std::vector<std::string> actual_columns = arg.column_names;

  std::sort(expected_columns.begin(), expected_columns.end());
  std::sort(actual_columns.begin(), actual_columns.end());

  if (expected_columns != actual_columns)
  {
    *result_listener << "specifies columns " << PrintToString(actual_columns);
    return false;
  }

  return true;
}

// Matcher that checks whether a SlicePredicate specifies all columns
MATCHER(AllColumns,
          std::string("requests all columns: "))
{
  if (arg.__isset.column_names || !arg.__isset.slice_range)
  {
    *result_listener << "does not request a slice range"; return false;
  }

  if (arg.slice_range.start != "")
  {
    *result_listener << "has incorrect start (" << arg.slice_range.start << ")";
    return false;
  }

  if (arg.slice_range.finish != "")
  {
    *result_listener << "has incorrect finish (" << arg.slice_range.finish << ")";
    return false;
  }

  return true;
}

// Matcher that checks whether a SlicePredicate specifies all columns with a
// particular prefix.
MATCHER_P(ColumnsWithPrefix,
          prefix,
          std::string("requests columns with prefix: ")+prefix)
{
  if (arg.__isset.column_names || !arg.__isset.slice_range)
  {
    *result_listener << "does not request a slice range"; return false;
  }

  if (arg.slice_range.start != prefix)
  {
    *result_listener << "has incorrect start (" << arg.slice_range.start << ")";
    return false;
  }

  // Calculate what the end of the range should be (the last byte should be
  // one more than the start - we don't handle wrapping since homestead-ng
  // doesn't supply names with non-ASCII characters).
  std::string end_str = prefix;
  char last_char = *end_str.rbegin();
  last_char++;
  end_str = end_str.substr(0, end_str.length()-1) + std::string(1, last_char);

  if (arg.slice_range.finish != end_str)
  {
    *result_listener << "has incorrect finish (" << arg.slice_range.finish << ")";
    return false;
  }

  return true;
}


//
// TESTS
//

TEST_F(CacheInitializationTest, Mainline)
{
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Return(&_client));
  EXPECT_CALL(_cache, release_client()).Times(1);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::OK, rc);
}


TEST_F(CacheInitializationTest, TransportException)
{
  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(te));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::CONNECTION_ERROR, rc);
}


TEST_F(CacheInitializationTest, NotFoundException)
{
  cass::NotFoundException nfe;
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(nfe));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::NOT_FOUND, rc);
}


TEST_F(CacheInitializationTest, UnknownException)
{
  Cache::NoResultsException rnfe("muppets", "kermit");
  EXPECT_CALL(_cache, get_client()).Times(1).WillOnce(Throw(rnfe));
  EXPECT_CALL(_cache, release_client()).Times(0);

  Cache::ResultCode rc = _cache.start();
  EXPECT_EQ(Cache::UNKNOWN_ERROR, rc);
}


TEST_F(CacheRequestTest, PutIMSSubscriptionMainline)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000, 300);

  std::vector<CFRowColumnValue> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["ims_subscription_xml"] = "<xml>";
  impu_columns["is_registered"] = "\x01";
  impu_columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CFRowColumnValue("impu", "kermit", impu_columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, PutIMSSubscriptionUnregistered)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::UNREGISTERED, IMPIS, 1000, 300);

  std::vector<CFRowColumnValue> expected;

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = std::string("\x00", 1);
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CFRowColumnValue("impu", "kermit", columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, PutIMSSubscriptionUnchanged)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::UNCHANGED, EMPTY_IMPIS, 1000, 300);

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client,
              batch_mutate(
                MutationMap("impu", "kermit", columns, 1000, 300), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, NoTTLOnPut)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  std::vector<CFRowColumnValue> expected;

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = "\x01";
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CFRowColumnValue("impu", "kermit", columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, PutIMSSubMultipleIDs)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("miss piggy");

  std::vector<CFRowColumnValue> expected;

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription(ids, "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";
  columns["is_registered"] = "\x01";
  columns["associated_impi__somebody@example.com"] = "";

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  expected.push_back(CFRowColumnValue("impu", "kermit", columns));
  expected.push_back(CFRowColumnValue("impu", "miss piggy", columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, PutTransportEx)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  apache::thrift::transport::TTransportException te;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(te));

  EXPECT_CALL(*trx, on_failure(_, Cache::CONNECTION_ERROR, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, PutInvalidRequestException)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  cass::InvalidRequestException ire;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));

  EXPECT_CALL(*trx, on_failure(_, Cache::INVALID_REQUEST, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, PutNotFoundException)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, PutNoResultsException)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  Cache::NoResultsException rnfe("muppets", "kermit");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(rnfe));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, PutUnknownException)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  std::string ex("Made up exception");
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ex));

  EXPECT_CALL(*trx, on_failure(_, Cache::UNKNOWN_ERROR, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, PutsHaveConsistencyLevelOne)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000);

  EXPECT_CALL(_client, batch_mutate(_, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, PutAuthVectorMainline)
{
  DigestAuthVector av;
  av.ha1 = "somehash";
  av.realm = "themuppetshow.com";
  av.qop = "auth";
  av.preferred = true;

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutAuthVector("gonzo", av, 1000);

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = av.ha1;
  columns["digest_realm"] = av.realm;
  columns["digest_qop"] = av.qop;
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, PutAsoocPublicIdMainline)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutAssociatedPublicID("gonzo", "kermit", 1000);

  std::map<std::string, std::string> columns;
  columns["public_id_kermit"] = "";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000), _));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, PutAssocPublicIdTTL)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutAssociatedPublicID("gonzo", "kermit", 1000, 300);

  std::map<std::string, std::string> columns;
  columns["public_id_kermit"] = "";

  EXPECT_CALL(_client,
              batch_mutate(MutationMap("impi", "gonzo", columns, 1000, 300), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, PutAssocPrivateIdMainline)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutAssociatedPrivateID({"kermit", "miss piggy"}, "gonzo", 1000);

  std::vector<CFRowColumnValue> expected;

  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__gonzo"] = "";

  expected.push_back(CFRowColumnValue("impu", "kermit", impu_columns));
  expected.push_back(CFRowColumnValue("impu", "miss piggy", impu_columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "gonzo", impi_columns));

  EXPECT_CALL(_client,
              batch_mutate(MutationMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, DeletePublicId)
{
  std::vector<CFRowColumnValue> expected;

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  // The "kermit" IMPU row should be deleted entirely
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));

  // The "kermit" column should be deleted from the IMPI's row in the
  // IMPI mapping table
  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", deleted_impi_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, DeleteMultiPublicIds)
{
  std::vector<CFRowColumnValue> expected;

  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePublicIDs(ids, IMPIS, 1000);

  // The "kermit", "gonzo" and "miss piggy" IMPU rows should be deleted entirely
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));
  EXPECT_CALL(_client, remove("gonzo", _, 1000, _));
  EXPECT_CALL(_client, remove("miss piggy", _, 1000, _));

  // Only the "kermit" column should be deleted from the IMPI's row in the
  // IMPI mapping table
  std::map<std::string, std::string> deleted_impi_columns;
  deleted_impi_columns["associated_primary_impu__kermit"] = "";
  expected.push_back(CFRowColumnValue("impi_mapping", "somebody@example.com", deleted_impi_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, DeletePrivateId)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePrivateIDs("kermit", 1000);

  EXPECT_CALL(_client,
              remove("kermit",
                     ColumnPathForTable("impi"),
                     1000,
                     cass::ConsistencyLevel::ONE));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, DeleteMultiPrivateIds)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePrivateIDs(ids, 1000);

  EXPECT_CALL(_client, remove("kermit", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("gonzo", ColumnPathForTable("impi"), _, _));
  EXPECT_CALL(_client, remove("miss piggy", ColumnPathForTable("impi"), _, _));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, DeleteIMPIMappings)
{
  std::vector<std::string> ids;
  ids.push_back("kermit");
  ids.push_back("gonzo");
  ids.push_back("miss piggy");

  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeleteIMPIMapping(ids, 1000);

  std::vector<CFRowColumnValue> expected;
  EXPECT_CALL(_client, remove("kermit", _, 1000, _));
  EXPECT_CALL(_client, remove("gonzo", _, 1000, _));
  EXPECT_CALL(_client, remove("miss piggy", _, 1000, _));

  do_successful_trx(trx, req);
}



TEST_F(CacheRequestTest, DeletesHaveConsistencyLevelOne)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  EXPECT_CALL(_client, remove(_, _, _, cass::ConsistencyLevel::ONE));
  EXPECT_CALL(_client, batch_mutate(_, cass::ConsistencyLevel::ONE));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, GetIMSSubscriptionMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = "\x01";
  columns["associated_impi__somebody@example.com"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ(RegistrationState::REGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);
}

TEST_F(CacheRequestTest, GetIMSSubscriptionTTL)
{
  std::map<std::string, std::string> columns;
  columns["is_registered"] = "\x01";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ(RegistrationState::REGISTERED, rec.result.state);
  EXPECT_EQ("", rec.result.xml);
  EXPECT_EQ(EMPTY_IMPIS, rec.result.impis);
}

TEST_F(CacheRequestTest, GetIMSSubscriptionUnregistered)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = std::string("\x00", 1);
  columns["associated_impi__somebody@example.com"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  // Test with a TTL of 3600
  make_slice(slice, columns, 3600);

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ(RegistrationState::UNREGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);
}

// If we have User-Data XML, but no explicit registration state, that should
// still be treated as unregistered state.

TEST_F(CacheRequestTest, GetIMSSubscriptionNoRegState)
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

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ(RegistrationState::UNREGISTERED, rec.result.state);
  EXPECT_EQ("<howdy>", rec.result.xml);
  EXPECT_EQ(IMPIS, rec.result.impis);

}

// Invalid registration state is treated as NOT_REGISTERED

TEST_F(CacheRequestTest, GetIMSSubscriptionInvalidRegState)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "";
  columns["is_registered"] = "\x03";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ(RegistrationState::NOT_REGISTERED, rec.result.state);
  EXPECT_EQ("", rec.result.xml);
  EXPECT_EQ(EMPTY_IMPIS, rec.result.impis);
}


TEST_F(CacheRequestTest, GetIMSSubscriptionNotFound)
{
  Cache::Request* req =
    _cache.create_GetIMSSubscription("kermit");
  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);

  EXPECT_CALL(_client, get_slice(_, "kermit", _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

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
  requested_columns.push_back("known_preferred");

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impi"),
                                 SpecificColumns(requested_columns),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("themuppetshow.com", rec.result.realm);
  EXPECT_EQ("auth", rec.result.qop);
  EXPECT_TRUE(rec.result.preferred);
}


TEST_F(CacheRequestTest, GetAuthVectorNonDefaultableColsReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("", rec.result.realm);
  EXPECT_EQ("", rec.result.qop);
  EXPECT_FALSE(rec.result.preferred);
}


TEST_F(CacheRequestTest, GetAuthVectorHa1NotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, GetAuthVectorNoColsReturned)
{
  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAuthVector("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, GetAuthVectorPublicIdRequested)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("digest_ha1");
  requested_columns.push_back("digest_realm");
  requested_columns.push_back("digest_qop");
  requested_columns.push_back("known_preferred");
  requested_columns.push_back("public_id_gonzo");

  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.
  columns["public_id_gonzo"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetAuthVector("kermit", "gonzo");

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 _,
                                 SpecificColumns(requested_columns),
                                 _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  EXPECT_EQ("somehash", rec.result.ha1);
  EXPECT_EQ("themuppetshow.com", rec.result.realm);
  EXPECT_EQ("auth", rec.result.qop);
  EXPECT_TRUE(rec.result.preferred);
}


TEST_F(CacheRequestTest, GetAuthVectorPublicIdRequestedNotReturned)
{
  std::map<std::string, std::string> columns;
  columns["digest_ha1"] = "somehash";
  columns["digest_realm"] = "themuppetshow.com";
  columns["digest_qop"] = "auth";
  columns["known_preferred"] = "\x01"; // That's how thrift represents bools.

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAuthVector, DigestAuthVector> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetAuthVector("kermit", "gonzo");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, GetAssocPublicIDsMainline)
{
  std::map<std::string, std::string> columns;
  columns["public_id_gonzo"] = "";
  columns["public_id_miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetAssociatedPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAssociatedPublicIDs("kermit");

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impi"),
                        ColumnsWithPrefix("public_id_"),
                        _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

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
  columns["public_id_gonzo"] = "";
  columns["public_id_miss piggy"] = "";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  std::vector<std::string> private_ids;
  private_ids.push_back("kermit");
  private_ids.push_back("miss piggy");

  ResultRecorder<Cache::GetAssociatedPublicIDs, std::vector<std::string>> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetAssociatedPublicIDs(private_ids);

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impi"),
                        ColumnsWithPrefix("public_id_"),
                        _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(_client,
              get_slice(_,
              "miss piggy",
              ColumnPathForTable("impi"),
              ColumnsWithPrefix("public_id_"),
              _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

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
  Cache::Request* req = _cache.create_GetAssociatedPublicIDs("kermit");

  EXPECT_CALL(_client, get_slice(_, "kermit", _, _, _))
    .WillOnce(SetArgReferee<0>(empty_slice));

  // Expect on_success to fire, but results should be empty.
  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

  std::vector<std::string> expected_ids;

  EXPECT_EQ(expected_ids, rec.result);
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
  Cache::Request* req = _cache.create_GetAssociatedPrimaryPublicIDs("gonzo");

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
  _cache.send(trx, req);
  wait();

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
  Cache::Request* req = _cache.create_GetAssociatedPrimaryPublicIDs(impis);

  EXPECT_CALL(_client,
              multiget_slice(_,
                             impis,
                             ColumnPathForTable("impi_mapping"),
                             ColumnsWithPrefix("associated_primary_impu__"),
                             _))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

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
  Cache::Request* req = _cache.create_GetAssociatedPrimaryPublicIDs("gonzo");

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
  _cache.send(trx, req);
  wait();

  EXPECT_TRUE(rec.result.empty());
}

TEST_F(CacheRequestTest, HaGetMainline)
{
  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";
  columns["is_registered"] = "\x01";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, Cache::GetIMSSubscription::Result> rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetIMSSubscription("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 cass::ConsistencyLevel::ONE))
    .WillOnce(Throw(nfe));
  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("impu"),
                                 AllColumns(),
                                 cass::ConsistencyLevel::QUORUM))
    .WillOnce(SetArgReferee<0>(slice));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(Invoke(trx, &RecordingTransaction::record_result));
  _cache.send(trx, req);
  wait();

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

  ResultRecorder<Cache::GetIMSSubscription, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetIMSSubscription("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::ONE))
    .WillOnce(Throw(nfe));
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::QUORUM))
    .WillOnce(Throw(nfe));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
}


TEST_F(CacheRequestTest, HaGet2ndReadUnavailableException)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request* req = _cache.create_GetIMSSubscription("kermit");

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::ONE))
    .WillOnce(Throw(nfe));

  cass::UnavailableException ue;
  EXPECT_CALL(_client, get_slice(_, _, _, _,
                                 cass::ConsistencyLevel::QUORUM))
    .WillOnce(Throw(ue));

  EXPECT_CALL(*trx, on_failure(_, Cache::NOT_FOUND, _));
  _cache.send(trx, req);
  wait();
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
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000, 300);

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<xml>";

  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(AdvanceTimeMs(12));
  EXPECT_CALL(*trx, on_success(_)).WillOnce(CheckLatency(trx, 12));

  _cache.send(trx, req);
  wait();
}


TEST_F(CacheLatencyTest, DeleteRecordsLatency)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_DeletePublicIDs("kermit", IMPIS, 1000);

  EXPECT_CALL(_client, remove(_, _, _, _));
  EXPECT_CALL(_client, batch_mutate(_, _)).WillRepeatedly(AdvanceTimeMs(13));
  EXPECT_CALL(*trx, on_success(_)).WillOnce(CheckLatency(trx, 13));

  _cache.send(trx, req);
  wait();
}


TEST_F(CacheLatencyTest, GetRecordsLatency)
{
  std::vector<std::string> requested_columns;
  requested_columns.push_back("ims_subscription_xml");

  std::map<std::string, std::string> columns;
  columns["ims_subscription_xml"] = "<howdy>";

  std::vector<cass::ColumnOrSuperColumn> slice;
  make_slice(slice, columns);

  ResultRecorder<Cache::GetIMSSubscription, std::pair<RegistrationState, std::string> > rec;
  RecordingTransaction* trx = make_rec_trx(&rec);
  Cache::Request *req = _cache.create_GetIMSSubscription("kermit");

  EXPECT_CALL(_client, get_slice(_, _, _, _, _))
    .WillOnce(DoAll(SetArgReferee<0>(slice),
                    AdvanceTimeMs(14)));

  EXPECT_CALL(*trx, on_success(_))
    .WillOnce(DoAll(Invoke(trx, &RecordingTransaction::record_result),
                    CheckLatency(trx, 14)));

  _cache.send(trx, req);
  wait();
}


TEST_F(CacheLatencyTest, ErrorRecordsLatency)
{
  CacheTestTransaction *trx = make_trx();
  Cache::Request* req =
    _cache.create_PutIMSSubscription("kermit", "<xml>", RegistrationState::REGISTERED, IMPIS, 1000, 300);

  cass::NotFoundException nfe;
  EXPECT_CALL(_client, batch_mutate(_, _))
    .WillOnce(DoAll(AdvanceTimeMs(12), Throw(nfe)));
  EXPECT_CALL(*trx, on_failure(_, _, _)).WillOnce(CheckLatency(trx, 12));

  _cache.send(trx, req);
  wait();
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromImpi)
{
  std::vector<CFRowColumnValue> expected;

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

  CacheTestTransaction* trx = make_trx();
  Cache::Request* req = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzo", 1000);

  expected.push_back(CFRowColumnValue("impi_mapping", "gonzo", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CFRowColumnValue("impu", "kermit", deleted_impu_columns));
  expected.push_back(CFRowColumnValue("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromMultipleImpis)
{
  std::vector<CFRowColumnValue> expected;

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

  CacheTestTransaction* trx = make_trx();
  std::vector<std::string> impis;
  impis.push_back("gonzo");
  impis.push_back("gonzo2");
  Cache::Request* req = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, impis, 1000);

  expected.push_back(CFRowColumnValue("impi_mapping", "gonzo", deleted_impi_columns));
  expected.push_back(CFRowColumnValue("impi_mapping", "gonzo2", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CFRowColumnValue("impu", "kermit", deleted_impu_columns));
  expected.push_back(CFRowColumnValue("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  do_successful_trx(trx, req);
}


TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromImpiCausingDeletion)
{
  std::vector<CFRowColumnValue> expected;

  std::map<std::string, std::string> impu_columns;
  impu_columns["associated_impi__gonzo"] = "";

  std::vector<cass::ColumnOrSuperColumn> impu_slice;
  make_slice(impu_slice, impu_columns);


  std::map<std::string, std::string> impi_columns;
  impi_columns["associated_primary_impu__kermit"] = "";

  std::vector<cass::ColumnOrSuperColumn> impi_slice;
  make_slice(impi_slice, impi_columns);

  CacheTestTransaction* trx = make_trx();
  Cache::Request* req = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzo", 1000);

  expected.push_back(CFRowColumnValue("impi_mapping", "gonzo", impi_columns));

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

  do_successful_trx(trx, req);
}

TEST_F(CacheRequestTest, DissociateImplicitRegistrationSetFromWrongImpi)
{
  std::vector<CFRowColumnValue> expected;

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

  CacheTestTransaction* trx = make_trx();
  Cache::Request* req = _cache.create_DissociateImplicitRegistrationSetFromImpi({"kermit", "robin"}, "gonzoooooo", 1000);

  expected.push_back(CFRowColumnValue("impi_mapping", "gonzoooooo", deleted_impi_columns));

  // Expect associated IMPI lookup

  EXPECT_CALL(_client,
              get_slice(_,
                        "kermit",
                        ColumnPathForTable("impu"),
                        ColumnsWithPrefix("associated_impi__"),
                        _))
    .WillOnce(SetArgReferee<0>(impu_slice));

  // Expect single-column removal, twice
  expected.push_back(CFRowColumnValue("impu", "kermit", deleted_impu_columns));
  expected.push_back(CFRowColumnValue("impu", "robin", deleted_impu_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  do_successful_trx(trx, req);
  EXPECT_TRUE(_log.contains("not all the provided IMPIs are associated with the IMPU"));
}


