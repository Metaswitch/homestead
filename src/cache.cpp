/**
 * @file cache.cpp implementation of a cassandra-backed cache.
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

#include <cache.h>

using namespace org::apache::cassandra;

void Cache::initialize()
{
  // No-op
}

void Cache::configure(std::string cass_hostname,
                      uint16_t cass_port)
{
  _cass_host = cass_hostname;
  _cass_port = cass_port;
}

Cache::ResultCode Cache::start()
{
  // Check connectivity to cassandra. Return an error code if there is a
  // problem.
  //
  // Initialize the thread pool.

  return ResultCode::OK;
}

void Cache::stop()
{
  // Stop the thread pool.
}

void Cache::wait_stopped()
{
  // Join the thread pool.
}

CassandraClient* Cache::_get_client()
{
  // Get client out of thread-local data.
  // If not found create one and store in thread local data.
  // Return client.
  return NULL;
}

void Cache::_release_client()
{
  // Get client out of thread-local data. Delete it. Write a NULL back to
  // thread-local data.
}

void Cache::_queue_request(Request *request)
{
  // Add the request to the cache's thread pool.
}










void Cache::Request::send(Cache *cache)
{
  // Push the request onto the cache's thread pool.
}









void Cache::PutRequest::
_put_columns(std::vector<std::string>& keys,
             std::map<std::string, std::string>& columns,
             int64_t timestamp,
             int32_t ttl)
{
  // Create a mutation for each column.
  // Create a vector of mutations (m).
  // Create a mutation map (key1 => m, key2 => m, ...)
  // Call _get_client()->batch_mutate().
  //
  // Catch thrift exceptions and convert them into on_error calls.
}








void Cache::GetRequest::
_ha_get_row(std::string& key,
            std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_row with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
_ha_get_columns(std::string& key,
                std::vector<std::string>& names,
                std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_columns with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
_ha_get_columns_with_prefix(std::string& key,
                            std::string& prefix,
                            std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_columns_with_prefix with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
_get_row(std::string& key,
         std::vector<ColumnOrSuperColumn>& columns,
         ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets all columns.
  // _issue_get_for_key()
}

void Cache::GetRequest::
_get_columns(std::string& key,
             std::vector<std::string>& names,
             std::vector<ColumnOrSuperColumn>& columns,
             ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets the specified columns.
  // _issue_get_for_key()
}

void Cache::GetRequest::
_get_columns_with_prefix(std::string& key,
                         std::string& prefix,
                         std::vector<ColumnOrSuperColumn>& columns,
                         ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets columns that have the specified prefix.
  // Fill in the `columns` vector with the results.
  // Strip the prefix off the resulting columns.
}

void Cache::GetRequest::
_issue_get_for_key(std::string& key,
                   SlicePredicate& predicate,
                   std::vector<ColumnOrSuperColumn>& columns)
{
  // Create a keyrange specifying the key.
  // Call _get_client()->get_range_slices()
  // Output the resulting columns vector.
}













void Cache::DeleteRowsRequest::
_delete_row(std::string& key,
            int64_t timestamp)
{
  // Create a ColumnPath specifying all columns.
  // Call _get_client->remove()
}
















void Cache::PutIMSSubscription::_process()
{
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::PutAssociatedPublicID::_process()
{
  // Add a assoc_public_ prefix to the public ID.
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::PutAuthVector::_process()
{
  // Convert the DigestAuthVector to a map of columns names => values.
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetIMSSubscription::_process()
{
  // _ha_get_column()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetAssociatedPublicIDs::_process()
{
  // _ha_get_columns_with_prefix()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetAuthVector::_process()
{
  // _ha_get_columns()
  // Construct a DigestAuthVector from the values returned (error if some are
  // missing).
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}

void Cache::DeletePublicIDs::_process()
{
  // _delete_row()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}

void Cache::DeletePrivateIDs::_process()
{
  // _delete_row()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}





void Cache::CacheThreadPool::_process_work(Request *request)
{
  // Call request->_process()
  // Catch unhandled exceptions and log them.
}

void Cache::CacheThreadPool::_on_thread_shutdown()
{
  // Call _release_client().
}
