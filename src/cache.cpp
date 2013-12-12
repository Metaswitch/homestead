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
#include <boost/format.hpp>

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;
using namespace org::apache::cassandra;

Cache* Cache::INSTANCE = &DEFAULT_INSTANCE;
Cache Cache::DEFAULT_INSTANCE;
const std::string Cache::KEYSPACE = "homestead_cache";

//
// Cache methods
//

Cache::Cache() :
  _cass_hostname(""),
  _cass_port(0),
  _num_threads(0),
  _max_queue(0),
  _thread_pool(NULL),
  _thread_local()
{
  // Create the thread-local storage that stores cassandra connections.
  // delete_client is a destroy callback that is called when the thread exits.
  pthread_key_create(&_thread_local, delete_client);
}


void Cache::initialize()
{
  // There is nothing ti initialize - this is intentionally a no-op.
}


void Cache::configure(std::string cass_hostname,
                      uint16_t cass_port,
                      unsigned int num_threads,
                      unsigned int max_queue)
{
  _cass_hostname = cass_hostname;
  _cass_port = cass_port;
  _num_threads = num_threads;
  _max_queue = max_queue;
}


Cache::ResultCode Cache::start()
{
  ResultCode rc = ResultCode::OK;

  // Check that we can connect to cassandra by getting a client. This logs in
  // and switches to the cache keyspace, so is a good test of whether cassandra
  // is working properly.
  try
  {
    get_client();
    release_client();
  }
  catch(TTransportException te)
  {
    rc = ResultCode::CONNECTION_ERROR;
  }
  catch(NotFoundException nfe)
  {
    rc = ResultCode::NOT_FOUND;
  }
  catch(...)
  {
    rc = ResultCode::UNKNOWN_ERROR;
  }

  // Start the thread pool.
  if (rc == ResultCode::OK)
  {
    _thread_pool = new CacheThreadPool(this, _num_threads, _max_queue);

    if (!_thread_pool->start())
    {
      rc = ResultCode::RESOURCE_ERROR;
    }
  }

  return rc;
}


void Cache::stop()
{
  if (_thread_pool != NULL)
  {
    _thread_pool->stop();
  }
}


void Cache::wait_stopped()
{
  if (_thread_pool != NULL)
  {
    _thread_pool->join();

    delete _thread_pool;
    _thread_pool = NULL;
  }
}


Cache::~Cache()
{
  // It is only safe to destroy the cache once the thread pool has been deleted
  // (as the poool stores a pointer to the cache). Make sure this is the case.
  stop();
  wait_stopped();
}


Cache::CacheClient* Cache::get_client()
{
  // See if we've already got a client for this thread.  If not allocate a new
  // one and write it back into thread-local storage.
  Cache::CacheClient* client = (Cache::CacheClient*)pthread_getspecific(_thread_local);

  if (client == NULL)
  {
    boost::shared_ptr<TTransport> socket =
        boost::shared_ptr<TSocket>(new TSocket(_cass_hostname, _cass_port));
    boost::shared_ptr<TFramedTransport> transport =
        boost::shared_ptr<TFramedTransport>(new TFramedTransport(socket));
    boost::shared_ptr<TProtocol> protocol =
        boost::shared_ptr<TBinaryProtocol>(new TBinaryProtocol(transport));
    client = new Cache::CacheClient(protocol, transport);
    client->set_keyspace(KEYSPACE);
    pthread_setspecific(_thread_local, client);
  }

  return client;
}


void Cache::release_client()
{
  // If this thread already has a client delete it and remove it from
  // thread-local storage.
  Cache::CacheClient* client = (Cache::CacheClient*)pthread_getspecific(_thread_local);

  if (client != NULL)
  {
    delete_client(client); client = NULL;
    pthread_setspecific(_thread_local, NULL);
  }
}


void Cache::delete_client(void *client)
{
  delete (Cache::CacheClient *)client; client = NULL;
}


void Cache::send(Request *request)
{
  _thread_pool->add_work(request);
  request = NULL;
}


//
// CacheThreadPool methods
//

Cache::CacheThreadPool::CacheThreadPool(Cache *cache,
                                        unsigned int num_threads,
                                        unsigned int max_queue) :
  ThreadPool<Cache::Request *>(num_threads, max_queue),
  _cache(cache)
{}


