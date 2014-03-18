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

#include "cache.h"

#include <boost/format.hpp>
#include <time.h>

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;
using namespace org::apache::cassandra;

// Variables to store the singleton cache object.
Cache* Cache::INSTANCE = &DEFAULT_INSTANCE;
Cache Cache::DEFAULT_INSTANCE;

// Keyspace and column family names.
std::string KEYSPACE = "homestead_cache";
std::string IMPI = "impi";
std::string IMPI_MAPPING = "impi_mapping";
std::string IMPU = "impu";

// Column names in the IMPU column family.
std::string IMS_SUB_XML_COLUMN_NAME = "ims_subscription_xml";
std::string REG_STATE_COLUMN_NAME = "is_registered";
std::string IMPI_COLUMN_PREFIX = "associated_impi__";
std::string IMPI_MAPPING_PREFIX = "associated_primary_impu__";

// Column names in the IMPI column family.
std::string ASSOC_PUBLIC_ID_COLUMN_PREFIX = "public_id_";
std::string DIGEST_HA1_COLUMN_NAME      ="digest_ha1";
std::string DIGEST_REALM_COLUMN_NAME    = "digest_realm";
std::string DIGEST_QOP_COLUMN_NAME      = "digest_qop";
std::string KNOWN_PREFERRED_COLUMN_NAME = "known_preferred";

const std::string BOOLEAN_FALSE = std::string("\0", 1);
const std::string BOOLEAN_TRUE = std::string("\x01", 1);

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
  LOG_STATUS("Configuring cache");
  LOG_STATUS("  Hostname:  %s", cass_hostname.c_str());
  LOG_STATUS("  Port:      %u", cass_port);
  LOG_STATUS("  Threads:   %u", num_threads);
  LOG_STATUS("  Max Queue: %u", max_queue);
  _cass_hostname = cass_hostname;
  _cass_port = cass_port;
  _num_threads = num_threads;
  _max_queue = max_queue;
}


Cache::ResultCode Cache::start()
{
  ResultCode rc = OK;

  // Check that we can connect to cassandra by getting a client. This logs in
  // and switches to the cache keyspace, so is a good test of whether cassandra
  // is working properly.
  LOG_STATUS("Starting cache");
  try
  {
    get_client();
    release_client();
  }
  catch(TTransportException te)
  {
    LOG_ERROR("Cache caught TTransportException: %s", te.what());
    rc = CONNECTION_ERROR;
  }
  catch(NotFoundException nfe)
  {
    LOG_ERROR("Cache caught NotFoundException: %s", nfe.what());
    rc = NOT_FOUND;
  }
  catch(...)
  {
    LOG_ERROR("Cache caught unknown exception!");
    rc = UNKNOWN_ERROR;
  }

  // Start the thread pool.
  if (rc == OK)
  {
    _thread_pool = new CacheThreadPool(this, _num_threads, _max_queue);

    if (!_thread_pool->start())
    {
      rc = RESOURCE_ERROR; // LCOV_EXCL_LINE
    }
  }

  return rc;
}


void Cache::stop()
{
  LOG_STATUS("Stopping cache");
  if (_thread_pool != NULL)
  {
    _thread_pool->stop();
  }
}


void Cache::wait_stopped()
{
  LOG_STATUS("Waiting for cache to stop");
  if (_thread_pool != NULL)
  {
    _thread_pool->join();

    delete _thread_pool;
    _thread_pool = NULL;
  }
}


int64_t Cache::generate_timestamp()
{
  // Return the current time in microseconds.
  timespec clock_time;
  int64_t timestamp;

  clock_gettime(CLOCK_REALTIME, &clock_time);
  timestamp = clock_time.tv_sec;
  timestamp *= 1000000;
  timestamp += (clock_time.tv_nsec / 1000);

  LOG_DEBUG("Generated Cassandra timestamp %llu", timestamp);
  return timestamp;
}


Cache::~Cache()
{
  // It is only safe to destroy the cache once the thread pool has been deleted
  // (as the pool stores a pointer to the cache). Make sure this is the case.
  stop();
  wait_stopped();
}


