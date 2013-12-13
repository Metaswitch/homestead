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
#include "thrift/Thrift.h"
#include "thrift/transport/TSocket.h"
#include "thrift/transport/TTransport.h"
#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TProtocol.h"
#include "thrift/protocol/TBinaryProtocol.h"
#include "Cassandra.h"

#include "authvector.h"
#include "threadpool.h"

#ifndef CACHE_H__
#define CACHE_H__

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
  /// @param cass_port the port to connect to cassnadra on.
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
  /// The cache takes ownership of the request and will destroy it once it has
  /// resolved.
  ///
  /// @param request object describing the request.
  void send(Request *request);

  /// @class CacheClient
  ///
  /// Simple subclass of a normal cassandra client but that automatically opens
  /// and closes it's transport.
  class CacheClient : public org::apache::cassandra::CassandraClient
  {
  public:
    CacheClient(boost::shared_ptr<apache::thrift::protocol::TProtocol> prot,
                boost::shared_ptr<apache::thrift::transport::TFramedTransport> transport) :
      org::apache::cassandra::CassandraClient(prot),
      _transport(transport)
    {
      transport->open();
    }

    virtual ~CacheClient()
    {
      _transport->close();
    }

  private:
    boost::shared_ptr<apache::thrift::transport::TFramedTransport> _transport;
  };

private:
  /// @class CacheThreadPool
  ///
  /// The thread pool used by the cache.  This is a simple subclass of
  /// ThreadPool that also stores a pointer back to the cache.
  class CacheThreadPool : public ThreadPool<Request *>
  {
  public:
    CacheThreadPool(Cache *cache,
                    unsigned int num_threads,
                    unsigned int max_queue = 0);
    virtual ~CacheThreadPool();

  private:
    Cache *_cache;

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

  CacheClient* get_client();
  void release_client();
  static void delete_client(void *client);

  // The constructors and assignment operation are private to prevent multiple
  // instances of the class from being created.
  Cache();
  Cache(Cache const &);
  void operator=(Cache const &);

public:
  //
  // Request objects.
  //

  class Request
  {
  public:
    Request(std::string& column_family);
    virtual ~Request();

    virtual void run(Cache::CacheClient* client);
    virtual void perform() = 0;
    virtual void on_failure(ResultCode error, std::string& text) = 0;

  protected:
    std::string _column_family;
    CacheClient *_client;
  };

  class ModificationRequest : public Request
  {
  public:
    ModificationRequest(std::string& column_family, int64_t timestamp);
    virtual ~ModificationRequest();

  protected:
    int64_t _timestamp;
  };

  class PutRequest : public ModificationRequest
  {
  public:
    PutRequest(std::string& column_family, int64_t timestamp, int32_t ttl = 0);
    virtual ~PutRequest();

  protected:
    int32_t _ttl;

    void put_columns(std::vector<std::string>& keys,
                     std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl);

  };

  class GetRequest : public Request
  {
  public:
    GetRequest(std::string& column_family);
    virtual ~GetRequest();

  protected:
    void ha_get_row(std::string& key,
                    std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void ha_get_columns(std::string& key,
                        std::vector<std::string>& names,
                        std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void ha_get_columns_with_prefix(std::string& key,
                                    std::string& prefix,
                                    std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void get_row(std::string& key,
                 std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                 org::apache::cassandra::ConsistencyLevel::type consistency_level);

    void get_columns(std::string& key,
                     std::vector<std::string>& names,
                     std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                     org::apache::cassandra::ConsistencyLevel::type consistency_level);

    void get_columns_with_prefix(std::string& key,
                                 std::string& prefix,
                                 std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                                 org::apache::cassandra::ConsistencyLevel::type consistency_level);

    void issue_get_for_key(std::string& key,
                           org::apache::cassandra::SlicePredicate& predicate,
                           std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                           org::apache::cassandra::ConsistencyLevel::type consistency_level);
  };

  class DeleteRowsRequest : public ModificationRequest
  {
  public:
    DeleteRowsRequest(std::string& column_family,
                      int64_t timestamp);
    virtual ~DeleteRowsRequest();

  protected:
    void delete_row(std::string& key,
                    int64_t timestamp);
  };

  class PutIMSSubscription : public PutRequest
  {
  public:
    PutIMSSubscription(std::string& public_id,
                       std::string& xml,
                       int64_t timestamp,
                       int32_t ttl = 0);
    PutIMSSubscription(std::vector<std::string>& public_ids,
                       std::string& xml,
                       int64_t timestamp,
                       int32_t ttl = 0);
    virtual ~PutIMSSubscription();

    void perform();

  protected:
    std::vector<std::string> _public_ids;
    std::string _xml;

    virtual void on_success() = 0;
  };

  class PutAssociatedPublicID : public PutRequest
  {
  public:
    PutAssociatedPublicID(std::string& private_id,
                          std::string& assoc_public_id,
                          int64_t timestamp,
                          int32_t ttl = 0);
    virtual ~PutAssociatedPublicID();

    void perform();

  protected:
    std::string _private_id;
    std::string _assoc_public_id;

    virtual void on_success() = 0;
  };

  class PutAuthVector : public PutRequest
  {
  public:
    PutAuthVector(std::string& private_id,
                  DigestAuthVector& auth_vector,
                  int64_t timestamp,
                  int32_t ttl = 0);
    virtual ~PutAuthVector();

    void perform();

  protected:
    std::vector<std::string> _private_ids;
    DigestAuthVector _auth_vector;

    virtual void on_success() = 0;
  };

  class GetIMSSubscription : public GetRequest
  {
  public:
    GetIMSSubscription(std::string& public_id);
    virtual ~GetIMSSubscription();

    void perform();

  protected:
    std::string _public_id;

    virtual void on_success(std::string& xml) = 0;
  };

  class GetAssociatedPublicIDs : public GetRequest
  {
    GetAssociatedPublicIDs(std::string& private_id);
    virtual ~GetAssociatedPublicIDs();

    void perform();

  protected:
    std::string _private_id;

    virtual void on_success(std::vector<std::string>& public_ids) = 0;
  };

  class GetAuthVector : public GetRequest
  {
  public:
    GetAuthVector(std::string& private_id);
    GetAuthVector(std::string& private_id,
                  std::string& public_id);
    virtual ~GetAuthVector();

    void perform();

  protected:
    std::string _private_id;
    std::string _public_id;

    virtual void on_success(DigestAuthVector& auth_vector) = 0;
  };

  class DeletePublicIDs : public DeleteRowsRequest
  {
  public:
    DeletePublicIDs(std::string& public_id, int64_t timestamp);
    DeletePublicIDs(std::vector<std::string>& public_ids,
                    int64_t timestamp);
    virtual ~DeletePublicIDs();

    void perform();

  protected:
    std::vector<std::string> _public_ids;

    virtual void on_success() = 0;
  };

  class DeletePrivateIDs : public DeleteRowsRequest
  {
  public:
    DeletePrivateIDs(std::string& public_id, int64_t timestamp);
    DeletePrivateIDs(std::vector<std::string>& public_ids,
                     int64_t timestamp);
    virtual ~DeletePrivateIDs();

    void perform();

  protected:
    std::vector<std::string> _private_ids;

    virtual void on_success() = 0;
  };
};

#endif
