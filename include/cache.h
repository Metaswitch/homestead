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

// TODO sort this out properly.
//
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

namespace cass = org::apache::cassandra;

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
  static inline Cache* get_instance() { return INSTANCE; }

  /// Initialize the cache.
  void initialize();

  /// Configure cache settings.
  ///
  /// @param cass_hostname the hostname for the cassandra database.
  /// @param cass_port the port to connect to cassandra on.
  /// @param num_threads the number of worker threads to use for processing
  ///   cache requests.
  /// @param max_queue the maximum number of requests that can be queued waiting
  ///   for a worker thread.  If more requests are added the call to send() will
  ///   block until some existing requests have been processed.  0 => no limit.
  void configure(std::string cass_hostname,
                 uint16_t cass_port,
                 unsigned int num_threads,
                 unsigned int max_queue = 0);

  /// Start the cache.
  ///
  /// Check that the cache can connect to cassandra, and start
  /// the worker threads.
  ///
  /// @return the result of starting the cache.
  ResultCode start();

  /// Stop the cache.
  ///
  /// This discards any queued requests and terminates the worker threads once
  /// their current request has completed.
  void stop();

  /// Wait until the cache has completely stopped.  This method my block.
  void wait_stopped();

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
  void send(Transaction* trx);

  /// @class CacheClient
  ///
  /// Simple subclass of a normal cassandra client but that automatically opens
  /// and closes it's transport.

  class CacheClientInterface
  {
  public:
    virtual void set_keyspace(const std::string& keyspace) = 0;
    virtual void batch_mutate(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > > & mutation_map, const cass::ConsistencyLevel::type consistency_level) = 0;
    virtual void get_slice(std::vector<cass::ColumnOrSuperColumn> & _return, const std::string& key, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level) = 0;
    virtual void remove(const std::string& key, const cass::ColumnPath& column_path, const int64_t timestamp, const cass::ConsistencyLevel::type consistency_level) = 0;
  };

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

    void batch_mutate(const std::map<std::string, std::map<std::string, std::vector<cass::Mutation> > > & mutation_map, const cass::ConsistencyLevel::type consistency_level)
    {
      cass::CassandraClient::batch_mutate(mutation_map, consistency_level);
    }

    void get_slice(std::vector<cass::ColumnOrSuperColumn> & _return, const std::string& key, const cass::ColumnParent& column_parent, const cass::SlicePredicate& predicate, const cass::ConsistencyLevel::type consistency_level)
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
  class CacheThreadPool : public ThreadPool<Transaction *>
  {
  public:
    CacheThreadPool(Cache *cache,
                    unsigned int num_threads,
                    unsigned int max_queue = 0);
    virtual ~CacheThreadPool();

  private:
    Cache *_cache;

    void process_work(Transaction*&);
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
  CacheThreadPool *_thread_pool;

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
  static void delete_client(void *client);

  // The constructors and assignment operation are private to prevent multiple
  // instances of the class from being created.
  Cache();
  Cache(Cache const &);
  void operator=(Cache const &);

public:
  /// @class RowNotFoundException exception that is thrown to indicate that a
  //requested row does not exist.
  class RowNotFoundException
  {
  public:
    RowNotFoundException(const std::string& column_family, const std::string& key) :
      _column_family(column_family),
      _key(key)
    {};

    virtual ~RowNotFoundException() {} ;

    std::string& get_column_family() { return _column_family; };
    std::string& get_key() { return _key; };

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
  // -  Select the appropriate request class and create a new instance.
  // -  Create a new instance of the transaction subclass passing it the request
  //    object.
  // -  Call Cache::send, passing in the transaction object.

  /// @class Transaction base class for transactions involving the cache.
  class Transaction
  {
  public:
    /// Constructor.
    ///
    /// @param req underlying request object.  The transaction takes ownership
    /// of this and is responsible for freeing it.
    Transaction(Request* req);
    virtual ~Transaction();

    void run(CacheClientInterface* client);

    virtual void on_success() = 0;
    virtual void on_failure(ResultCode error, std::string& text) = 0;

  protected:
    Request* _req;
  };

  /// @class Request base class for all cache requests.
  class Request
  {
  public:
    /// Constructor.
    /// @param column_family the column family the request operates on.
    Request(std::string& column_family);
    virtual ~Request();

    /// Execute a request.
    ///
    /// This method is called automatically by the cache when the request is
    /// ready to be run.
    ///
    /// @param client the client to use to interact with the database.
    /// @param trx the parent transaction (which is notified when the request is
    ///   complete).
    virtual void run(CacheClientInterface* client,
                     Transaction *trx);

  protected:
    /// Method that contains the business logic of the request.
    ///
    /// This is called by run from within an exception handler(). It is safe to
    /// throw exceptions from perform, as run() will catch them and convert them
    /// to appropriate error codes.
    virtual void perform() {};

    std::string _column_family;
    CacheClientInterface *_client;
    Transaction *_trx;
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
    ModificationRequest(std::string& column_family, int64_t timestamp);
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
    PutRequest(std::string& column_family, int64_t timestamp, int32_t ttl = 0);
    virtual ~PutRequest();

  protected:
    int32_t _ttl;

    /// Write columns to a row/rows.
    ///
    /// If multiple rows are specified the same columns are written to all rows.
    ///
    /// @param keys the row keys
    /// @param columns the columns to write. Specified as a map {name => value}
    void put_columns(std::vector<std::string>& keys,
                     std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl);
  };

  /// @class GetRequest a request to read data from the cache.
  class GetRequest : public Request
  {
  public:
    /// Constructor.
    GetRequest(std::string& column_family);
    virtual ~GetRequest();

  protected:
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
    void ha_get_row(std::string& key,
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
    void ha_get_columns(std::string& key,
                        std::vector<std::string>& names,
                        std::vector<cass::ColumnOrSuperColumn>& columns);

    /// HA get all columns in a row that have a particular prefix to their name.
    /// This is useful when working with dynamic columns.
    ///
    /// @param key row key
    /// @param prefix the prefix
    /// @param columns (out) the retrieved columns. NOTE: the column names have
    ///   their prefix removed.
    void ha_get_columns_with_prefix(std::string& key,
                                    std::string& prefix,
                                    std::vector<cass::ColumnOrSuperColumn>& columns);

    /// Get an entire row (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_row(std::string& key,
                 std::vector<cass::ColumnOrSuperColumn>& columns,
                 cass::ConsistencyLevel::type consistency_level);

    /// Get specific columns in a row (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_columns(std::string& key,
                     std::vector<std::string>& names,
                     std::vector<cass::ColumnOrSuperColumn>& columns,
                     cass::ConsistencyLevel::type consistency_level);

    /// Get columns whose names begin with the specified prefix. (non-HA).
    /// @param consistency_level cassandra consistency level.
    void get_columns_with_prefix(std::string& key,
                                 std::string& prefix,
                                 std::vector<cass::ColumnOrSuperColumn>& columns,
                                 cass::ConsistencyLevel::type consistency_level);

    /// Utility method to issue a get request for a particular key.
    ///
    /// @param key row key
    /// @param predicate slice predicate specifying what columns to get.
    /// @param columns (out) the retrieved columns.
    /// @param consistency_level cassandra consistency level.
    void issue_get_for_key(std::string& key,
                           cass::SlicePredicate& predicate,
                           std::vector<cass::ColumnOrSuperColumn>& columns,
                           cass::ConsistencyLevel::type consistency_level);
  };

  /// @class DeleteRowsRequest a request to delete one or more rows from the
  /// cache.
  class DeleteRowsRequest : public ModificationRequest
  {
  public:
    DeleteRowsRequest(std::string& column_family,
                      int64_t timestamp);
    virtual ~DeleteRowsRequest();

  protected:
    /// Delete a row from the cache.
    ///
    /// @param key key of the row to delete
    void delete_row(std::string& key,
                    int64_t timestamp);
  };

  /// @class PutIMSSubscription write the IMS subscription XML for a public ID.
  class PutIMSSubscription : public PutRequest
  {
  public:
    /// Constructor that sets the IMS subscription XML for a *single* public ID.
    ///
    /// @param public_id the public ID.
    /// @param xml the subscription XML.
    PutIMSSubscription(const std::string& public_id,
                       const std::string& xml,
                       const int64_t timestamp,
                       const int32_t ttl = 0);

    /// Constructor that sets the same  IMS subscription XML for multiple public
    /// IDs.
    ///
    /// @param public_ids a vector of public IDs to set the XML for.
    /// @param xml the subscription XML.
    PutIMSSubscription(const std::vector<std::string>& public_ids,
                       const std::string& xml,
                       const int64_t timestamp,
                       const int32_t ttl = 0);
    virtual ~PutIMSSubscription();

  protected:
    std::vector<std::string> _public_ids;
    std::string _xml;

    void perform();
  };

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

  class GetIMSSubscription : public GetRequest
  {
  public:
    /// Get the IMS subscription XML for a public identity.
    ///
    /// @param public_id the public identity.
    GetIMSSubscription(std::string& public_id);
    virtual ~GetIMSSubscription();

    /// Access the result of the request.
    ///
    /// @param xml the IMS subscription XML document.
    virtual void get_result(std::string& xml);

  protected:
    // Request parameters.
    std::string _public_id;

    // Result.
    std::string _xml;

    void perform();
  };

  class GetAssociatedPublicIDs : public GetRequest
  {
  public:
    /// Get the public Ids that are associated with a private ID.
    ///
    /// @param private_id the private ID.
    GetAssociatedPublicIDs(std::string& private_id);
    virtual ~GetAssociatedPublicIDs();

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

  class GetAuthVector : public GetRequest
  {
  public:
    /// Get the auth vector of a private ID.
    ///
    /// @param private_id the private ID.
    GetAuthVector(std::string& private_id);

    /// Get the auth vector of a private ID that has an associated public ID.
    ///
    /// If the private ID exists but the public ID is not associated with it,
    /// the on_failure will be called with a NOT_FOUND result.
    ///
    /// @param private_id the private ID.
    GetAuthVector(std::string& private_id,
                  std::string& public_id);
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

  class DeletePublicIDs : public DeleteRowsRequest
  {
  public:
    /// Delete an single public ID from the cache.
    ///
    /// @param public_id the public ID to delete.
    DeletePublicIDs(std::string& public_id, int64_t timestamp);

    /// Delete several public IDs from the cache.
    ///
    /// @param public_ids the public IDs to delete.
    DeletePublicIDs(std::vector<std::string>& public_ids,
                    int64_t timestamp);
    virtual ~DeletePublicIDs();

  protected:
    std::vector<std::string> _public_ids;

    void perform();
  };

  class DeletePrivateIDs : public DeleteRowsRequest
  {
  public:
    /// Delete an single private ID from the cache.
    ///
    /// @param private_id the private ID to delete.
    DeletePrivateIDs(std::string& public_id, int64_t timestamp);

    /// Delete an single private ID from the cache.
    ///
    /// @param private_id the private ID to delete.
    DeletePrivateIDs(std::vector<std::string>& public_ids,
                     int64_t timestamp);
    virtual ~DeletePrivateIDs();

  protected:
    std::vector<std::string> _private_ids;

    void perform();
  };
};

#endif