// LCOV_EXCL_START - UTs do not cover relationship of clients to threads.
Cache::CacheClientInterface* Cache::get_client()
{
  // See if we've already got a client for this thread.  If not allocate a new
  // one and write it back into thread-local storage.
  LOG_DEBUG("Getting thread-local CacheClientInterface");
  Cache::CacheClientInterface* client = (Cache::CacheClientInterface*)pthread_getspecific(_thread_local);

  if (client == NULL)
  {
    LOG_DEBUG("No thread-local CacheClientInterface - creating one");
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
  LOG_DEBUG("Looking to release thread-local CacheClientInterface");
  Cache::CacheClientInterface* client = (Cache::CacheClientInterface*)pthread_getspecific(_thread_local);

  if (client != NULL)
  {
    LOG_DEBUG("Found thread-local CacheClientInterface - destroying");
    delete_client(client);
    client = NULL;
    pthread_setspecific(_thread_local, NULL);
  }
}


void Cache::delete_client(void* client)
{
  delete (Cache::CacheClientInterface*)client;
  client = NULL;
}
// LCOV_EXCL_STOP


void Cache::send(Cache::Transaction* trx, Cache::Request* req)
{
  req->set_trx(trx);
  _thread_pool->add_work(req);
  req = NULL;
}


//
// CacheThreadPool methods
//

Cache::CacheThreadPool::CacheThreadPool(Cache* cache,
                                        unsigned int num_threads,
                                        unsigned int max_queue) :
  ThreadPool<Cache::Request*>(num_threads, max_queue),
  _cache(cache)
{}


Cache::CacheThreadPool::~CacheThreadPool()
{}


void Cache::CacheThreadPool::process_work(Request* &req)
{
  // Run the request.  Catch all exceptions to stop an error from killing the
  // worker thread.
  try
  {
    req->run(_cache->get_client());
  }
  // LCOV_EXCL_START Transaction catches all exceptions so the thread pool
  // fallback code is never triggered.
  catch(...)
  {
    LOG_ERROR("Unhandled exception when processing cache request");
  }
  // LCOV_EXCL_STOP

  // We own the request so we have to free it.
  delete req;
  req = NULL;
}

//
// Request methods
//

Cache::Request::Request(const std::string& column_family) :
  _column_family(column_family),
  _trx(NULL)
{}


Cache::Request::~Request()
{
  delete _trx;
  _trx = NULL;
}


void Cache::Request::run(Cache::CacheClientInterface* client)
{
  ResultCode rc = OK;
  std::string error_text = "";

  // Store the client and transaction pointer so it is available to subclasses
  // that override perform().
  _client = client;

  // Call perform() to actually do the business logic of the request.  Catch
  // exceptions and turn them into return codes and error text.
  try
  {
    perform();
  }
  catch(TTransportException& te)
  {
    rc = CONNECTION_ERROR;
    error_text = (boost::format("Exception: %s [%d]\n")
                  % te.what() % te.getType()).str();
  }
  catch(InvalidRequestException& ire)
  {
    rc = INVALID_REQUEST;
    error_text = (boost::format("Exception: %s [%s]\n")
                  % ire.what() % ire.why.c_str()).str();
  }
  catch(NotFoundException& nfe)
  {
    rc = NOT_FOUND;
    error_text = (boost::format("Exception: %s\n")
                  % nfe.what()).str();
  }
  catch(Cache::NoResultsException& nre)
  {
    rc = NOT_FOUND;
    error_text = (boost::format("Row %s not present in column_family %s\n")
                  % nre.get_key() % nre.get_column_family()).str();
  }
  catch(...)
  {
    rc = UNKNOWN_ERROR;
    error_text = "Unknown error";
  }

  if (rc != OK)
  {
    // Caught an exception so:
    // - Stop the transaction duration timer (it might not have been stopped
    // yet).
    // - Notify the user of the error.
    LOG_ERROR("Cache request failed: rc=%d, %s", rc, error_text.c_str());
    _trx->stop_timer();
    _trx->on_failure(this, rc, error_text);
  }
}

//
// ModificationRequest methods.
//

Cache::ModificationRequest::ModificationRequest(const std::string& column_family,
                                                int64_t timestamp) :
  Cache::Request(column_family),
  _timestamp(timestamp)
{}


Cache::ModificationRequest::~ModificationRequest()
{}

//
// PutRequest methods
//

Cache::PutRequest::PutRequest(const std::string& column_family,
                              int64_t timestamp,
                              int32_t ttl) :
  Cache::ModificationRequest(column_family, timestamp),
  _ttl(ttl)
{}


Cache::PutRequest::~PutRequest()
{}


void Cache::PutRequest::
put_columns(const std::vector<std::string>& keys,
            const std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl)
{
  // Vector of mutations (one per column being modified).
  std::vector<Mutation> mutations;

  // The mutation map is of the form {"key": {"column_family": [mutations] } }
  std::map<std::string, std::map<std::string, std::vector<Mutation> > > mutmap;

  // Populate the mutations vector.
  LOG_DEBUG("Constructing cache put request with timestamp %lld and per-column TTLs", timestamp);
  for (std::map<std::string, std::string>::const_iterator it = columns.begin();
       it != columns.end();
       ++it)
  {
    Mutation mutation;
    Column* column = &mutation.column_or_supercolumn.column;

    column->name = it->first;
    column->value = it->second;
    LOG_DEBUG("  %s => %s (TTL %d)", column->name.c_str(), column->value.c_str(), ttl);
    column->__isset.value = true;
    column->timestamp = timestamp;
    column->__isset.timestamp = true;

    // A ttl of 0 => no expiry.
    if (ttl > 0)
    {
      column->ttl = ttl;
      column->__isset.ttl = true;
    }

    mutation.column_or_supercolumn.__isset.column = true;
    mutation.__isset.column_or_supercolumn = true;
    mutations.push_back(mutation);
  }

  // Update the mutation map.
  for (std::vector<std::string>::const_iterator it = keys.begin();
       it != keys.end();
       ++it)
  {
    mutmap[*it][_column_family] = mutations;
  }

  // Execute the database operation.
  LOG_DEBUG("Executing put request operation");
  _trx->start_timer();
  _client->batch_mutate(mutmap, ConsistencyLevel::ONE);
  _trx->stop_timer();
}

void Cache::PutRequest::
put_columns_to_multiple_cfs(const std::vector<CFRowColumnValue>& to_put,
                            int64_t timestamp,
                            int32_t ttl)
{
  // The mutation map is of the form {"key": {"column_family": [mutations] } }
  std::map<std::string, std::map<std::string, std::vector<Mutation> > > mutmap;

  // Populate the mutations vector.
  LOG_DEBUG("Constructing cache put request with timestamp %lld and per-column TTLs", timestamp);
  for (std::vector<CFRowColumnValue>::const_iterator it = to_put.begin();
       it != to_put.end();
       ++it)
  {
    // Vector of mutations (one per column being modified).
    std::vector<Mutation> mutations;

    for (std::map<std::string, std::string>::const_iterator col = it->columns.begin();
         col != it->columns.end();
         ++col)
    {
      Mutation mutation;
      Column* column = &mutation.column_or_supercolumn.column;

      column->name = col->first;
      column->value = col->second;
      LOG_DEBUG("  %s => %s (TTL %d)", column->name.c_str(), column->value.c_str(), ttl);
      column->__isset.value = true;
      column->timestamp = timestamp;
      column->__isset.timestamp = true;

      // A ttl of 0 => no expiry.
      if (ttl > 0)
      {
        column->ttl = ttl;
        column->__isset.ttl = true;
      }

      mutation.column_or_supercolumn.__isset.column = true;
      mutation.__isset.column_or_supercolumn = true;
      mutations.push_back(mutation);
    }

    mutmap[it->row][it->cf] = mutations;
  }

  // Execute the database operation.
  LOG_DEBUG("Executing put request operation");
  _trx->start_timer();
  _client->batch_mutate(mutmap, ConsistencyLevel::ONE);
  _trx->stop_timer();
}

//
// GetRequest methods.
//

Cache::GetRequest::GetRequest(const std::string& column_family) :
  Request(column_family)
{}


Cache::GetRequest::~GetRequest()
{}

// Macro to turn an underlying (non-HA) get method into an HA one.
//
// This macro takes the following arguments:
// -  The name of the underlying get method to call.
// -  The arguments for the underlying get method.
//
// It works as follows:
// -  Call the underlying method with a consistency level of ONE.
// -  If this raises a NotFoundException, try again with a consistency level of
//    QUORUM.
// -  If this fails again with either NotFoundException or UnavailableException
//    (meaning the necessary servers are not currently available), re-throw the
//    original exception.
#define HA(METHOD, ...)                                                      \
        try                                                                  \
        {                                                                    \
          METHOD(__VA_ARGS__, ConsistencyLevel::ONE);                        \
        }                                                                    \
        catch(NotFoundException& nfe)                                        \
        {                                                                    \
          LOG_DEBUG("Failed ONE read for %s. Try QUORUM", #METHOD);          \
                                                                             \
          try                                                                \
          {                                                                  \
            METHOD(__VA_ARGS__, ConsistencyLevel::QUORUM);                   \
          }                                                                  \
          catch(NotFoundException)                                           \
          {                                                                  \
            throw nfe;                                                       \
          }                                                                  \
          catch(UnavailableException)                                        \
          {                                                                  \
            throw nfe;                                                       \
          }                                                                  \
        }


void Cache::Request::
ha_get_columns(const std::string& key,
               const std::vector<std::string>& names,
               std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_columns, key, names, columns);
}


void Cache::Request::
ha_get_columns_with_prefix(const std::string& key,
                           const std::string& prefix,
                           std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_columns_with_prefix, key, prefix, columns);
}

