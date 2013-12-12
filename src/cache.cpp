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

std::string KEYSPACE = "homestead_cache";
std::string IMPI = "impi";
std::string IMPU = "impu";

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


Cache::CacheThreadPool::~CacheThreadPool()
{}


void Cache::CacheThreadPool::process_work(Request* &request)
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


Cache::Request::Request(std::string& column_family) :
  _column_family(column_family)
{}


Cache::Request::~Request()
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
    error_text = (boost::format("Exception: %s [%d]\n")
                  % te.what() % te.getType()).str();
  }
  catch(InvalidRequestException ire)
  {
    rc = ResultCode::INVALID_REQUEST;
    error_text = (boost::format("Exception: %s [%s]\n")
                  % ire.what() % ire.why.c_str()).str();
  }
  catch(NotFoundException nfe)
  {
    rc = ResultCode::NOT_FOUND;
    error_text = (boost::format("Exception: %s\n")
                  % nfe.what()).str();
  }

  if (rc != ResultCode::OK)
  {
    on_failure(rc, error_text);
  }
}

//
// ModificationRequest methods.
//

Cache::ModificationRequest::ModificationRequest(std::string& column_family,
                                                int64_t timestamp) :
  Cache::Request(column_family),
  _timestamp(timestamp)
{}


Cache::ModificationRequest::~ModificationRequest()
{}

//
// PutRequest methods
//

Cache::PutRequest::PutRequest(std::string& column_family,
                              int64_t timestamp,
                              int32_t ttl) :
  Cache::ModificationRequest(column_family, timestamp),
  _ttl(ttl)
{}


Cache::PutRequest::~PutRequest()
{}


void Cache::PutRequest::
put_columns(Cache::CacheClient* client,
            std::string& column_family,
            std::vector<std::string>& keys,
            std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl)
{
  std::vector<Mutation> mutations;
  std::map<std::string, std::map<std::string, std::vector<Mutation>>> mutmap;

  for (std::map<std::string, std::string>::iterator it = columns.begin();
       it != columns.end();
       ++it)
  {
    Mutation mutation;
    Column* column = &mutation.column_or_supercolumn.column;

    column->name = it->first;
    column->value = it->second;
    column->__isset.value = true;
    column->timestamp = timestamp;
    column->__isset.timestamp = true;
    column->ttl = ttl;
    column->__isset.ttl = true;

    mutation.column_or_supercolumn.__isset.column = true;
    mutation.__isset.column_or_supercolumn = true;
    mutations.push_back(mutation);
  }

  for (std::vector<std::string>::const_iterator it = keys.begin();
       it != keys.end();
       ++it)
  {
    mutmap[*it][column_family] = mutations;
  }

  client->batch_mutate(mutmap, ConsistencyLevel::ONE);
}

//
// GetRequest methods.
//

Cache::GetRequest::GetRequest(std::string& column_family) :
  Request(column_family)
{}


Cache::GetRequest::~GetRequest()
{}


#define HA(METHOD, ...)                                                      \
        try                                                                  \
        {                                                                    \
          METHOD(__VA_ARGS__, ConsistencyLevel::ONE);                        \
        }                                                                    \
        catch(...)                                                           \
        {                                                                    \
          LOG_DEBUG("Failed ONE read for %s. Try QUORUM");                   \
          METHOD(__VA_ARGS__, ConsistencyLevel::QUORUM);                     \
        }


void Cache::GetRequest::
ha_get_row(Cache::CacheClient* client,
           std::string& column_family,
           std::string& key,
           std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_row, client, column_family, key, columns);
}


void Cache::GetRequest::
ha_get_columns(Cache::CacheClient* client,
               std::string& column_family,
               std::string& key,
               std::vector<std::string>& names,
               std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_columns, client, column_family, key, names, columns);
}


void Cache::GetRequest::
ha_get_columns_with_prefix(Cache::CacheClient* client,
                           std::string& column_family,
                           std::string& key,
                           std::string& prefix,
                           std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_columns_with_prefix, client, column_family, key, prefix, columns);
}


