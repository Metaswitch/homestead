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
#include "Cassandra.h"

#include "authvector.h"
#include "threadpool.h"

#ifndef CACHE_H__
#define CACHE_H__

// Singleton class representing a cassandra-backed subscriber cache.
class Cache
{
public:
  virtual ~Cache();

  enum ResultCode
  {
    OK = 0
  };

  //
  // Methods to manage the cache instance. These mirror the methods used to
  // mange the HTTP and Diameter stacks.
  //
  static inline Cache* get_instance() { return INSTANCE; }

  void initialize();
  void configure(std::string cass_hostname,
                 uint16_t cass_port);
  ResultCode start();
  void stop();
  void wait_stopped();

  // Class representing a cassandra request.
  class Request
  {
  public:
    Request(std::string& table);
    virtual ~Request();

    virtual void send(Cache *cache);

  private:
    virtual void on_success() {};

    virtual void on_error(ResultCode error_code)
    {
      // TODO write a debug log.
    };

    virtual void _process();

    std::string _table;
  };

  // Class representing a request to modify the cassandra cache - for example
  // putting some columns, deleting rows, etc.
  class ModificationRequest : Request
  {
  public:
    ModificationRequest(std::string& table, int64_t timestamp);
    virtual ~ModificationRequest();

  private:
    int64_t _timestamp;
  };

  // Class rerpresenting a request to put some data into the cassandra cache.
  class PutRequest : ModificationRequest
  {
  public:
    PutRequest(std::string& table, int64_t timestamp, int32_t ttl = 0);
    virtual ~PutRequest();

  private:
    int32_t _ttl;

    void _put_columns(std::vector<std::string>& keys,
                      std::map<std::string, std::string>& columns,
                      int64_t timestamp,
                      int32_t ttl);

  };

  // Class representing a request to get some data from the cassandra cache.
  class GetRequest : Request
  {
  public:
    virtual ~GetRequest();

  private:
    void _ha_get_row(std::string& key,
                     std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void _ha_get_columns(std::string& key,
                         std::vector<std::string>& names,
                         std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void _ha_get_columns_with_prefix(std::string& key,
                                     std::string& prefix,
                                     std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);

    void _get_row(std::string& key,
                  std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                  org::apache::cassandra::ConsistencyLevel consistency_level);

    void _get_columns(std::string& key,
                      std::vector<std::string>& names,
                      std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                      org::apache::cassandra::ConsistencyLevel consistency_level);

    void _get_columns_with_prefix(std::string& key,
                                  std::string& prefix,
                                  std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns,
                                  org::apache::cassandra::ConsistencyLevel consistency_level);

    void _issue_get_for_key(std::string& key,
                            org::apache::cassandra::SlicePredicate& predicate,
                            std::vector<org::apache::cassandra::ColumnOrSuperColumn>& columns);
  };

  // Class representing a request to delete some rows from the cassandra cache.
  class DeleteRowsRequest : ModificationRequest
  {
  public:
    DeleteRowsRequest(std::string& table,
                      std::vector<std::string>& keys,
                      int64_t timestamp);
    virtual ~DeleteRowsRequest();

  private:
    void _delete_row(std::string& key,
                     int64_t timestamp);
  };

  // A request to put an IMS subscription XML document into the cache.
  class PutIMSSubscription : PutRequest
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

  private:
    std::vector<std::string> _public_ids;
    std::string _xml;

    void _process();
  };

  // A request to associate a public ID with a particular private ID.
  class PutAssociatedPublicID : PutRequest
  {
  public:
    PutAssociatedPublicID(std::string& private_id,
                          std::string& assoc_public_id,
                          int64_t timestamp,
                          int32_t ttl = 0);
    virtual ~PutAssociatedPublicID();

  private:
    std::string& _private_id;
    std::string _assoc_public_id;

    void _process();
  };

  // A request to add an authorization vector to the cache.
  class PutAuthVector : PutRequest
  {
  public:
    PutAuthVector(std::string& private_id,
                  DigestAuthVector& auth_vector,
                  int64_t timestamp,
                  int32_t ttl = 0);
    virtual ~PutAuthVector();

  private:
    std::string _private_id;
    DigestAuthVector *auth_vector;

    void _process();
  };

  // A request to get the IMS subscription XML for a public ID.
  class GetIMSSubscription : GetRequest
  {
  public:
    GetIMSSubscription(std::string& public_id);
    virtual ~GetIMSSubscription();

  private:
    std::string _public_id;

    void on_success(std::string& xml) {};
    void _process();
  };

  // A request to get the public IDs associated with a private ID.
  class GetAssociatedPublicIDs : GetRequest
  {
    GetAssociatedPublicIDs(std::string& private_id);
    virtual ~GetAssociatedPublicIDs();

  private:
    std::string _private_id;

    void on_success(std::vector<std::string>& public_ids) {};
    void _process();
  };

  // A request to get the authorization vector for a private ID.
  class GetAuthVector : GetRequest
  {
  public:
    GetAuthVector(std::string& private_id);
    GetAuthVector(std::string& private_id,
                  std::string& public_id);
    virtual ~GetAuthVector();

  private:
    std::string _private_id;
    std::string _public_id;

    void on_success(DigestAuthVector *auth_vector);
    void _process();
  };

  // A request to delete one or more public IDs.
  class DeletePublicIDs : DeleteRowsRequest
  {
  public:
    DeletePublicIDs(std::string& public_id, int64_t timestamp);
    DeletePublicIDs(std::vector<std::string>& public_ids,
                    int64_t timestamp);
    virtual ~DeletePublicIDs();

  private:
    std::vector<std::string> public_ids;

    void _process();
  };

  // A request to delete one or more private IDs.
  class DeletePrivateIDs : DeleteRowsRequest
  {
  public:
    DeletePrivateIDs(std::string& public_id, int64_t timestamp);
    DeletePrivateIDs(std::vector<std::string>& public_ids,
                     int64_t timestamp);
    virtual ~DeletePrivateIDs();

  private:
    std::vector<std::string> public_ids;

    void _process();
  };

  // Return the current time (in micro-seconds). This timestamp is suitable to
  // use with methods that modify the cache.
  int64_t generate_timestamp(void);

private:
  class CacheThreadPool : ThreadPool<Request *>
  {
  public:
    CacheThreadPool(unsigned int num_threads, unsigned int max_queue = 0);
    virtual ~CacheThreadPool();

  private:
    void _process_work(Request *);
    void _on_thread_shutdown();
  };

  // Singleton variables.
  static Cache* INSTANCE;
  static Cache DEFAULT_INSTANCE;

  static const std::string KEYSPACE;

  std::string _cass_host;
  uint16_t _cass_port;

  CacheThreadPool _thread_pool;
  pthread_key_t _thread_local;

  void _queue_request(Request *request);

  // Get a thread-specific Cassandra connection.
  org::apache::cassandra::CassandraClient* _get_client();
  void _release_client();

  // The constructors and assignment operation are private to prevent multiple
  // instances of the class from being created.
  Cache();
  Cache(Cache const &);
  void operator=(Cache const &);
};

#endif