void Cache::Request::
ha_get_all_columns(const std::string& key,
                   std::vector<ColumnOrSuperColumn>& columns)
{
  HA(get_row, key, columns);
}


void Cache::Request::
get_columns(const std::string& key,
            const std::vector<std::string>& names,
            std::vector<ColumnOrSuperColumn>& columns,
            ConsistencyLevel::type consistency_level)
{
  // Get only the specified column names.
  SlicePredicate sp;
  sp.column_names = names;
  sp.__isset.column_names = true;

  issue_get_for_key(key, sp, columns, consistency_level);
}


void Cache::Request::
get_columns_with_prefix(const std::string& key,
                        const std::string& prefix,
                        std::vector<ColumnOrSuperColumn>& columns,
                        ConsistencyLevel::type consistency_level)
{
  // This slice range gets all columns with the specified prefix.
  SliceRange sr;
  sr.start = prefix;
  // Increment the last character of the "finish" field.
  sr.finish = prefix;
  *sr.finish.rbegin() = (*sr.finish.rbegin() + 1);

  SlicePredicate sp;
  sp.slice_range = sr;
  sp.__isset.slice_range = true;

  issue_get_for_key(key, sp, columns, consistency_level);

  // Remove the prefix from the returned column names.
  for (std::vector<ColumnOrSuperColumn>::iterator it = columns.begin();
       it != columns.end();
       ++it)
  {
    it->column.name = it->column.name.substr(prefix.length());
  }
}

void Cache::Request::
get_row(const std::string& key,
        std::vector<ColumnOrSuperColumn>& columns,
        ConsistencyLevel::type consistency_level)
{
  // This slice range gets all columns with the specified prefix.
  SliceRange sr;
  sr.start = "";
  // Increment the last character of the "finish" field.
  sr.finish = "";

  SlicePredicate sp;
  sp.slice_range = sr;
  sp.__isset.slice_range = true;

  issue_get_for_key(key, sp, columns, consistency_level);
}


