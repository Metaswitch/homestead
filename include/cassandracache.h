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

#ifndef CASSANDRACACHE_H__
#define CASSANDRACACHE_H__

// Use the apache cassandra namespace. It's not ideal to do this in a header
// file, but our method declarations get stupidly long otherwise.
using namespace org::apache::cassandra;

// Singleton class representing a cassandra-backed subscriber cache.
class CassandraCache
{
public:
  //
  // Exceptions that may be thown by this class.
  //
  class Exception
  {
  public:
    inline Exception(const char* func, int rc) : _func(func), _rc(rc) {};
    const char* _func;
    const int _rc;
  };

  // A entry could not be found in the cache.
  class NotFound : Exception {};

  // There was an error communicating with Cassandra.
  class CassandraError : Exception {};

  //
  // Methods to manage the cache instance. These mirror the methods used to
  // mange the HTTP and Diameter stacks.
  //
  static inline CassandraCache* get_instance() { return INSTANCE; }

  void initialize();
  void configure(std::string cass_hostname,
                 uint16_t cass_port);
  void start();
  void stop();
  void wait_stopped();

  //
  // Methods to set, get and delete items in the cache.
  //
  // Methods that moidy the cache must take a timestamp parameter. Related
  // updates should be made with the same timestamp, which ensures the cache
  // ends up being (eventually) consistent.
  //
  // The 'put' methods also take a time to live (TTL) parameter. This specifys
  // how long the entry exists in the cache before being automatically expired.
  // 0 => the entry never expires. Modifying the row resets the expiry time.
  //
  void put_imssubscription(std::string& public_id,
                           std::string& xml,
                           int64_t timestamp,
                           int32_t ttl = 0);
  void put_multi_imssubscription(std::vector<std::string>& public_ids,
                                 std::string& xml,
                                 int64_t timestamp,
                                 int32_t ttl = 0);
  std::string* get_imssubscription(std::string& public_id);

  void put_assoc_public_id(std::string& private_id,
                           std::string& assoc_public_id,
                           int64_t timestamp,
                           int32_t ttl = 0);
  std::vector<std::string>* get_assoc_public_ids(std::string& private_id);

  void put_auth_vector(std::string& private_id,
                       DigestAuthVector& auth_vector,
                       int64_t timestamp,
                       int32_t ttl = 0);
  DigestAuthVector *get_auth_vector(std::string& private_id,
                                    std::string* public_id = NULL);

  void delete_public_id(std::string& public_id, int64_t timestamp);
  void delete_multi_public_id(std::vector<std::string>& public_ids,
                              int64_t timestamp);
  void delete_private_id(std::string& private_id, int64_t timestamp);
  void delete_multi_private_id(std::vector<std::string>& private_ids,
                               int64_t timestamp);

  // Return the current time (in micro-seconds). This timestamp is suitable to
  // use with methods that modify the cache.
  int64_t generate_timestamp(void);

  virtual ~CassandraCache();
private:

  // Singleton variables.
  static CassandraCache* INSTANCE;
  static CassandraCache DEFAULT_INSTANCE;

  // The keyspace the cache is stored in.
  static const std::string KEYSPACE;

  // Cassandra connection details.
  std::string _cass_host;
  uint16_t _cass_port;

  // The constructors and assignment operation are private to prevent multiple
  // instances of the class from being created.
  CassandraCache();
  CassandraCache(CassandraCache const &);
  void operator=(CassandraCache const &);

  // Get a thread-specific Cassandra connection.
  CassandraClient* _get_client();

  //
  // Utility methods to manipulate cassandra rows/columns.
  //
  void _modify_column(std::string& key,
                      std::string& name,
                      std::string& val,
                      int64_t timestamp,
                      int32_t ttl);

  void _get_row(std::string& key,
                std::vector<ColumnOrSuperColumn>& columns,
                ConsistencyLevel consistency_level);

  void _ha_get_row(std::string& key,
                   std::vector<ColumnOrSuperColumn>& columns);

  void _get_columns(std::string& key,
                    std::vector<std::string>& names,
                    std::vector<ColumnOrSuperColumn>& columns,
                    ConsistencyLevel consistency_level);
  void _ha_get_columns(std::string& key,
                       std::vector<std::string>& names,
                       std::vector<ColumnOrSuperColumn>& columns);

  void _get_columns_with_prefix(std::string& key,
                                std::string& prefix,
                                std::vector<ColumnOrSuperColumn>& columns,
                                ConsistencyLevel consistency_level);
  void _ha_get_columns_with_prefix(std::string& key,
                                   std::string& prefix,
                                   std::vector<ColumnOrSuperColumn>& columns);

  void _delete_row(std::string& key);

  void _serialize_digest_auth_vector(DigestAuthVector& auth_vector,
                                     std::vector<Mutation>& columns);
  void _deserialize_digest_auth_vector(std::vector<ColumnOrSuperColumn>& columns,
                                       DigestAuthVector& auth_vector);
};

#endif