void Cache::GetRequest::
get_row(Cache::CacheClient* client,
        std::string& column_family,
        std::string& key,
        std::vector<ColumnOrSuperColumn>& columns,
        ConsistencyLevel::type consistency_level)
{
  SliceRange sr;
  sr.start = "";
  sr.finish = "";

  SlicePredicate sp;
  sp.slice_range = sr;
  sp.__isset.slice_range = true;

  issue_get_for_key(client, column_family, key, sp, columns, consistency_level);
}


void Cache::GetRequest::
get_columns(Cache::CacheClient* client,
            std::string& column_family,
            std::string& key,
            std::vector<std::string>& names,
            std::vector<ColumnOrSuperColumn>& columns,
            ConsistencyLevel::type consistency_level)
{
  SlicePredicate sp;
  sp.column_names = names;
  sp.__isset.column_names = true;

  issue_get_for_key(client, column_family, key, sp, columns, consistency_level);
}


void Cache::GetRequest::
get_columns_with_prefix(Cache::CacheClient* client,
                        std::string& column_family,
                        std::string& key,
                        std::string& prefix,
                        std::vector<ColumnOrSuperColumn>& columns,
                        ConsistencyLevel::type consistency_level)
{
  SliceRange sr;
  sr.start = prefix;
  sr.finish = prefix + "\xFF";

  SlicePredicate sp;
  sp.slice_range = sr;
  sp.__isset.slice_range = true;

  issue_get_for_key(client, column_family, key, sp, columns, consistency_level);

  for (std::vector<ColumnOrSuperColumn>::iterator it = columns.begin();
       it != columns.end();
       ++it)
  {
    it->column.name = it->column.name.substr(prefix.length());
  }
}


void Cache::GetRequest::
issue_get_for_key(Cache::CacheClient* client,
                  std::string& column_family,
                  std::string& key,
                  SlicePredicate& predicate,
                  std::vector<ColumnOrSuperColumn>& columns,
                  ConsistencyLevel::type consistency_level)
{
  ColumnParent cparent;
  cparent.column_family = column_family;

  KeyRange range;
  range.start_key = key;
  range.end_key = "";
  range.__isset.start_key = true;
  range.__isset.end_key = true;

  std::vector<KeySlice> results;
  client->get_range_slices(results, cparent, predicate, range, consistency_level);

  columns = results[0].columns;
}

//
// DeleteRowsRequest methods
//

Cache::DeleteRowsRequest::DeleteRowsRequest(std::string& column_family,
                                            int64_t timestamp) :
  ModificationRequest(column_family, timestamp)
{}


Cache::DeleteRowsRequest::~DeleteRowsRequest()
{}


void Cache::DeleteRowsRequest::
delete_row(Cache::CacheClient* client,
           std::string& column_family,
           std::string& key,
           int64_t timestamp)
{
  ColumnPath cp;
  cp.column_family = column_family;
  client->remove(key, cp, timestamp, ConsistencyLevel::ONE);
}

//
// PutIMSSubscription methods.
//

Cache::PutIMSSubscription::
PutIMSSubscription(std::string& public_id,
                   std::string& xml,
                   int64_t timestamp,
                   int32_t ttl) :
  PutRequest(IMPU, timestamp, ttl),
  _public_ids(1, public_id),
  _xml(xml)
{}

Cache::PutIMSSubscription::
PutIMSSubscription(std::vector<std::string>& public_ids,
                   std::string& xml,
                   int64_t timestamp,
                   int32_t ttl) :
  PutRequest(IMPU, timestamp, ttl),
  _public_ids(public_ids),
  _xml(xml)
{}

Cache::PutIMSSubscription::
~PutIMSSubscription()
{}

std::string IMS_SUB_XML_COLUMN_NAME = "ims_subscription_xml";