void Cache::Request::
issue_get_for_key(const std::string& key,
                  const SlicePredicate& predicate,
                  std::vector<ColumnOrSuperColumn>& columns,
                  ConsistencyLevel::type consistency_level)
{
  ColumnParent cparent;
  cparent.column_family = _column_family;

  _trx->start_timer();
  _client->get_slice(columns, key, cparent, predicate, consistency_level);
  _trx->stop_timer();

  if (columns.size() == 0)
  {
    Cache::NoResultsException row_not_found_ex(_column_family, key);
    throw row_not_found_ex;
  }
}

//
// DeleteRowsRequest methods
//

Cache::DeleteRowsRequest::DeleteRowsRequest(const std::string& column_family,
                                            int64_t timestamp) :
  ModificationRequest(column_family, timestamp)
{}


Cache::DeleteRowsRequest::~DeleteRowsRequest()
{}


void Cache::DeleteRowsRequest::
delete_row(const std::string& key,
           int64_t timestamp)
{
  ColumnPath cp;
  cp.column_family = _column_family;

  _trx->start_timer();
  LOG_DEBUG("Deleting row with key %s (timestamp %lld", key.c_str(), timestamp);
  _client->remove(key, cp, timestamp, ConsistencyLevel::ONE);
  _trx->stop_timer();
}

void Cache::DeleteRowsRequest::
delete_columns_from_multiple_cfs(const std::vector<CFRowColumnValue>& to_rm,
                                 int64_t timestamp)
{
  // The mutation map is of the form {"key": {"column_family": [mutations] } }
  std::map<std::string, std::map<std::string, std::vector<Mutation> > > mutmap;

  // Populate the mutations vector.
  LOG_DEBUG("Constructing cache delete request with timestamp %lld", timestamp);

  for (std::vector<CFRowColumnValue>::const_iterator it = to_rm.begin();
       it != to_rm.end();
       ++it)
  {
    std::vector<Mutation> mutations;
    Mutation mutation;
    Deletion deletion;
    SlicePredicate what;
    std::vector<std::string> column_names;

    for (std::map<std::string, std::string>::const_iterator col = it->columns.begin();
         col != it->columns.end();
         ++col)
    {
      // Vector of mutations (one per column being modified).
      column_names.push_back(col->first);
    }

    what.__set_column_names(column_names);
    LOG_DEBUG("Deleting %d columns from %s:%s", what.column_names.size(), it->cf.c_str(), it->row.c_str());
    deletion.__set_predicate(what);
    deletion.__set_timestamp(timestamp);
    mutation.__set_deletion(deletion);
    mutations.push_back(mutation);

    mutmap[it->row][it->cf] = mutations;
  }

  // Execute the database operation.
  LOG_DEBUG("Executing delete request operation");
  _trx->start_timer();
  _client->batch_mutate(mutmap, ConsistencyLevel::ONE);
  _trx->stop_timer();
}


//
// PutIMSSubscription methods.
//

Cache::PutIMSSubscription::
PutIMSSubscription(const std::string& public_id,
                   const std::string& xml,
                   const RegistrationState reg_state,
                   const std::vector<std::string>& impis,
                   const int64_t timestamp,
                   const int32_t ttl):
  PutRequest(IMPU, timestamp, 0),
  _public_ids(1, public_id),
  _impis(impis),
  _xml(xml),
  _reg_state(reg_state),
  _ttl(ttl)
{}
Cache::PutIMSSubscription::
PutIMSSubscription(const std::vector<std::string>& public_ids,
                   const std::string& xml,
                   const RegistrationState reg_state,
                   const std::vector<std::string>& impis,
                   const int64_t timestamp,
                   const int32_t ttl):
  PutRequest(IMPU, timestamp, 0),
  _public_ids(public_ids),
  _impis(impis),
  _xml(xml),
  _reg_state(reg_state),
  _ttl(ttl)
{}


Cache::PutIMSSubscription::
~PutIMSSubscription()
{}


void Cache::PutIMSSubscription::perform()
{
  std::vector<CFRowColumnValue> to_put;
  std::map<std::string, std::string> columns;
  columns[IMS_SUB_XML_COLUMN_NAME] = _xml;

  if (_reg_state == RegistrationState::REGISTERED)
  {
    columns[REG_STATE_COLUMN_NAME] = BOOLEAN_TRUE;
  }
  else if (_reg_state == RegistrationState::UNREGISTERED)
  {
    columns[REG_STATE_COLUMN_NAME] = BOOLEAN_FALSE;
  }
  else
  {
    if (_reg_state != RegistrationState::UNCHANGED)
    {
      // LCOV_EXCL_START - invalid case not hit in UT
      LOG_ERROR("Invalid registration state %d", _reg_state);
      // LCOV_EXCL_STOP
    }
  }

  for (std::vector<std::string>::iterator impi = _impis.begin();
       impi != _impis.end();
       impi++)
  {
    std::string column_name = IMPI_COLUMN_PREFIX + *impi;
    columns[column_name] = "";

    std::map<std::string, std::string> impi_columns;
    std::vector<std::string>::iterator default_public_id = _public_ids.begin();
    impi_columns[IMPI_MAPPING_PREFIX + *default_public_id] = "";
    to_put.push_back(CFRowColumnValue(IMPI_MAPPING, *impi, impi_columns));
  }

  for (std::vector<std::string>::iterator row = _public_ids.begin();
       row != _public_ids.end();
       row++)
  {
    to_put.push_back(CFRowColumnValue(_column_family, *row, columns));
  }

  put_columns_to_multiple_cfs(to_put, _timestamp, _ttl);
  _trx->on_success(this);
}