void Cache::CacheThreadPool::process_work(Request *request)
{
  // Run the request.  Catch all exceptions to stop an error from killing the
  // worker thread.
  try
  {
    request->run(_cache->get_client());
  }
  catch(...)
  {
    LOG_ERROR("Unhandled exception when processing cache request");
  }

  // We own the request so we have to free it.
  delete request; request = NULL;
}

//
// Request methods
//

Cache::Request::Request(std::string& table) :
  _table(table)
{}

void Cache::Request::run(Cache::CacheClient *client)
{
  ResultCode rc = ResultCode::OK;
  std::string error_text = "";

  try
  {
    perform(client);
  }
  catch(TTransportException te)
  {
    rc = ResultCode::CONNECTION_ERROR;
    error_text = (boost::format("Exception: %s [%d]\n") % te.what() % te.getType()).str();
  }
  catch(InvalidRequestException ire)
  {
    rc = ResultCode::INVALID_REQUEST;
    error_text = (boost::format("Exception: %s [%s]\n") % ire.what() % ire.why.c_str()).str();
  }
  catch(NotFoundException nfe)
  {
    rc = ResultCode::NOT_FOUND;
    error_text = (boost::format("Exception: %s\n") % nfe.what()).str();
  }

  if (rc != ResultCode::OK)
  {
    on_failure(rc, error_text);
  }
}















void Cache::PutRequest::
put_columns(Cache::CacheClient* client,
            std::vector<std::string>& keys,
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
ha_get_row(Cache::CacheClient* client,
           std::string& key,
           std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_row with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
ha_get_columns(Cache::CacheClient* client,
               std::string& key,
               std::vector<std::string>& names,
               std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_columns with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
ha_get_columns_with_prefix(Cache::CacheClient* client,
                           std::string& key,
                           std::string& prefix,
                           std::vector<ColumnOrSuperColumn>& columns)
{
  // Call _get_columns_with_prefix with a consistency level of ONE.
  // If this throws an NotFound exception, retry with QUORUM.
}

void Cache::GetRequest::
get_row(Cache::CacheClient* client,
        std::string& key,
        std::vector<ColumnOrSuperColumn>& columns,
        ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets all columns.
  // _issue_get_for_key()
}

void Cache::GetRequest::
get_columns(Cache::CacheClient* client,
            std::string& key,
            std::vector<std::string>& names,
            std::vector<ColumnOrSuperColumn>& columns,
            ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets the specified columns.
  // _issue_get_for_key()
}

void Cache::GetRequest::
get_columns_with_prefix(Cache::CacheClient* client,
                        std::string& key,
                        std::string& prefix,
                        std::vector<ColumnOrSuperColumn>& columns,
                        ConsistencyLevel consistency_level)
{
  // Create a slice prediciate that gets columns that have the specified prefix.
  // Fill in the `columns` vector with the results.
  // Strip the prefix off the resulting columns.
}

void Cache::GetRequest::
issue_get_for_key(Cache::CacheClient* client,
                  std::string& key,
                  SlicePredicate& predicate,
                  std::vector<ColumnOrSuperColumn>& columns)
{
  // Create a keyrange specifying the key.
  // Call _get_client()->get_range_slices()
  // Output the resulting columns vector.
}













void Cache::DeleteRowsRequest::
delete_row(Cache::CacheClient* client,
           std::string& key,
           int64_t timestamp)
{
  // Create a ColumnPath specifying all columns.
  // Call _get_client->remove()
}
















void Cache::PutIMSSubscription::perform(Cache::CacheClient* client)
{
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::PutAssociatedPublicID::perform(Cache::CacheClient* client)
{
  // Add a assoc_public_ prefix to the public ID.
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::PutAuthVector::perform(Cache::CacheClient* client)
{
  // Convert the DigestAuthVector to a map of columns names => values.
  // _put_columns()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetIMSSubscription::perform(Cache::CacheClient* client)
{
  // _ha_get_column()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetAssociatedPublicIDs::perform(Cache::CacheClient* client)
{
  // _ha_get_columns_with_prefix()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}


void Cache::GetAuthVector::perform(Cache::CacheClient* client)
{
  // _ha_get_columns()
  // Construct a DigestAuthVector from the values returned (error if some are
  // missing).
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}

void Cache::DeletePublicIDs::perform(Cache::CacheClient* client)
{
  // _delete_row()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}

void Cache::DeletePrivateIDs::perform(Cache::CacheClient* client)
{
  // _delete_row()
  // Catch thrift exceptions and convert to error codes.
  // Call on_success or on_falure as appropriate.
}