void Cache::PutIMSSubscription::perform(Cache::CacheClient* client)
{
  std::map<std::string, std::string> columns;
  columns[IMS_SUB_XML_COLUMN_NAME] = _xml;

  put_columns(client, _column_family, _public_ids, columns, _timestamp, _ttl);
  on_success();
}

//
// PutAssociatedPublicID methods.
//

std::string ASSOC_PUBLIC_ID_COLUMN_PREFIX = "public_id_";

Cache::PutAssociatedPublicID::
PutAssociatedPublicID(std::string& private_id,
                      std::string& assoc_public_id,
                      int64_t timestamp,
                      int32_t ttl) :
  PutRequest(IMPI, timestamp, ttl),
  _private_id(private_id),
  _assoc_public_id(assoc_public_id)
{}

Cache::PutAssociatedPublicID::
~PutAssociatedPublicID()
{}


void Cache::PutAssociatedPublicID::perform(Cache::CacheClient* client)
{
  std::map<std::string, std::string> columns;
  columns[ASSOC_PUBLIC_ID_COLUMN_PREFIX + _assoc_public_id] = "";

  std::vector<std::string> keys(1, _private_id);

  put_columns(client, _column_family, keys, columns, _timestamp, _ttl);
  on_success();
}




Cache::PutAuthVector::
PutAuthVector(std::string& private_id,
              DigestAuthVector& auth_vector,
              int64_t timestamp,
              int32_t ttl) :
  PutRequest(IMPI, timestamp),
  _private_ids(1, private_id),
  _auth_vector(auth_vector)
{}


Cache::PutAuthVector::
~PutAuthVector()
{}


std::string DIGEST_HA1_COLUMN_NAME      ="digest_ha1";
std::string DIGEST_REALM_COLUMN_NAME    = "digest_realm";
std::string DIGEST_QOP_COLUMN_NAME      = "digest_qop";
std::string KNOWN_PREFERRED_COLUMN_NAME = "known_preferred";

void Cache::PutAuthVector::perform(Cache::CacheClient* client)
{
  std::map<std::string, std::string> columns;
  columns[DIGEST_HA1_COLUMN_NAME]      = _auth_vector.ha1;
  columns[DIGEST_REALM_COLUMN_NAME]    = _auth_vector.realm;
  columns[DIGEST_QOP_COLUMN_NAME]      = _auth_vector.qop;
  columns[KNOWN_PREFERRED_COLUMN_NAME] = _auth_vector.preferred ? "\x01" : "\x00";

  put_columns(client, _column_family, _private_ids, columns, _timestamp, _ttl);

  on_success();
}


//
// GetIMSSubscription methods
//

Cache::GetIMSSubscription::
GetIMSSubscription(std::string& public_id) :
  GetRequest(IMPU),
  _public_id(public_id)
{}


Cache::GetIMSSubscription::
~GetIMSSubscription()
{}


void Cache::GetIMSSubscription::perform(Cache::CacheClient* client)
{
  std::vector<ColumnOrSuperColumn> results;
  std::vector<std::string> requested_columns(1, IMS_SUB_XML_COLUMN_NAME);

  ha_get_columns(client, IMPU, _public_id, requested_columns, results);

  on_success(results[0].column.value);
}







Cache::GetAssociatedPublicIDs::
GetAssociatedPublicIDs(std::string& private_id) :
  GetRequest(IMPI),
  _private_id(private_id)
{}


Cache::GetAssociatedPublicIDs::
~GetAssociatedPublicIDs()
{}


void Cache::GetAssociatedPublicIDs::perform(Cache::CacheClient* client)
{
  std::vector<ColumnOrSuperColumn> columns;
  std::vector<std::string> results;

  ha_get_columns_with_prefix(client,
                             _column_family,
                             _private_id,
                             ASSOC_PUBLIC_ID_COLUMN_PREFIX,
                             columns);

  for(std::vector<ColumnOrSuperColumn>::const_iterator it = columns.begin();
      it != columns.end();
      ++it)
  {
    results.push_back(it->column.value);
  }

  on_success(results);
}