// PutAssociatedPrivateID methods

Cache::PutAssociatedPrivateID::
PutAssociatedPrivateID(const std::vector<std::string>& impus,
                       const std::string& impi,
                       const int64_t timestamp,
                       const int32_t ttl) :
  PutRequest(IMPU, timestamp, ttl),
  _impus(impus),
  _impi(impi)
{}


Cache::PutAssociatedPrivateID::
~PutAssociatedPrivateID()
{}


void Cache::PutAssociatedPrivateID::perform()
{
  std::vector<CFRowColumnValue> to_put;
  std::map<std::string, std::string> impu_columns;
  std::map<std::string, std::string> impi_columns;
  impu_columns[IMPI_COLUMN_PREFIX + _impi] = "";

  std::string default_public_id = _impus.front();
  impi_columns[IMPI_MAPPING_PREFIX + default_public_id] = "";
  to_put.push_back(CFRowColumnValue(IMPI_MAPPING, _impi, impi_columns));

  for (std::vector<std::string>::iterator row = _impus.begin();
       row != _impus.end();
       row++)
  {
    to_put.push_back(CFRowColumnValue(_column_family, *row, impu_columns));
  }

  put_columns_to_multiple_cfs(to_put, _timestamp, _ttl);

  _trx->on_success(this);
}


//
// PutAssociatedPublicID methods.
//

Cache::PutAssociatedPublicID::
PutAssociatedPublicID(const std::string& private_id,
                      const std::string& assoc_public_id,
                      const int64_t timestamp,
                      const int32_t ttl) :
  PutRequest(IMPI, timestamp, ttl),
  _private_id(private_id),
  _assoc_public_id(assoc_public_id)
{}


Cache::PutAssociatedPublicID::
~PutAssociatedPublicID()
{}


void Cache::PutAssociatedPublicID::perform()
{
  std::map<std::string, std::string> columns;
  columns[ASSOC_PUBLIC_ID_COLUMN_PREFIX + _assoc_public_id] = "";

  std::vector<std::string> keys(1, _private_id);

  put_columns(keys, columns, _timestamp, _ttl);
  _trx->on_success(this);
}

//
// PutAuthVector methods.
//

Cache::PutAuthVector::
PutAuthVector(const std::string& private_id,
              const DigestAuthVector& auth_vector,
              const int64_t timestamp,
              const int32_t ttl) :
  PutRequest(IMPI, timestamp, ttl),
  _private_ids(1, private_id),
  _auth_vector(auth_vector)
{}


Cache::PutAuthVector::
~PutAuthVector()
{}


void Cache::PutAuthVector::perform()
{
  std::map<std::string, std::string> columns;
  columns[DIGEST_HA1_COLUMN_NAME]      = _auth_vector.ha1;
  columns[DIGEST_REALM_COLUMN_NAME]    = _auth_vector.realm;
  columns[DIGEST_QOP_COLUMN_NAME]      = _auth_vector.qop;
  columns[KNOWN_PREFERRED_COLUMN_NAME] = _auth_vector.preferred ? BOOLEAN_TRUE : BOOLEAN_FALSE;

  put_columns(_private_ids, columns, _timestamp, _ttl);
  _trx->on_success(this);
}


//
// GetIMSSubscription methods
//

Cache::GetIMSSubscription::
GetIMSSubscription(const std::string& public_id) :
  GetRequest(IMPU),
  _public_id(public_id),
  _xml(),
  _reg_state(RegistrationState::NOT_REGISTERED),
  _xml_ttl(0),
  _reg_state_ttl(0),
  _impis()
{}


Cache::GetIMSSubscription::
~GetIMSSubscription()
{}


