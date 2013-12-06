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

#ifndef CASSANDRACACHE_H__
#define CASSANDRACACHE_H__

class CassandraCache
{
public:
  class Exception
  {
  public:
    inline Exception(const char* func, int rc) : _func(func), _rc(rc) {};
    const char* _func;
    const int _rc;
  };

  class NotFound : Exception {};
  class CassandraError : Exception {};

  CassandraCache (arguments);
  virtual ~CassandraCache ();

  static inline CassandraCache* get_instance() { return INSTANCE; }

  void initialize();
  void configure(std::string cass_hostname,
                 uint16_t cass_port);
  void start();
  void stop();
  void wait_stopped();

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
  DigestAuthVector *get_auth_vector(std::string& private_id);

  void delete_public_id(std::string& public_id);
  void delete_multi_public_id(std::vector<std::string>& public_ids);

  void delete_private_id(std::string& private_id);
  void delete_multi_private_id(std::vector<std::string>& private_ids);

private:
  static CassandraCache* INSTANCE;
  static CassandraCache DEFAULT_INSTANCE;

  static const std::string KEYSPACE;

  std::string _cass_host;
  uint16_t _cass_port;

  // The constructors and assignment operation are private to prevent multiple
  // instances of the class from being created.
  CassandraCache();
  CassandraCache(CassandraCache const &);
  void operator=(CassandraCache const &);

  // Get a thread-specific Cassandra connection.
  org::apache::cassandra::CassandraClient* get_client();

  void modify_column(std::string& key,
                     std::string& name,
                     std::string& val,
                     int64_t timestamp,
                     int32_t ttl);
  void get_row(std::string& key,
               std::vector<ColumnOrSuperColumn>& columns);
  void delete_row(std::string& key);

  void serialize_digest_auth_vector(DigestAuthVector& auth_vector,
                                    std::vector<Mutation>& columns);
  void deserialize_digest_auth_vector(std::vector<ColumnOrSuperColumn>& columns,
                                      DigestAuthVector& auth_vector);
};
