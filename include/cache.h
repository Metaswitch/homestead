/**
 * @file cassandracache.h class definition of a cassandra-backed cache.
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
#ifndef CACHE_H__
#define CACHE_H__

// Well this is fun.  Free diameter uses cmake to define some compile time
// options.  Thrift also defines these options.  So an app that uses both won't
// compile because of the multiple definition.
//
// To work around this undefine any troublesome macros here. This means that any
// code that includes DiameterStack and Cache headers must include the Cache
// last.
#ifdef HAVE_CLOCK_GETTIME
#undef HAVE_CLOCK_GETTIME
#endif

#ifdef HAVE_MALLOC_H
#undef HAVE_MALLOC_H
#endif

#ifdef ntohll
#undef ntohll
#endif

#ifdef htonll
#undef htonll
#endif

#include "thrift/Thrift.h"
#include "thrift/transport/TSocket.h"
#include "thrift/transport/TTransport.h"
#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TProtocol.h"
#include "thrift/protocol/TBinaryProtocol.h"
#include "Cassandra.h"

#include "authvector.h"
#include "threadpool.h"
#include "utils.h"
#include "reg_state.h"

namespace cass = org::apache::cassandra;

// Simple data structure to allow specifying a set of column names and values for
// a particular row and column family. Useful when batching operations
// across multiple column families in one Thrift request.
struct CFRowColumnValue
{
  CFRowColumnValue(std::string cf,
                   std::string row,
                   std::map<std::string, std::string> columns) :
    cf(cf),
    row(row),
    columns(columns)
  {};

  CFRowColumnValue(std::string cf,
                   std::string row) :
    cf(cf),
    row(row),
    columns()
  {};

  std::string cf;
  std::string row;
  std::map<std::string, std::string> columns;

};

/// @class Cache
///
/// Singleton class representing a cassandra-backed subscriber cache.
///
/// The usage is as follows:
/// -  At start of day the user calls initialize()
/// -  Configure the cache by calling configure()
/// -  Start the cache by calling start()
/// -  To send a request build a request object (as subclass of Cache::Request)
///    and call send(). The on_success / on_failure method of the request will
///    be called when the request resolves.
/// -  Stop the cache by calling stop().  Optionally call wait_stop() to block
///    until the cache has finished stopping.
///
/// The cache can be reconfigured by doing stop(), wait_stop(), configure(),
/// start().
class Cache
{
public:
  class Request;
  class Transaction;

  virtual ~Cache();

  enum ResultCode
  {
    OK = 0,
    INVALID_REQUEST,
    NOT_FOUND,
    RESOURCE_ERROR,
    CONNECTION_ERROR,
    UNKNOWN_ERROR
  };

  /// @return the singleton cache instance.
  static inline Cache* get_instance() {return INSTANCE;}

  /// Initialize the cache.
  virtual void initialize();

  /// Configure cache settings.
  ///
  /// @param cass_hostname the hostname for the cassandra database.
  /// @param cass_port the port to connect to cassandra on.
  /// @param num_threads the number of worker threads to use for processing
  ///   cache requests.
  /// @param max_queue the maximum number of requests that can be queued waiting
  ///   for a worker thread.  If more requests are added the call to send() will
  ///   block until some existing requests have been processed.  0 => no limit.
  virtual void configure(std::string cass_hostname,
                         uint16_t cass_port,
                         unsigned int num_threads,
                         unsigned int max_queue = 0);

  /// Start the cache.
  ///
  /// Check that the cache can connect to cassandra, and start
  /// the worker threads.
  ///
  /// @return the result of starting the cache.
  virtual ResultCode start();

  /// Stop the cache.
  ///
  /// This discards any queued requests and terminates the worker threads once
  /// their current request has completed.
  virtual void stop();

  /// Wait until the cache has completely stopped.  This method may block.
  virtual void wait_stopped();

  /// Generate a timestamp suitable for supplying on cache modification
  /// requests.
  ///
  /// @return the current time (in micro-seconds).
  static int64_t generate_timestamp();

  /// Send a request to cassandra.
  ///
  /// The cache takes ownership of the transaction and will destroy it once it
  /// has resolved.
  ///
  /// @param trx transaction containing the request.
  virtual void send(Transaction* trx, Request* req);

  /// @class CacheClient
  ///
  /// Interface to the CassandraClient that the cache uses.  Defining this
  /// interface makes it easier to mock out the client in unit test.
  class CacheClientInterface
  {
  public:
    virtual ~CacheClientInterface() {}
    virtual void set_keyspace(const std::string& keyspace) = 0;
    virtual void batch_mutate(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > >& mutation_map
                              const cass::ConsistencyLevel::type consistency_level) = 0;
    virtual void get_slice(std::vector<cass::ColumnOrSuperColumn>& _return,
                           const std::string& key,
                           const cass::ColumnParent& column_parent,
                           const cass::SlicePredicate& predicate,
                           const cass::ConsistencyLevel::type consistency_level) = 0;
    virtual void remove(const std::string& key,
                        const cass::ColumnPath& column_path,
                        const int64_t timestamp,
                        const cass::ConsistencyLevel::type consistency_level) = 0;
  };

  /// @class CacheClient
  ///
  /// Simple subclass of a normal cassandra client but that automatically opens
  /// and closes it's transport.
  class CacheClient : public cass::CassandraClient, public CacheClientInterface
  {
  public:
    CacheClient(boost::shared_ptr<apache::thrift::protocol::TProtocol> prot,
                boost::shared_ptr<apache::thrift::transport::TFramedTransport> transport) :
      cass::CassandraClient(prot),
      _transport(transport)
    {
      transport->open();
    }

    virtual ~CacheClient()
    {
      _transport->close();
    }

    void set_keyspace(const std::string& keyspace)
    {
      cass::CassandraClient::set_keyspace(keyspace);
    }

    void batch_mutate(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > >& mutation_map,
                      const cass::ConsistencyLevel::type consistency_level)
    {
      cass::CassandraClient::batch_mutate(mutation_map, consistency_level);
    }

    void get_slice(std::vector<cass::ColumnOrSuperColumn>& _return,
                   const std::string& key,
                   const cass::ColumnParent& column_parent,
                   const cass::SlicePredicate& predicate,
                   const cass::ConsistencyLevel::type consistency_level)
    {
      cass::CassandraClient::get_slice(_return, key, column_parent, predicate, consistency_level);
    }

    void remove(const std::string& key, const cass::ColumnPath& column_path, const int64_t timestamp, const cass::ConsistencyLevel::type consistency_level)
    {
      cass::CassandraClient::remove(key, column_path, timestamp, consistency_level);
    }

  private:
    boost::shared_ptr<apache::thrift::transport::TFramedTransport> _transport;
  };

private:
  /// @class CacheThreadPool
  ///
  /// The thread pool used by the cache.  This is a simple subclass of
  /// ThreadPool that also stores a pointer back to the cache.
  class CacheThreadPool : public ThreadPool<Cache::Request*>
  {
  public:
    CacheThreadPool(Cache* cache,
                    unsigned int num_threads,
                    unsigned int max_queue = 0);
    virtual ~CacheThreadPool();

  private:
    Cache* _cache;

    void process_work(Request*&);
  };

  // Thread pool is a friend so that it can call get_client().
  friend class CacheThreadPool;

  // Singleton variables.
  static Cache* INSTANCE;
  static Cache DEFAULT_INSTANCE;

  // Cassandra connection information.
  std::string _cass_hostname;
  uint16_t _cass_port;

  // Thread pool management.
  //
  // _num_threads and _max_queue are set up by the call to configure().  These
  // are used when creating the thread pool in the call to start().
  unsigned int _num_threads;
  unsigned int _max_queue;
  CacheThreadPool* _thread_pool;

  // Cassandra connection management.
  //
  // Each worker thread has it's own cassandra client. This is created only when
  // required, and deleted when the thread exits.
  //
  // - _thread_local stores the client as a thread-local variable.
  // - get_client() creates and stores a new client.
  // - delete_client deletes a client (and is automatically called when the
  //   thread exits).
  // - release_client() removes the client from thread-local storage and deletes
  //   it. It allows a thread to pro-actively delete it's client.
  pthread_key_t _thread_local;

  virtual CacheClientInterface* get_client();
  virtual void release_client();
  static void delete_client(void* client);

  // The constructors and assignment operation are protected to prevent multiple
  // instances of the class from being created.
protected:
  Cache();
  Cache(Cache const&);
  void operator=(Cache const&);

public:
  /// @class NoResultsException exception that is thrown to indicate that a
  //requested row does not exist.
  class NoResultsException
  {
  public:
    NoResultsException(const std::string& column_family, const std::string& key) :
      _column_family(column_family),
      _key(key)
    {};

    virtual ~NoResultsException() {} ;

    std::string& get_column_family() {return _column_family; };
    std::string& get_key() {return _key; };

  private:
    std::string _column_family;
    std::string _key;
  };

  //
  // Request objects.
  //
  // Each request to the cache is represented by a request object. There are a
  // number of different request classes which do different things.
  //
  // Request objects are passed to the cache object which processes them
  // asynchronously.  Once complete the request's on_success or on_failure
  // method is called (depending on the result of the request).
  //
  // To issue a request to the cache a user should:
  // -  Create a subclass of Transaction that implments on_success and
  //    on_failure.
  // -  Create a new instance of the transaction subclass.
  // -  Select the appropriate request class and create a new instance by
  //    calling create_<RequestType>.  Do not imnstantiate Request classes
  //    directly as this makes the user code impossible to test with mock
  //    requests or a mock cache.
  // -  Call Cache::send, passing in the transaction and request.

  /// @class Transaction base class for transactions involving the cache.
  class Transaction
  {
  public:
    Transaction() {};
    virtual ~Transaction() {};

    virtual void on_success(Request* req) = 0;
    virtual void on_failure(Request* req, ResultCode error, std::string& text) = 0;

    // Start and stop the transaction stopwatch.  Should only be called by cache
    // module code.
    void start_timer() { _stopwatch.start(); }
    void stop_timer() { _stopwatch.stop(); }

    /// Get the duration of the transaction.
    ///
    /// @param duration_us The duration of the transaction (measured as the
    /// duration of the thrift request). Only valid if this function returns
    /// true.
    ///
    /// @return whether the duration has been obtained.
    bool get_duration(unsigned long& duration_us)
    {
      return _stopwatch.read(duration_us);
    }

  private:
    Utils::StopWatch _stopwatch;
  };

  /// @class Request base class for all cache requests.
  class Request
  {
    // The cache needs to call the Request's protected methods.
    friend class Cache;

  public:
    /// Constructor.
    /// @param column_family the column family the request operates on.
    Request(const std::string& column_family);
    virtual ~Request();

  protected:
    /// Execute a request.
    ///
    /// This method is called automatically by the cache when the request is
    /// ready to be run.
    ///
    /// @param client the client to use to interact with the database.
    /// @param trx the parent transaction (which is notified when the request is
    ///   complete).
    virtual void run(CacheClientInterface* client);

    virtual void set_trx(Transaction* trx) {_trx = trx; }
    virtual Transaction* get_trx() {return _trx; }

    // The get_* methods live in Request rather than GetRequest -
    // classes like PutRequest and DeleteRequest may need to get data
    // to fulfil their purpose (whereas the delete_* and put_* methods
    // should stay in the subclasss, as a GetRequest should never need
    // to put or delete data).

    // After growing a cluster, Cassandra does not pro-actively populate the
    // new nodes with their data (the nodes are expected to use `nodetool
    // repair` if they need to get their data).  Combining this with
    // the fact that we generally use consistency ONE when reading data, the
    // behaviour on new nodes is to return NotFoundException or empty result
    // sets to queries, even though the other nodes have a copy of the data.
    //
    // To resolve this issue, we define ha_ versions of various get methods.
    // These attempt a QUORUM read in the event that a ONE read returns
    // no data.  If the QUORUM read fails due to unreachable nodes, the
    // original result will be used.
    //
    // To implement this, the non-HA versions must take the consistency level as
    // their last parameter.

    /// HA get an entire row.
    ///
    /// @param key row key
    /// @param columns (out) columns in the row
    void ha_get_row(const std::string& key,
                    std::vector<cass::ColumnOrSuperColumn>& columns);

    /// HA get specific columns in a row.
    ///
    /// Note that if a requested row does not exist in cassandra, this method
    /// will return only the rows that do exist. It will not throw an exception
    /// in this case.
    ///
    /// @param key row key
    /// @param names the names of the columns to retrieve
    /// @param columns (out) the retrieved columns
    void ha_get_columns(const std::string& key,
                        const std::vector<std::string>& names,
                        std::vector<cass::ColumnOrSuperColumn>& columns);

    /// HA get all columns in a row
    /// This is useful when working with dynamic columns.
    ///
    /// @param key row key
    /// @param columns (out) the retrieved columns.
    void ha_get_all_columns(const std::string& key,
                            std::vector<cass::ColumnOrSuperColumn>& columns);

    /// HA get all columns in a row that have a particular prefix to their name.
    /// This is useful when working with dynamic columns.
    ///
    /// @param key row key
    /// @param prefix the prefix
    /// @param columns (out) the retrieved columns. NOTE: the column names have
    ///   their prefix removed.
    void ha_get_columns_with_prefix(const std::string& key,
                                    const std::string& prefix,
                                    std::vector<cass::ColumnOrSuperColumn>& columns);

    /// Get an entire row (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_row(const std::string& key,
                 std::vector<cass::ColumnOrSuperColumn>& columns,
                 cass::ConsistencyLevel::type consistency_level);

    /// Get specific columns in a row (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_columns(const std::string& key,
                     const std::vector<std::string>& names,
                     std::vector<cass::ColumnOrSuperColumn>& columns,
                     cass::ConsistencyLevel::type consistency_level);

    /// Get columns whose names begin with the specified prefix. (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_columns_with_prefix(const std::string& key,
                                 const std::string& prefix,
                                 std::vector<cass::ColumnOrSuperColumn>& columns,
                                 cass::ConsistencyLevel::type consistency_level);

    /// Utility method to issue a get request for a particular key.
    ///
    /// @param key row key
    /// @param predicate slice predicate specifying what columns to get.
    /// @param columns (out) the retrieved columns.
    /// @param consistency_level cassandra consistency level.
    void issue_get_for_key(const std::string& key,
                           const cass::SlicePredicate& predicate,
                           std::vector<cass::ColumnOrSuperColumn>& columns,
                           cass::ConsistencyLevel::type consistency_level);

    /// Method that contains the business logic of the request.
    ///
    /// This is called by run from within an exception handler(). It is safe to
    /// throw exceptions from perform, as run() will catch them and convert them
    /// to appropriate error codes.
    virtual void perform() {};

    std::string _column_family;
    CacheClientInterface* _client;
    Transaction* _trx;
  };

  /// @class ModificationRequest a request to modify the cache (e.g. write
  /// columns or delete rows).
  class ModificationRequest : public Request
  {
  public:
    /// Constructor
    ///
    /// @param timestamp the timestamp of the request.  Should be filled in with
    ///   the result from generate_timestamp().  To ensure consistency, related
    ///   cache updates should all be made with the same timestamp.
    ModificationRequest(const std::string& column_family, int64_t timestamp);
    virtual ~ModificationRequest();

  protected:
    int64_t _timestamp;
  };

  /// @class PutRequest a request to write to the database.
  class PutRequest : public ModificationRequest
  {
  public:
    /// Constructor
    /// @param ttl the time-to-live of the written data (after which it expires
    ///   and is deleted). 0 => no expiry.
    PutRequest(const std::string& column_family,
               int64_t timestamp,
               int32_t ttl = 0);
    virtual ~PutRequest();

  protected:
    int32_t _ttl;

    /// Write columns to a row/rows.
    ///
    /// If multiple rows are specified the same columns are written to all rows.
    ///
    /// @param keys the row keys
    /// @param columns the columns to write. Specified as a map {name => value}
    void put_columns(const std::vector<std::string>& keys,
                     const std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl);

    void put_columns_to_multiple_cfs(const std::vector<CFRowColumnValue>& to_put,
                                     int64_t timestamp,
                                     int32_t ttl);

  };

  /// @class GetRequest a request to read data from the cache.
  class GetRequest : public Request
  {
  public:
    /// Constructor.
    GetRequest(const std::string& column_family);
    virtual ~GetRequest();

  protected:
  };

  /// @class DeleteRowsRequest a request to delete one or more rows from the
  /// cache.
  class DeleteRowsRequest : public ModificationRequest
  {
  public:
    DeleteRowsRequest(const std::string& column_family,
                      int64_t timestamp);
    virtual ~DeleteRowsRequest();

  protected:
    /// Delete a row from the cache.
    ///
    /// @param key key of the row to delete
    void delete_row(const std::string& key,
                    int64_t timestamp);

    // Deletes a set of columns from arbitrary rows in arbitrary
    // column familes, as specified by the CFRowColumnValue vector.

    // Useful for batching up delete operations into a single Thrift request.
    void delete_columns_from_multiple_cfs(const std::vector<CFRowColumnValue>& to_rm,
                                          int64_t timestamp);
    void batch_delete(const std::vector<CFRowColumnValue>& to_rm,
                      int64_t timestamp)
    {delete_columns_from_multiple_cfs(to_rm, timestamp);};

  };

  /// @class PutIMSSubscription write the IMS subscription XML for a public ID.
  class PutIMSSubscription : public PutRequest
  {
  public:
    /// Constructor that sets the IMS subscription XML for a *single* public ID.
    ///
    /// @param public_id the public ID.
    /// @param xml the subscription XML.
    /// @param reg_state The new registration state
    /// @param impis A set of private IDs to associate with this
    /// public ID
    PutIMSSubscription(const std::string& public_id,
                       const std::string& xml,
                       const RegistrationState reg_state,
                       const std::vector<std::string>& impis,
                       const int64_t timestamp,
                       const int32_t ttl = 0);

    /// Constructor that sets the same  IMS subscription XML for multiple public
    /// IDs.
    ///
    /// @param public_ids a vector of public IDs to set the XML for.
    /// @param xml the subscription XML.
    /// @param reg_state The new registration state
    /// @param impis A set of private IDs to associate with these
    /// public IDs
    PutIMSSubscription(const std::vector<std::string>& public_ids,
                       const std::string& xml,
                       const RegistrationState reg_state,
                       const std::vector<std::string>& impis,
                       const int64_t timestamp,
                       const int32_t ttl = 0);

    virtual ~PutIMSSubscription();

  protected:
    std::vector<std::string> _public_ids;
    std::vector<std::string> _impis;
    std::string _xml;
    RegistrationState _reg_state;
    int32_t _ttl;

    void perform();
  };

  virtual PutIMSSubscription* create_PutIMSSubscription(const std::string& public_id,
                                                        const std::string& xml,
                                                        const RegistrationState reg_state,
                                                        const std::vector<std::string>& impis,
                                                        const int64_t timestamp,
                                                        const int32_t ttl = 0)
  {
    return new PutIMSSubscription(public_id, xml, reg_state, impis, timestamp, ttl);
  }

  virtual PutIMSSubscription* create_PutIMSSubscription(std::vector<std::string>& public_ids,
                                                        const std::string& xml,
                                                        const RegistrationState reg_state,
                                                        const std::vector<std::string>& impis,
                                                        const int64_t timestamp,
                                                        const int32_t ttl = 0)
  {
    return new PutIMSSubscription(public_ids, xml, reg_state, impis, timestamp, ttl);
  }

  virtual PutIMSSubscription* create_PutIMSSubscription(const std::vector<std::string>& public_ids,
                                                        const std::string& xml,
                                                        const RegistrationState reg_state,
                                                        const int64_t timestamp,
                                                        const int32_t ttl = 0)
  {
    std::vector<std::string> no_impis;
    return new PutIMSSubscription(public_ids, xml, reg_state, no_impis, timestamp, ttl);
  }

  virtual PutIMSSubscription* create_PutIMSSubscription(const std::string& public_id,
                                                        const std::string& xml,
                                                        const RegistrationState reg_state,
                                                        const int64_t timestamp,
                                                        const int32_t ttl = 0)
  {
    std::vector<std::string> no_impis;
    return new PutIMSSubscription(public_id, xml, reg_state, no_impis, timestamp, ttl);
  }

  class PutAssociatedPrivateID : public PutRequest
  {
  public:
    /// Give a set of public IDs (representing an implicit registration set) an associated private ID.
    ///
    /// @param impus The public IDs.
    /// @param impi the private ID to associate with them.
    PutAssociatedPrivateID(const std::vector<std::string>& impus,
                           const std::string& impi,
                           const int64_t timestamp,
                           const int32_t ttl = 0);
    virtual ~PutAssociatedPrivateID();

  protected:
    std::vector<std::string> _impus;
    std::string _impi;

    void perform();
  };

  virtual PutAssociatedPrivateID* create_PutAssociatedPrivateID(
    const std::vector<std::string>& impus,
    const std::string& impi,
    const int64_t timestamp,
    const int32_t ttl = 0)
  {
    return new PutAssociatedPrivateID(impus, impi, timestamp, ttl);
  }

  class PutAssociatedPublicID : public PutRequest
  {
  public:
    /// Give a private_id an associated public ID.
    ///
    /// @param private_id the private ID in question.
    /// @param assoc_public_id the public ID to associate with it.
    PutAssociatedPublicID(const std::string& private_id,
                          const std::string& assoc_public_id,
                          const int64_t timestamp,
                          const int32_t ttl = 0);
    virtual ~PutAssociatedPublicID();

  protected:
    std::string _private_id;
    std::string _assoc_public_id;

    void perform();
  };

  virtual PutAssociatedPublicID* create_PutAssociatedPublicID(const std::string& private_id,
                                                              const std::string& assoc_public_id,
                                                              const int64_t timestamp,
                                                              const int32_t ttl = 0)
  {
    return new PutAssociatedPublicID(private_id, assoc_public_id, timestamp, ttl);
  }

  class PutAuthVector : public PutRequest
  {
  public:
    /// Set the authorization vector used for a private ID.
    ///
    /// This request does not support AKAAuthVectors (it is not possible to
    /// cache these due to the sequence numbers they contain).
    ///
    /// @param private_id the private ID in question.
    /// @param auth_vector the auth vector to store.
    PutAuthVector(const std::string& private_id,
                  const DigestAuthVector& auth_vector,
                  const int64_t timestamp,
                  const int32_t ttl = 0);
    virtual ~PutAuthVector();

  protected:
    std::vector<std::string> _private_ids;
    DigestAuthVector _auth_vector;

    void perform();
  };

  virtual PutAuthVector* create_PutAuthVector(const std::string& private_id,
                                              const DigestAuthVector& auth_vector,
                                              const int64_t timestamp,
                                              const int32_t ttl = 0)
  {
    return new PutAuthVector(private_id, auth_vector, timestamp, ttl);
  }

  class GetIMSSubscription : public GetRequest
  {
  public:
    /// Get the IMS subscription XML for a public identity.
    ///
    /// @param public_id the public identity.
    GetIMSSubscription(const std::string& public_id);
    virtual ~GetIMSSubscription();
    virtual void get_result(std::pair<RegistrationState, std::string>& result);

    /// Access the result of the request.
    ///
    /// @param xml the IMS subscription XML document.
    /// @param ttl the column's time-to-live.
    virtual void get_xml(std::string& xml, int32_t& ttl);

    /// Access the result of the request.
    ///
    /// @param reg_state the registration state value.
    /// @param ttl the column's time-to-live.
    virtual void get_registration_state(RegistrationState& reg_state, int32_t& ttl);

    /// Access the result of the request.
    ///
    /// @param impis The IMPIs associated with this IMS Subscription
    virtual void get_associated_impis(std::vector<std::string>& impis);

    struct Result
    {
      std::string xml;
      RegistrationState state;
      std::vector<std::string> impis;
    };
    virtual void get_result(Result& result);

  protected:
    // Request parameters.
    std::string _public_id;

    // Result.
    std::string _xml;
    RegistrationState _reg_state;
    int32_t _xml_ttl;
    int32_t _reg_state_ttl;
    std::vector<std::string> _impis;

    void perform();
  };

  virtual GetIMSSubscription* create_GetIMSSubscription(const std::string& public_id)
  {
    return new GetIMSSubscription(public_id);
  }

  /// Get all the public IDs that are associated with one or more
  /// private IDs.

  // Only used when subscribers are locally provisioned - for the
  // database operation that stores associations between IMPIs and
  // primary public IDs for use in handling RTRs, see GetAssociatedPrimaryPublicIDs.

  class GetAssociatedPublicIDs : public GetRequest
  {
  public:
    /// Get the public Ids that are associated with a single private ID.
    ///
    /// @param private_id the private ID.
    GetAssociatedPublicIDs(const std::string& private_id);

    /// Get the public Ids that are associated with multiple private IDs.
    ///
    /// @param private_ids a vector of private IDs.
    GetAssociatedPublicIDs(const std::vector<std::string>& private_ids);
    virtual ~GetAssociatedPublicIDs();

    /// Access the result of the request.
    ///
    /// @param public_ids A vector of public IDs associated with the private ID.
    virtual void get_result(std::vector<std::string>& public_ids);

  protected:
    // Request parameters.
    std::vector<std::string> _private_ids;

    // Result.
    std::vector<std::string> _public_ids;

    void perform();
  };

  virtual GetAssociatedPublicIDs* create_GetAssociatedPublicIDs(const std::string& private_id)
  {
    return new GetAssociatedPublicIDs(private_id);
  }

  virtual GetAssociatedPublicIDs* create_GetAssociatedPublicIDs(
    const std::vector<std::string>& private_ids)
  {
    return new GetAssociatedPublicIDs(private_ids);
  }

  /// Retrieves the primary public IDs which a particular IMPI has been
  /// used to authenticate, by querying the "impi_mapping" table.

  /// Note that this operates on the "impi_mapping" table (storing data
  /// needed to handle Registration-Termination-Requests, and only used
  /// when we have a HSS) not the "impi" table (storing the SIP digest
  /// HA1 and all the public IDs associated with this IMPI, and only
  /// used when subscribers are locally provisioned).
  class GetAssociatedPrimaryPublicIDs : public GetRequest
  {
  public:
    /// Get the primary public Ids that are associated with a single private ID.
    ///
    /// @param private_id the private ID.
    GetAssociatedPrimaryPublicIDs(const std::string& private_id);
    virtual ~GetAssociatedPrimaryPublicIDs() {};

    /// Access the result of the request.
    ///
    /// @param public_ids A vector of public IDs associated with the private ID.
    virtual void get_result(std::vector<std::string>& public_ids);

  protected:
    // Request parameters.
    std::string _private_id;

    // Result.
    std::vector<std::string> _public_ids;

    void perform();
  };

  virtual GetAssociatedPrimaryPublicIDs* create_GetAssociatedPrimaryPublicIDs(
    const std::string& private_id)
  {
    return new GetAssociatedPrimaryPublicIDs(private_id);
  }

  class GetAuthVector : public GetRequest
  {
  public:
    /// Get the auth vector of a private ID.
    ///
    /// @param private_id the private ID.
    GetAuthVector(const std::string& private_id);

    /// Get the auth vector of a private ID that has an associated public ID.
    ///
    /// If the private ID exists but the public ID is not associated with it,
    /// the on_failure will be called with a NOT_FOUND result.
    ///
    /// @param private_id the private ID.
    GetAuthVector(const std::string& private_id,
                  const std::string& public_id);
    virtual ~GetAuthVector();

    /// Access the result of the request.
    ///
    /// @param auth_vector the digest auth vector for the private ID.
    virtual void get_result(DigestAuthVector& auth_vector);

  protected:
    // Request parameters.
    std::string _private_id;
    std::string _public_id;

    // Result.
    DigestAuthVector _auth_vector;

    void perform();
  };

  virtual GetAuthVector* create_GetAuthVector(const std::string& private_id)
  {
    return new GetAuthVector(private_id);
  }

  virtual GetAuthVector* create_GetAuthVector(const std::string& private_id,
                                              const std::string& public_id)
  {
    return new GetAuthVector(private_id, public_id);
  }

  class DeletePublicIDs : public DeleteRowsRequest
  {
  public:
    /// Delete several public IDs from the cache, and also dissociate
    /// the primary public ID (the first one in the vector) from the
    /// given IMPIs.
    ///
    /// @param public_ids the public IDs to delete.
    /// @param impis the IMPIs to dissociate from this implicit
    ///              registration set.
    DeletePublicIDs(const std::vector<std::string>& public_ids,
                    const std::vector<std::string>& impis,
                    int64_t timestamp);

    /// Delete a public ID from the cache, and also dissociate
    /// it from the given IMPIs.
    ///
    /// @param public_id the public ID to delete.
    /// @param impis the IMPIs to dissociate from this implicit
    ///              registration set.
    DeletePublicIDs(const std::string& public_id,
                    const std::vector<std::string>& impis,
                    int64_t timestamp);

    virtual ~DeletePublicIDs();

  protected:
    std::vector<std::string> _public_ids;
    std::vector<std::string> _impis;

    void perform();
  };

  virtual DeletePublicIDs* create_DeletePublicIDs(
    const std::vector<std::string>& public_ids,
    const std::vector<std::string>& impis,
    int64_t timestamp)
  {
    return new DeletePublicIDs(public_ids, impis, timestamp);
  }

  virtual DeletePublicIDs* create_DeletePublicIDs(
    const std::string& public_id,
    const std::vector<std::string>& impis,
    int64_t timestamp)
  {
    return new DeletePublicIDs(public_id, impis, timestamp);
  }

  class DeletePrivateIDs : public DeleteRowsRequest
  {
  public:
    /// Delete a single private ID from the cache.
    ///
    /// @param private_id the private ID to delete.
    DeletePrivateIDs(const std::string& private_id, int64_t timestamp);

    /// Delete an single private ID from the cache.
    ///
    /// @param private_ids the private IDs to delete.
    DeletePrivateIDs(const std::vector<std::string>& private_ids,
                     int64_t timestamp);
    virtual ~DeletePrivateIDs();

  protected:
    std::vector<std::string> _private_ids;

    void perform();
  };

  virtual DeletePrivateIDs* create_DeletePrivateIDs(const std::string& private_id,
                                                    int64_t timestamp)
  {
    return new DeletePrivateIDs(private_id, timestamp);
  }

  virtual DeletePrivateIDs* create_DeletePrivateIDs(const std::vector<std::string>& private_ids,
                                                    int64_t timestamp)
  {
    return new DeletePrivateIDs(private_ids, timestamp);
  }

  /// DeleteIMPIMapping operates on the "impi_mapping" Cassandra table,
  /// and deletes whole rows - effectively causing the cache to
  /// "forget" that a particular IMPI has been used to authenticate any
  /// IMPUs.

  /// The main use-case is for Registration-Termination-Requests, which
  /// may specify a private ID and require the S-CSCF to clear all data
  /// and bindings associated with it.

  class DeleteIMPIMapping : public DeleteRowsRequest
  {
  public:
    /// Delete a mapping from private IDs to the IMPUs they have authenticated.
    ///
    DeleteIMPIMapping(const std::vector<std::string>& private_ids,
                      int64_t timestamp);
    virtual ~DeleteIMPIMapping() {};

  protected:
    std::vector<std::string> _private_ids;

    void perform();
  };

  virtual DeleteIMPIMapping* create_DeleteIMPIMapping(const std::vector<std::string>& private_ids,
                                                      int64_t timestamp)
  {
    return new DeleteIMPIMapping(private_ids, timestamp);
  }

  /// DeleteIMPIMapping operates on the "impi_mapping" and "impu"
  /// Cassandra tables. It takes a vector of public IDs (representing
  /// the IDs in an implicit registration set) and a private ID, and
  /// removes the association between them:
  ///   * Each public ID's row in the IMPU table is updated to remove
  ///     this private ID. If this is the last private ID, the row is deleted.
  ///   * The private ID's row in the IMPI mapping table is updated to
  ///     remove the primary public ID. If this was the last prmary
  ///     public ID, the row is left empty for Cassandra to eventually
  ///     delete.

  /// The main use-case is for Registration-Termination-Requests.

  class DissociateImplicitRegistrationSetFromImpi : public DeleteRowsRequest
  {
  public:
    /// Delete a mapping from private IDs to the IMPUs they have authenticated.
    ///
    DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                              const std::string& impi,
                                              int64_t timestamp);
    virtual ~DissociateImplicitRegistrationSetFromImpi() {};

  protected:
    std::vector<std::string> _impus;
    std::string _impi;

    void perform();
  };

  virtual DissociateImplicitRegistrationSetFromImpi* create_DissociateImplicitRegistrationSetFromImpi(
    const std::vector<std::string>& impus,
    const std::string& impi,
    int64_t timestamp)
  {
    return new DissociateImplicitRegistrationSetFromImpi(impus, impi, timestamp);
  }
};

#endif