void Cache::GetIMSSubscription::perform()
{
  int64_t now = generate_timestamp();
  LOG_DEBUG("Issuing get for column %s for key %s",
            IMS_SUB_XML_COLUMN_NAME.c_str(), _public_id.c_str());
  std::vector<ColumnOrSuperColumn> results;

  try
  {
    ha_get_all_columns(_public_id, results);

    for(std::vector<ColumnOrSuperColumn>::iterator it = results.begin(); it != results.end(); ++it)
    {
      if (it->column.name == IMS_SUB_XML_COLUMN_NAME)
      {
        _xml = it->column.value;

        // Cassandra timestamps are in microseconds (see
        // generate_timestamp) but TTLs are in seconds, so divide the
        // timestamps by a million.
        if (it->column.ttl > 0)
        {
          _xml_ttl = ((it->column.timestamp/1000000) + it->column.ttl) - (now / 1000000);
        };
        LOG_DEBUG("Retrieved XML column with TTL %d and value %s", _xml_ttl, _xml.c_str());
      }
      else if (it->column.name == REG_STATE_COLUMN_NAME)
      {
        if (it->column.ttl > 0)
        {
          _reg_state_ttl = ((it->column.timestamp/1000000) + it->column.ttl) - (now / 1000000);
        };
        if (it->column.value == BOOLEAN_TRUE)
        {
          _reg_state = RegistrationState::REGISTERED;
          LOG_DEBUG("Retrieved is_registered column with value True and TTL %d",
                    _reg_state_ttl);
        }
        else if (it->column.value == BOOLEAN_FALSE)
        {
          _reg_state = RegistrationState::UNREGISTERED;
          LOG_DEBUG("Retrieved is_registered column with value False and TTL %d",
                    _reg_state_ttl);
        }
        else if ((it->column.value == ""))
        {
          LOG_DEBUG("Retrieved is_registered column with empty value and TTL %d",
                    _reg_state_ttl);
        }
        else
        {
          LOG_WARNING("Registration state column has invalid value %d %s",
                      it->column.value.c_str()[0],
                      it->column.value.c_str());
        };
      }
      else if (it->column.name.find(IMPI_COLUMN_PREFIX) == 0)
      {
        std::string impi = it->column.name.substr(IMPI_COLUMN_PREFIX.length());
        _impis.push_back(impi);
      }
    }

    // If we're storing user data for this subscriber (i.e. there is
    // XML), then by definition they cannot be in NOT_REGISTERED state
    // - they must be in UNREGISTERED state.
    if ((_reg_state == RegistrationState::NOT_REGISTERED) && !_xml.empty())
    {
      LOG_DEBUG("Found stored XML for subscriber, treating as UNREGISTERED state");
      _reg_state = RegistrationState::UNREGISTERED;
    }

  }
  catch(Cache::NoResultsException& nre)
  {
    // This is a valid state rather than an exceptional one, so we
    // catch the exception and return success. Values ae left in the
    // default state (NOT_REGISTERED and empty XML).
  }


  _trx->on_success(this);
}

void Cache::GetIMSSubscription::get_xml(std::string& xml, int32_t& ttl)
{
  xml = _xml;
  ttl = _xml_ttl;
}

void Cache::GetIMSSubscription::get_associated_impis(std::vector<std::string>& associated_impis)
{
  associated_impis = _impis;
}


void Cache::GetIMSSubscription::get_result(std::pair<RegistrationState, std::string>& result)
{
  RegistrationState state;
  int32_t reg_ttl;
  std::string xml;
  int32_t xml_ttl;

  get_registration_state(state, reg_ttl);
  get_xml(xml, xml_ttl);
  result.first = state;
  result.second = xml;
}

void Cache::GetIMSSubscription::get_result(Cache::GetIMSSubscription::Result& result)
{
  int32_t unused_ttl;

  get_registration_state(result.state, unused_ttl);
  get_xml(result.xml, unused_ttl);
  get_associated_impis(result.impis);
}


void Cache::GetIMSSubscription::get_registration_state(RegistrationState& reg_state, int32_t& ttl)
{
  reg_state = _reg_state;
  ttl = _reg_state_ttl;
}

//
// GetAssociatedPublicIDs methods
//

Cache::GetAssociatedPublicIDs::
GetAssociatedPublicIDs(const std::string& private_id) :
  GetRequest(IMPI),
  _private_ids(1, private_id),
  _public_ids()
{}


Cache::GetAssociatedPublicIDs::
GetAssociatedPublicIDs(const std::vector<std::string>& private_ids) :
  GetRequest(IMPI),
  _private_ids(private_ids),
  _public_ids()
{}


Cache::GetAssociatedPublicIDs::
~GetAssociatedPublicIDs()
{}


void Cache::GetAssociatedPublicIDs::perform()
{
  std::vector<ColumnOrSuperColumn> columns;
  std::set<std::string> public_ids;

  for (std::vector<std::string>::iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    LOG_DEBUG("Looking for public IDs for private ID %s", it->c_str());
    try
    {
      ha_get_columns_with_prefix(*it,
                                 ASSOC_PUBLIC_ID_COLUMN_PREFIX,
                                 columns);
    }
    catch(Cache::NoResultsException& nre)
    {
      LOG_INFO("Couldn't find any public IDs for private ID %s", (*it).c_str());
    }

    // Convert the query results from a vector of columns to a vector containing
    // the column names. The public_id prefix has already been stripped, so this
    // is just a list of public IDs and can be passed directly to on_success.
    for(std::vector<ColumnOrSuperColumn>::const_iterator column_it = columns.begin();
        column_it != columns.end();
        ++column_it)
    {
      LOG_DEBUG("Found associated public ID %s", column_it->column.name.c_str());
      public_ids.insert(column_it->column.name);
    }
    columns.clear();
  }

  // Move the std::set of public_ids to the std::vector of _public_ids so that they
  // are available to the handler.
  std::copy(public_ids.begin(), public_ids.end(), std::back_inserter(_public_ids));

  _trx->on_success(this);
}

void Cache::GetAssociatedPublicIDs::get_result(std::vector<std::string>& ids)
{
  ids = _public_ids;
}

//
// GetAssociatedPrimaryPublicIDs methods
//

Cache::GetAssociatedPrimaryPublicIDs::
GetAssociatedPrimaryPublicIDs(const std::string& private_id) :
  GetRequest(IMPI_MAPPING),
  _private_id(private_id),
  _public_ids()
{}