Cache::GetAuthVector::
GetAuthVector(std::string& private_id) :
  GetRequest(IMPI),
  _private_id(private_id),
  _public_id("")
{}


Cache::GetAuthVector::
GetAuthVector(std::string& private_id,
              std::string& public_id) :
  GetRequest(IMPI),
  _private_id(private_id),
  _public_id(public_id)
{}


Cache::GetAuthVector::
~GetAuthVector()
{}



void Cache::GetAuthVector::perform(Cache::CacheClient* client)
{
  std::vector<std::string> requested_columns;
  std::string requested_public_id_col = "";

  requested_columns.push_back(DIGEST_HA1_COLUMN_NAME);
  requested_columns.push_back(DIGEST_REALM_COLUMN_NAME);
  requested_columns.push_back(DIGEST_QOP_COLUMN_NAME);
  requested_columns.push_back(KNOWN_PREFERRED_COLUMN_NAME);

  if (_public_id.length() > 0)
  {
    requested_public_id_col = ASSOC_PUBLIC_ID_COLUMN_PREFIX + _public_id;
    requested_columns.push_back(requested_public_id_col);
  }

  std::vector<ColumnOrSuperColumn> results;
  ha_get_columns(client, IMPI, _private_id, requested_columns, results);

  DigestAuthVector av;

  for (std::vector<ColumnOrSuperColumn>::const_iterator it = results.begin();
       it != results.end();
       ++it)
  {
    const Column *col = &it->column;

    if (col->name == DIGEST_HA1_COLUMN_NAME)
    {
      av.ha1 = col->value;
    }
    else if (col->name == DIGEST_REALM_COLUMN_NAME)
    {
      av.realm = col->value;
    }
    else if (col->name == DIGEST_QOP_COLUMN_NAME)
    {
      av.qop = col->value;
    }
    else if (col->name == KNOWN_PREFERRED_COLUMN_NAME)
    {
      av.preferred = (col->value == "\x01");
    }
    else if (col->name == requested_public_id_col)
    {
      requested_public_id_col = "";
    }
  }

  if (requested_public_id_col.length() == 0)
  {
    on_success(av);
  }
  else
  {
    std::string error_text = (boost::format(
        "Private ID '%s' exists but does not have associated public ID '%s'")
        % _private_id, _public_id);
    on_failure(ResultCode::NOT_FOUND, error_text);
  }
}


Cache::DeletePublicIDs::
DeletePublicIDs(std::string& public_id, int64_t timestamp) :
  DeleteRowsRequest(IMPU, timestamp),
  _public_ids(1, public_id)
{}


Cache::DeletePublicIDs::
DeletePublicIDs(std::vector<std::string>& public_ids, int64_t timestamp) :
  DeleteRowsRequest(IMPU, timestamp),
  _public_ids(public_ids)
{}


Cache::DeletePublicIDs::
~DeletePublicIDs()
{}

void Cache::DeletePublicIDs::perform(Cache::CacheClient* client)
{
  for (std::vector<std::string>::iterator it = _public_ids.begin();
       it != _public_ids.end();
       ++it)
  {
    delete_row(client, _column_family, *it, _timestamp);
  }

  on_success();
}



Cache::DeletePrivateIDs::
DeletePrivateIDs(std::string& private_id, int64_t timestamp) :
  DeleteRowsRequest(IMPI, timestamp),
  _private_ids(1, private_id)
{}


Cache::DeletePrivateIDs::
DeletePrivateIDs(std::vector<std::string>& private_ids, int64_t timestamp) :
  DeleteRowsRequest(IMPI, timestamp),
  _private_ids(private_ids)
{}


Cache::DeletePrivateIDs::
~DeletePrivateIDs()
{}

void Cache::DeletePrivateIDs::perform(Cache::CacheClient* client)
{
  for (std::vector<std::string>::iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    delete_row(client, _column_family, *it, _timestamp);
  }

  on_success();
}