void Cache::GetAssociatedPrimaryPublicIDs::perform()
{
  std::vector<ColumnOrSuperColumn> columns;

  LOG_DEBUG("Looking for primary public IDs for private ID %s", _private_id.c_str());
  try
  {
    ha_get_columns_with_prefix(_private_id,
                               IMPI_MAPPING_PREFIX,
                               columns);
  }
  catch(Cache::NoResultsException& nre)
  {
    LOG_INFO("Couldn't find any public IDs for private ID %s", _private_id.c_str());
  }

  // Convert the query results from a vector of columns to a vector containing
  // the column names. The public_id prefix has already been stripped, so this
  // is just a list of public IDs and can be passed directly to on_success.
  for(std::vector<ColumnOrSuperColumn>::const_iterator column_it = columns.begin();
      column_it != columns.end();
      ++column_it)
  {
    LOG_DEBUG("Found associated public ID %s", column_it->column.name.c_str());
    _public_ids.push_back(column_it->column.name);
  }

  _trx->on_success(this);
}

void Cache::GetAssociatedPrimaryPublicIDs::get_result(std::vector<std::string>& ids)
{
  ids = _public_ids;
}

//
// GetAuthVector methods.
//

Cache::GetAuthVector::
GetAuthVector(const std::string& private_id) :
  GetRequest(IMPI),
  _private_id(private_id),
  _public_id(""),
  _auth_vector()
{}


Cache::GetAuthVector::
GetAuthVector(const std::string& private_id,
              const std::string& public_id) :
  GetRequest(IMPI),
  _private_id(private_id),
  _public_id(public_id),
  _auth_vector()
{}


Cache::GetAuthVector::
~GetAuthVector()
{}


void Cache::GetAuthVector::perform()
{
  LOG_DEBUG("Looking for authentication vector for %s", _private_id.c_str());
  std::vector<std::string> requested_columns;
  std::string public_id_col = "";
  bool public_id_requested = false;
  bool public_id_found = false;

  requested_columns.push_back(DIGEST_HA1_COLUMN_NAME);
  requested_columns.push_back(DIGEST_REALM_COLUMN_NAME);
  requested_columns.push_back(DIGEST_QOP_COLUMN_NAME);
  requested_columns.push_back(KNOWN_PREFERRED_COLUMN_NAME);

  if (_public_id.length() > 0)
  {
    LOG_DEBUG("Checking public ID %s", _public_id.c_str());
    // We've been asked to verify the private ID has an associated public ID.
    // So request the public ID column as well.
    //
    // This is a dynamic column so we include it's prefix.
    public_id_col = ASSOC_PUBLIC_ID_COLUMN_PREFIX + _public_id;
    requested_columns.push_back(public_id_col);
    public_id_requested = true;
  }

  LOG_DEBUG("Issuing cache query");
  std::vector<ColumnOrSuperColumn> results;
  ha_get_columns(_private_id, requested_columns, results);

  for (std::vector<ColumnOrSuperColumn>::const_iterator it = results.begin();
       it != results.end();
       ++it)
  {
    const Column* col = &it->column;

    if (col->name == DIGEST_HA1_COLUMN_NAME)
    {
      _auth_vector.ha1 = col->value;
    }
    else if (col->name == DIGEST_REALM_COLUMN_NAME)
    {
      _auth_vector.realm = col->value;
    }
    else if (col->name == DIGEST_QOP_COLUMN_NAME)
    {
      _auth_vector.qop = col->value;
    }
    else if (col->name == KNOWN_PREFERRED_COLUMN_NAME)
    {
      // Cassnadra booleans are byte string of length 1, with a value f 0
      // (false) or 1 (true).
      _auth_vector.preferred = (col->value == "\x01");
    }
    else if (col->name == public_id_col)
    {
      public_id_found = true;
    }
  }

  if (public_id_requested && !public_id_found)
  {
    // We were asked to verify a public ID, but that public ID that was not
    // found.  This is a failure.
    std::string error_text = (boost::format(
                                "Private ID '%s' exists but does not have associated public ID '%s'")
                              % _private_id % _public_id).str();
    LOG_DEBUG("Cache query failed: %s", error_text.c_str());
    _trx->on_failure(this, NOT_FOUND, error_text);
  }
  else if (_auth_vector.ha1 == "")
  {
    // The HA1 column was not found.  This cannot be defaulted so is an error.
    std::string error_text = "HA1 column not found";
    LOG_DEBUG("Cache query failed: %s", error_text.c_str());
    _trx->on_failure(this, NOT_FOUND, error_text);
  }
  else
  {
    _trx->on_success(this);
  }
}

void Cache::GetAuthVector::get_result(DigestAuthVector& av)
{
  av = _auth_vector;
}

//
// DeletePublicIDs methods
//

Cache::DeletePublicIDs::
DeletePublicIDs(const std::string& public_id,
                const std::vector<std::string>& impis,
                int64_t timestamp) :
  DeleteRowsRequest(IMPU, timestamp),
  _public_ids(1, public_id),
  _impis(impis)
{}

Cache::DeletePublicIDs::
DeletePublicIDs(const std::vector<std::string>& public_ids,
                const std::vector<std::string>& impis,
                int64_t timestamp) :
  DeleteRowsRequest(IMPU, timestamp),
  _public_ids(public_ids),
  _impis(impis)
{}


Cache::DeletePublicIDs::
~DeletePublicIDs()
{}

void Cache::DeletePublicIDs::perform()
{
  std::vector<CFRowColumnValue> to_delete;

  for (std::vector<std::string>::const_iterator it = _public_ids.begin();
       it != _public_ids.end();
       ++it)
  {
    // Don't specify columns as we're deleting the whole row
    to_delete.push_back(CFRowColumnValue(_column_family, *it));
  }

  std::string primary_public_id = _public_ids.front();
  std::map<std::string, std::string> impi_columns_to_delete;
  impi_columns_to_delete[IMPI_MAPPING_PREFIX + primary_public_id] = "";

  for (std::vector<std::string>::const_iterator it = _impis.begin();
       it != _impis.end();
       ++it)
  {
    // Delete the column for this primary public ID from the IMPI
    // mapping table
    to_delete.push_back(CFRowColumnValue(IMPI_MAPPING, *it, impi_columns_to_delete));
  }

  // Perform the batch deletion we've built up
  delete_columns_from_multiple_cfs(to_delete, _timestamp);

  _trx->on_success(this);
}

//
// DeletePrivateIDs methods
//

Cache::DeletePrivateIDs::
DeletePrivateIDs(const std::string& private_id, int64_t timestamp) :
  DeleteRowsRequest(IMPI, timestamp),
  _private_ids(1, private_id)
{}


Cache::DeletePrivateIDs::
DeletePrivateIDs(const std::vector<std::string>& private_ids, int64_t timestamp) :
  DeleteRowsRequest(IMPI, timestamp),
  _private_ids(private_ids)
{}


Cache::DeletePrivateIDs::
~DeletePrivateIDs()
{}


void Cache::DeletePrivateIDs::perform()
{
  for (std::vector<std::string>::const_iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    delete_row(*it, _timestamp);
  }

  _trx->on_success(this);
}

//
// DeleteIMPIMapping methods
//

Cache::DeleteIMPIMapping::
DeleteIMPIMapping(const std::vector<std::string>& private_ids, int64_t timestamp) :
  DeleteRowsRequest(IMPI_MAPPING, timestamp),
  _private_ids(private_ids)
{}

void Cache::DeleteIMPIMapping::perform()
{
  std::vector<CFRowColumnValue> to_delete;

  for (std::vector<std::string>::const_iterator it = _private_ids.begin();
       it != _private_ids.end();
       ++it)
  {
    // Don't specify columns as we're deleting the whole row
    to_delete.push_back(CFRowColumnValue(_column_family, *it));
  }

  delete_columns_from_multiple_cfs(to_delete, _timestamp);
  _trx->on_success(this);
}

//
// DissociateImplicitRegistrationSetFromImpi methods
//

Cache::DissociateImplicitRegistrationSetFromImpi::
DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                          const std::string& impi,
                                          int64_t timestamp) :
  DeleteRowsRequest(IMPU, timestamp),
  _impus(impus),
  _impi(impi)
{}

void Cache::DissociateImplicitRegistrationSetFromImpi::perform()
{
  std::vector<CFRowColumnValue> to_delete;

  // Go through IMPI mapping table and delete the columns

  std::string primary_public_id = _impus.front();

  LOG_DEBUG("Deleting column %s from row %s", std::string(IMPI_MAPPING_PREFIX + primary_public_id).c_str(), _impi.c_str());

  std::map<std::string, std::string> impi_columns_to_delete;
  std::map<std::string, std::string> impu_columns_to_delete;

  // Value doesn't matter for deletions
  impi_columns_to_delete[IMPI_MAPPING_PREFIX + primary_public_id] = "";
  impu_columns_to_delete[IMPI_COLUMN_PREFIX + _impi] = "";

  to_delete.push_back(CFRowColumnValue(IMPI_MAPPING, _impi, impi_columns_to_delete));

  // Check how many IMPIs are associated with this implicit
  // registration set (all the columns are the same, so we only need
  // to check the first)

  std::vector<cass::ColumnOrSuperColumn> columns;
  ha_get_columns_with_prefix(primary_public_id, IMPI_COLUMN_PREFIX, columns);
  int associated_impis = columns.size();

  LOG_DEBUG("%d IMPIs are associated with this IRS", associated_impis);

  for (std::vector<std::string>::const_iterator it = _impus.begin();
       it != _impus.end();
       ++it)
  {
    // Is this the last IMPI associated with this IMPU?
    if (associated_impis > 1)
    {
      // No - Go through IMPU table and delete the column
      // specifically for this IMPI
      to_delete.push_back(CFRowColumnValue(_column_family, *it, impu_columns_to_delete));
    }
    else
    {
      // Yes - delete all columns in the IMPU rows
      impu_columns_to_delete[IMS_SUB_XML_COLUMN_NAME] = "";
      impu_columns_to_delete[REG_STATE_COLUMN_NAME] = "";
      to_delete.push_back(CFRowColumnValue(_column_family, *it, impu_columns_to_delete));
    }
  }

  // Perform the batch deletion we've built up
  delete_columns_from_multiple_cfs(to_delete, _timestamp);

  _trx->on_success(this);
}
