/**
 * Memcached implementation of a HSS Cache
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "memcached_cache.h"
#include <string>
#include <future>
#include "homestead_xml_utils.h"
#include "log.h"
#include "utils.h"

using std::placeholders::_1;
using std::placeholders::_2;

typedef std::tuple<Store::Status, ImpuStore::Impu*, unsigned long> impu_result_t;

// LCOV_EXCL_START
static void pause_stopwatch(Utils::StopWatch* stopwatch, const std::string& reason)
{
  TRC_DEBUG("Pausing stopwatch due to %s", reason.c_str());
  stopwatch->stop();
}

static void resume_stopwatch(Utils::StopWatch* stopwatch, const std::string& reason)
{
  TRC_DEBUG("Resuming stopwatch due to %s", reason.c_str());
  stopwatch->start();
}
// LCOV_EXCL_STOP

Utils::IOHook* create_hook(Utils::StopWatch* stopwatch)
{
  return new Utils::IOHook(std::bind(pause_stopwatch, stopwatch, _1),
                           std::bind(resume_stopwatch, stopwatch, _1));
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::create_impu(uint64_t cas,
                                                                      const ImpuStore* store)
{
  std::vector<std::string> impis = get_associated_impis();
  std::vector<std::string> impus = get_associated_impus();

  int now = time(0);

  return new ImpuStore::DefaultImpu(_default_impu,
                                    impus,
                                    impis,
                                    _registration_state,
                                    _charging_addresses,
                                    get_ims_sub_xml(),
                                    cas,
                                    _ttl + now,
                                    store);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu()
{
  return create_impu(0L, nullptr);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu_from_impu(const ImpuStore::Impu* with_cas)
{
  return create_impu(with_cas->cas, with_cas->store);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu_for_store(const ImpuStore* store)
{
  if (_store == store)
  {
    return create_impu(_cas, _store);
  }
  else
  {
    return nullptr;
  }
}

// Update the given data store, based on an updated view of the world
// provided by the user
//
// An element may be passed in to ignore (e.g. the default IMPU shouldn't
// be treated as an associated IMPU)
//
// Elements which are not in the vector, but are in the provided data
// store are marked as DELETED.
//
// Elements which are in the vector, but are either not in the provided
// data store, or are in the provided data store as DELETED, are marked
// as ADDED
//
// Any other element is left unchanged.
void set_elements(const std::vector<std::string>& updated,
                  MemcachedImplicitRegistrationSet::Data& data,
                  const std::string ignore)
{
  for (std::pair<const std::string, MemcachedImplicitRegistrationSet::State>& entry : data)
  {
    if (!Utils::in_vector(entry.first, updated))
    {
      entry.second = MemcachedImplicitRegistrationSet::State::DELETED;
    }
  }

  for (const std::string& entry : updated)
  {
    if (entry != ignore)
    {
      MemcachedImplicitRegistrationSet::Data::iterator it = data.find(entry);

      if (it == data.end())
      {
        data[entry] = MemcachedImplicitRegistrationSet::State::ADDED;
      }
      else if (it->second == MemcachedImplicitRegistrationSet::State::DELETED)
      {
        it->second = MemcachedImplicitRegistrationSet::State::ADDED;
      }
    }
  }
}

void MemcachedImplicitRegistrationSet::set_ims_sub_xml(const std::string& xml)
{
  TRC_DEBUG("Setting XML for IMPU: %s to %s",
            _default_impu.c_str(),
            xml.c_str());
  _ims_sub_xml_set = true;
  _ims_sub_xml = xml;

  std::string default_impu;

  std::vector<std::string> assoc_impus =
    XmlUtils::get_public_and_default_ids(xml,
                                         default_impu);

  if (_default_impu != default_impu)
  {
    if (_default_impu != "")
    {
      TRC_WARNING("Unsupported change of default IMPU from %s to %s - HSS should perform RTR first",
                  _default_impu.c_str(),
                  default_impu.c_str());
    }

    _default_impu = default_impu;
  }

  set_elements(assoc_impus, _associated_impus, default_impu);
}

void MemcachedImplicitRegistrationSet::add_associated_impi(const std::string& impi)
{
  _impis[impi] = MemcachedImplicitRegistrationSet::State::ADDED;
}

void MemcachedImplicitRegistrationSet::delete_associated_impi(const std::string& impi)
{
  _impis[impi] = MemcachedImplicitRegistrationSet::State::DELETED;
}

// Merge two data sets
// All new elements in the data set will be marked as unchanged, any missing
// from the data set will be makred as deleted if they are unchanged currently.
void merge_data_sets(MemcachedImplicitRegistrationSet::Data& data, std::vector<std::string> added)
{
  for (const std::string &key : added)
  {
    MemcachedImplicitRegistrationSet::Data::iterator it = data.find(key);

    if (it == data.end())
    {
      data[key] = MemcachedImplicitRegistrationSet::State::UNCHANGED;
    }
  }

  // Now mark missing ones as deleted.
  for (MemcachedImplicitRegistrationSet::Data::value_type& pair : data)
  {
    bool unchanged = pair.second == MemcachedImplicitRegistrationSet::State::UNCHANGED;
    bool not_in_vector = !Utils::in_vector(pair.first, added);
    if (unchanged && not_in_vector)
    {
      pair.second = MemcachedImplicitRegistrationSet::State::DELETED;
    }
  }
}

void MemcachedImplicitRegistrationSet::update_from_impu_from_store(ImpuStore::DefaultImpu* impu)
{
  MemcachedImplicitRegistrationSet::State state;

  // We only update our data from the store if it's not been updated by
  // the caller
  if (!_registration_state_set)
  {
    _registration_state = impu->registration_state;
  }

  if (!_ims_sub_xml_set)
  {
    _ims_sub_xml = impu->service_profile;
  }

  if (!_charging_addresses_set)
  {
    _charging_addresses = impu->charging_addresses;
  }

  // Update the IRS with the details in IMPI and Associated IMPUs

  // For IMPIs, the store data is equivalent to ours. We mark unknown IMPIs as
  // unchanged, and removed unchanged IMPIs as deleted.
  merge_data_sets(_impis, impu->impis);

  if (_refreshed)
  {
    // If we are marked as refreshed, then the IMPU data from the store is *less* up
    // to date than our data, so we should mark it as deleted so we clear up any
    // references
    state = MemcachedImplicitRegistrationSet::State::DELETED;

    for (const std::string& assoc_impu : impu->associated_impus)
    {
      _associated_impus.emplace(assoc_impu, state);
    }
  }
  else
  {
    // If we aren't refreshed (i.e. TTL isn't set), then update our view of the
    // TTL
    int now = time(0);
    _ttl = impu->expiry - now;

    // If we are marked as not refreshed, the IMPU data from the store should be
    // considered valid, and we should mark it as unchanged, if we weren't
    // previously aware of it.
    merge_data_sets(_associated_impus, impu->associated_impus);
  }
}

void delete_tracked(MemcachedImplicitRegistrationSet::Data& data)
{
  for (std::pair<const std::string, MemcachedImplicitRegistrationSet::State>& entry : data)
  {
    entry.second = MemcachedImplicitRegistrationSet::State::DELETED;
  }
}

void MemcachedImplicitRegistrationSet::delete_assoc_impus()
{
  delete_tracked(_associated_impus);
}

void MemcachedImplicitRegistrationSet::delete_impis()
{
  delete_tracked(_impis);
}

// Helper function to get the details of an IMPU.
// Note this doesn't sort out associated versus default impus.
//
// We try to get the Impu from the local store, and fall back to remote stores
// if we don't find it in the local store.
// The exact logic is:
//  - try to find the Impu in the local store
//  - if we get any errors other than NOT_FOUND, immediately give up and return
//    the error
//  - if we get NOT_FOUND from the local store:
//    - try all remote stores in parallel
//    - if we get a result from a remote store, use that
//    - if we get any other error, just ignore it (since we've already
//      established that the local store returned NOT_FOUND)
Store::Status MemcachedCache::get_impu_for_impu_gr(const std::string& impu,
                                                   ImpuStore::Impu*& out_impu,
                                                   SAS::TrailId trail,
                                                   Utils::StopWatch* stopwatch)
{
  Utils::IOHook* hook = nullptr;

  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  Store::Status status = _local_store->get_impu(impu, out_impu, trail);

  // We've done the only network I/O on this thread that we were going to, so
  // can safely delete our hook now
  if (hook)
  {
    delete hook; hook = nullptr;
  }

  if (status == Store::Status::NOT_FOUND)
  {
    // If we successfully connect to the local store but fail to find an Impu,
    // try the remote stores
    std::vector<std::promise<impu_result_t>*> promises;

    for (ImpuStore* remote_store : _remote_stores)
    {
      std::promise<impu_result_t>* promise = new std::promise<impu_result_t>();
      promises.push_back(promise);

      // If we have a stopwatch, we need to time how long each of the parallel
      // remote requests takes, so we need to create and start a StopWatch for
      // each one.
      Utils::StopWatch* remote_stopwatch = nullptr;
      if (stopwatch)
      {
        remote_stopwatch = new Utils::StopWatch();
        remote_stopwatch->start();
      }

      _thread_pool.add_work([promise, remote_store, impu, trail, remote_stopwatch]()->void
      {
        // If we created a StopWatch, we now need to create an IOHook so that it
        // will pause when we're doing network I/O
        // These Hooks are thread_local, which is why this is done here (on the
        // thread we'll use for the remote read)
        Utils::IOHook* remote_hook = nullptr;
        if (remote_stopwatch)
        {
          remote_hook = create_hook(remote_stopwatch);
        }

        ImpuStore::Impu* remote_data = nullptr;
        Store::Status remote_status = remote_store->get_impu(impu, remote_data, trail);
        unsigned long remote_time = 0L;

        if (remote_stopwatch)
        {
          delete remote_hook;
          remote_stopwatch->read(remote_time);
          delete remote_stopwatch;
        }

        promise->set_value(impu_result_t(remote_status, remote_data, remote_time));
      });
    }

    if (stopwatch)
    {
      // Stop the main StopWatch while we wait for the remote reads to complete.
      // We'll later add on the time we spent processing these, minus I/O time
      stopwatch->stop();
    }

    unsigned long remote_time_to_add = 0L;

    for (std::promise<impu_result_t>* promise : promises)
    {
      std::future<impu_result_t> future = promise->get_future();
      impu_result_t result = future.get();

      // Want to choose whichever request took the longest to add to our stopwatch time
      unsigned long remote_time = std::get<2>(result);
      if (remote_time > remote_time_to_add)
      {
        remote_time_to_add = remote_time;
      }

      if ((status != Store::Status::OK) && (std::get<0>(result) == Store::Status::OK))
      {
        // If we've not yet set the impu, use the one from this remote if it succeeded
        out_impu = std::get<1>(result);
        status = Store::Status::OK;
      }
      else
      {
        ImpuStore::Impu* discard = std::get<1>(result);

        if (discard)
        {
          delete discard;
        }
      }

      delete promise;
    }

    // Restart the StopWatch
    if (stopwatch)
    {
      stopwatch->start();
      stopwatch->add_time(remote_time_to_add);
    }
  }

  return status;
}

Store::Status MemcachedCache::get_impus_for_impi(const std::string& impi,
                                                 SAS::TrailId trail,
                                                 Utils::StopWatch* stopwatch,
                                                 std::vector<std::string>& impus)
{
  ImpuStore::ImpiMapping* mapping = nullptr;
  Store::Status status = get_impi_mapping_gr(impi, mapping, trail, stopwatch);

  if (status == Store::Status::OK)
  {
    impus = mapping->get_default_impus();
    delete mapping;
  }

  return status;
}

// Try to get the ImpiMapping from the local store, falling back to the remote
// stores if we don't find anything in the local store.
// The exact logic is:
//  - try to find the mapping in the local store
//  - if we get any errors other than NOT_FOUND, immediately give up and return
//    the error
//  - if we get NOT_FOUND from the local store:
//    - try each remote store in turn
//    - if we get OK from a remote store, we've found a mapping so return OK
//    - if we get any other error, try the next remote store but don't set the
//      return value to that error (since we've already established that the
//      local store returned NOT_FOUND)
//
// On success, out_mapping is set the to retrieved mapping.
// On failure, out_mapping is unchanged.
Store::Status MemcachedCache::get_impi_mapping_gr(const std::string& impi,
                                                  ImpuStore::ImpiMapping*& out_mapping,
                                                  SAS::TrailId trail,
                                                  Utils::StopWatch* stopwatch)
{
  Utils::IOHook* hook = nullptr;

  // Create an IOHook that will pause the StopWatch while performing network I/O
  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  Store::Status status = _local_store->get_impi_mapping(impi, out_mapping, trail);

  if (status == Store::Status::NOT_FOUND)
  {
    // If we successfully connect to the local store but fail to find an
    // ImpiMapping, try the remote stores
    for (ImpuStore* remote_store : _remote_stores)
    {
      Store::Status remote_status = remote_store->get_impi_mapping(impi, out_mapping, trail);

      if (remote_status == Store::Status::OK)
      {
        status = remote_status;
        break;
      }
    }
  }

  if (hook)
  {
    delete hook;
  }

  return status;
}

Store::Status MemcachedCache::get_implicit_registration_set_for_impu(const std::string& impu,
                                                     SAS::TrailId trail,
                                                     Utils::StopWatch* stopwatch,
                                                     ImplicitRegistrationSet*& result)
{

  ImpuStore::Impu* data = nullptr;
  Store::Status status = get_impu_for_impu_gr(impu, data, trail, stopwatch);

  if (status == Store::Status::OK && !data->is_default_impu())
  {
    ImpuStore::AssociatedImpu* assoc_impu = (ImpuStore::AssociatedImpu*)data;

    TRC_INFO("IMPU: %s maps to IMPU: %s", impu.c_str(), assoc_impu->default_impu.c_str());

    status = get_impu_for_impu_gr(assoc_impu->default_impu, data, trail, stopwatch);

    delete assoc_impu;

    if (status == Store::Status::OK)
    {
      // Is the target IMPU a default IMPU?
      bool is_default = data->is_default_impu();

      // Does the target IMPU have the source Associated IMPU as
      // a default IMPU?
      bool is_associated = (is_default &&
                            ((ImpuStore::DefaultImpu*)data)->has_associated_impu(impu));

      if (!is_default || !is_associated)
      {
        // Target IMPU is invalid - probably a window condition
        // Log and treat as not found.
        if (!is_default)
        {
          TRC_INFO("Non-default IMPU pointed by associated IMPU record");
        }
        else if (!is_associated)
        {
          TRC_INFO("Default IMPU does not contain IMPU as associated");
        }

        delete data;
        status = Store::Status::NOT_FOUND;
      }
    }
  }

  if (status == Store::Status::OK)
  {
    result = new MemcachedImplicitRegistrationSet((ImpuStore::DefaultImpu*) data);
    delete data;
  }

  return status;
}

Store::Status MemcachedCache::perform(MemcachedCache::store_action action,
                                      progress_callback progress_cb,
                                      Utils::StopWatch* stopwatch)
{
   Store::Status status = action(_local_store, stopwatch);

   if (status == Store::Status::OK)
   {
     // If the local store update succeeded, call the progress callback
     progress_cb();

     // Now perform the action to all the remote stores, but don't update the
     // status (as we've already claimed success)
     for (ImpuStore* remote_store : _remote_stores)
     {
       Store::Status inner_status = action(remote_store, nullptr);
       if (inner_status != Store::Status::OK)
       {
         // Nothing we can do, but log the error
         TRC_DEBUG("Failed to perform operation to remote store with error %d",
                   inner_status);
       }
     }
   }

   return status;
}

Store::Status MemcachedCache::put_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                            progress_callback progress_cb,
                                                            SAS::TrailId trail,
                                                            Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs;

  if (mirs->has_changed())
  {
    store_action action =
      std::bind(&MemcachedCache::put_irs_action, this, mirs, trail, _1, _2);
    status = perform(action, progress_cb, stopwatch);
  }
  else
  {
    // This is treated as success, so call the progress callback as if we'd
    // successfully deleted it
    progress_cb();
  }

  return status;
}

Store::Status MemcachedCache::update_irs_impi_mappings(MemcachedImplicitRegistrationSet* irs,
                                                       SAS::TrailId trail,
                                                       ImpuStore* store,
                                                       Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  // Create an IOHook to pause the stopwatch when performing network I/O
  Utils::IOHook* hook = nullptr;
  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  // Updating the mappings needs to be CASed, as each of the IMPIs maps
  // to an array, which may be mutated by multiple Homesteads simultaneously

  // Remove old IMPI mappings
  for (const std::string& impi : irs->impis(MemcachedImplicitRegistrationSet::State::DELETED))
  {
    do
    {
      // We use a separate Status as failing to find an ImpiMapping shouldn't
      // affect our overall Status
      ImpuStore::ImpiMapping* mapping = nullptr;
      Store::Status inner_status = store->get_impi_mapping(impi, mapping, trail);

      if (inner_status == Store::Status::OK)
      {
        if (mapping->has_default_impu(irs->get_default_impu()))
        {
          mapping->remove_default_impu(irs->get_default_impu());

          if (mapping->is_empty())
          {
            status = store->delete_impi_mapping(mapping, trail);
          }
          else
          {
            status = store->set_impi_mapping(mapping, trail);
          }
        }
      }

      delete mapping;

    } while(status == Store::Status::DATA_CONTENTION);
  }

  // Refresh unchanged IMPIs if the IRS is being refreshed
  if (irs->is_refreshed())
  {
    for (const std::string& impi : irs->impis(MemcachedImplicitRegistrationSet::State::UNCHANGED))
    {
      do
      {
        // We use a separate Status as failing to find an ImpiMapping shouldn't
        // affect our overall Status
        ImpuStore::ImpiMapping* mapping = nullptr;
        Store::Status inner_status = store->get_impi_mapping(impi, mapping, trail);

        if (inner_status == Store::Status::OK)
        {
          int now = time(0);
          mapping->set_expiry(irs->get_ttl() + now);

          // Although we believe the IMPI-IMPU mapping is unchanged,
          // in the background it may have been deleted, and re-added,
          // so we should check that the data is still consistent with
          // the Default IMPU record
          if (!mapping->has_default_impu(irs->get_default_impu()))
          {
            mapping->add_default_impu(irs->get_default_impu());
          }
        }
        else
        {
          int now = time(0);
          mapping = new ImpuStore::ImpiMapping(impi, irs->get_default_impu(), irs->get_ttl() + now);
        }

        status = store->set_impi_mapping(mapping, trail);

        delete mapping;
      } while(status == Store::Status::DATA_CONTENTION);
    }
  }

  // Add new IMPIs
  for (const std::string& impi : irs->impis(MemcachedImplicitRegistrationSet::State::ADDED))
  {
    int64_t expiry = time(0) + irs->get_ttl();

    // Given we think this IMPI-IMPU mapping is new, and given
    // multiple IMPI mapping to multiple IRS is rare, we assume
    // that the IMPI-IMPU mapping does not exist. If it does, we'll
    // perform a CAS contention resolution
    ImpuStore::ImpiMapping* mapping = new ImpuStore::ImpiMapping(impi,
                                                                 irs->get_default_impu(),
                                                                 expiry);

    do
    {
      status = store->set_impi_mapping(mapping, trail);

      if (status == Store::Status::DATA_CONTENTION)
      {
        ImpuStore::ImpiMapping* new_mapping = nullptr;
        Store::Status inner_status = store->get_impi_mapping(impi, new_mapping, trail);

        if (inner_status == Store::Status::OK)
        {
          // We found an existing mapping so delete the old one and update the
          // one we found
          delete mapping;
          mapping = new_mapping;

          int now = time(0);
          mapping->set_expiry(irs->get_ttl() + now);

          if (!mapping->has_default_impu(irs->get_default_impu()))
          {
            mapping->add_default_impu(irs->get_default_impu());
          }
          else if (!irs->is_refreshed())
          {
            // We aren't being refreshed, and the IMPU is present, so just mark
            // the data as good
            status = Store::Status::OK;
          }
        }
        else if (inner_status == Store::Status::NOT_FOUND)
        {
          // If we've failed to find something in the store, having just had
          // DATA_CONTENTION, then just leave the mapping untouched and go back
          // around the loop, as we shouldn't hit DATA_CONTENTION next time we
          // try to set the mapping
        }
        else
        {
          // We've hit some other error, so just bail out
          status = inner_status;
        }
      }
    } while(status == Store::Status::DATA_CONTENTION);

    delete mapping;
  }

  if (hook)
  {
    delete hook;
  }

  return status;
}

Store::Status MemcachedCache::update_irs_associated_impus(MemcachedImplicitRegistrationSet* irs,
                                                          SAS::TrailId trail,
                                                          ImpuStore* store,
                                                          Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  // Create an IOHook to pause the stopwatch when performing network I/O
  Utils::IOHook* hook = nullptr;
  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  // Remove old associated IMPUs
  for (const std::string& associated_impu : irs->impus(MemcachedImplicitRegistrationSet::State::DELETED))
  {
    do
    {
      ImpuStore::Impu* mapping = nullptr;
      store->get_impu(associated_impu, mapping, trail);

      if (mapping && !mapping->is_default_impu())
      {
        ImpuStore::AssociatedImpu* assoc_mapping = (ImpuStore::AssociatedImpu*)mapping;

        if (assoc_mapping->default_impu == irs->get_default_impu())
        {
          status = store->delete_impu(mapping, trail);
        }
      }

      delete mapping;
    } while(status == Store::Status::DATA_CONTENTION);
  }

  // Refresh unchanged associated IMPUs if the IRS is being refreshed
  if (irs->is_refreshed())
  {
    for (const std::string& associated_impu : irs->impus(MemcachedImplicitRegistrationSet::State::UNCHANGED))
    {
      int64_t expiry = time(0) + irs->get_ttl();
      ImpuStore::AssociatedImpu* impu = new ImpuStore::AssociatedImpu(associated_impu,
                                                                      irs->get_default_impu(),
                                                                      0L,
                                                                      expiry,
                                                                      store);

      store->set_impu_without_cas(impu, trail);
      delete impu;
    }
  }

  // Add new associated IMPUs
  for (const std::string& associated_impu : irs->impus(MemcachedImplicitRegistrationSet::State::ADDED))
  {
    int64_t expiry = time(0) + irs->get_ttl();
    ImpuStore::AssociatedImpu* impu = new ImpuStore::AssociatedImpu(associated_impu,
                                                                    irs->get_default_impu(),
                                                                    0L,
                                                                    expiry,
                                                                    store);

    store->set_impu_without_cas(impu, trail);
    delete impu;
  }

  if (hook)
  {
    delete hook;
  }

  return status;
}

// Create an IMPU from an IRS and set it in the store.
// This is used when we failed to find the IMPU in the store, and have therefore
// created a new record for it.
// The behaviour depends on the registration state of the IMPU we're trying to
// store:
//  - if we're storing an IMPU in state REGISTERED, then we just blind write to
//    the store. This is because we only do this when we've got a new
//    registration, so we've just communicated with the HSS and the data we have
//    is authoritative. We therefore want to overwrite anything that's been put
//    in the store since then.
//    This is safe because either:
//      (a) The data in the store is from a registration at the same time, and
//          hence matches what we have (or is equivalently valid), so we can
//          just write over it.
//      (b) The data in the store is from an UNREGISTERED request at the same
//          time, but that is now out of date as we have just registered on the
//          HSS.
//    Note that we aren't resolving any differing additional IMPUs or IMPIs.
//    These should have been subject to a PPR, and will expire anyway. This very
//    narrow race (multiple SAA and HSS changes happening simultaneously for a
//    single IMPU) isn't worth resolving given the additional latency required,
//    and it's not clear which one should win anyway.
//
//  - if we're storing an IMPU in state UNREGISTERED, then we want to attempt to
//    write to the store. However, if there's any data already in the store, we
//    want to give up because that data was added since we last checked.
//    In the case where there is some data in the store, either:
//      (a) The data in the store in also UNREGISTERED, which would happen if
//          another thread was also handling an UNREGISTERED request. In that
//          case, the data will be the same so we don't need to overwrite it
//      (b) The data in the store in REGISTERED, which would happen if the user
//          has registered while we've been processing this request. In that
//          case, we don't want to overwrite it with the UNREGISTERED data.
//    So in both cases we just ignore the failure to write to the store.
Store::Status MemcachedCache::create_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store,
                                              Utils::StopWatch* stopwatch)
{
  ImpuStore::DefaultImpu* impu = irs->get_impu();

  Store::Status status = Store::Status::OK;

  // Create an IOHook to pause the stopwatch when performing network I/O
  Utils::IOHook* hook = nullptr;
  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  if (impu->registration_state == RegistrationState::REGISTERED)
  {
    TRC_DEBUG("Storing REGISTERED IMPU %s without checking CAS value",
              impu->impu.c_str());
    status = store->set_impu_without_cas(impu, trail);
  }
  else
  {
    TRC_DEBUG("Attempting to add UNREGISTERED IMPU %s", impu->impu.c_str());
    status = store->add_impu(impu, trail);

    if (status == Store::Status::DATA_CONTENTION)
    {
      // Just ignore the error
      TRC_DEBUG("Ignoring data contention error attempting to add IMPU for %s",
                impu->impu.c_str());
      status = Store::Status::OK;
    }
  }

  delete impu;

  if (hook)
  {
    delete hook;
  }

  return status;
}

typedef Store::Status (ImpuStore::*irs_impu_action)(ImpuStore::Impu*, SAS::TrailId);

Store::Status perform_irs_impu_action(irs_impu_action action,
                                      MemcachedImplicitRegistrationSet* irs,
                                      SAS::TrailId trail,
                                      ImpuStore* store,
                                      Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;
  ImpuStore::DefaultImpu* impu = irs->get_impu_for_store(store);

  // Create an IOHook to pause the stopwatch when performing network I/O
  Utils::IOHook* hook = nullptr;
  if (stopwatch)
  {
    hook = create_hook(stopwatch);
  }

  do
  {
    if (impu)
    {
      // This branch is hit if the IRS was created from an IMPU for this
      // store, and we haven't found that the update conflicts. This is
      // the mainline case in the local site case, and saves us a potential
      // re-read in the mainline case. We can also hit this case if we got the
      // IRS from a remote store, and we are now writing back to that remote
      // store. This is tautological equivalent.
      status = (store->*action)(impu, trail);
    }
    else
    {
      // The IRS was not originally created from this store, so we don't
      // have any idea how to CAS this data into place, or we hit a
      // conflict. As such start by fetching the stores view of the data.
      // As we merge in remote stores, we'll get a fuller picture of the
      // data, but we don't attempt to update local stores, and just
      // assume they'll fall into place.
      ImpuStore::Impu* mapped_impu = nullptr;
      store->get_impu(irs->get_default_impu(), mapped_impu, trail);

      if (mapped_impu == nullptr)
      {
         if (action == &ImpuStore::delete_impu)
         {
           // Nothing in the store, but we are deleting, so just
           // return OK
           status = Store::Status::OK;
         }
         else
         {
           // Nothing in the store representing this IMPU - just create
           // a new one
           impu = irs->get_impu();
           status = (store->*action)(impu, trail);
         }
      }
      else if (mapped_impu->is_default_impu())
      {
        ImpuStore::DefaultImpu* default_impu = (ImpuStore::DefaultImpu*) mapped_impu;

        // Merge details from the store into our IRS.
        irs->update_from_impu_from_store(default_impu);
        impu = irs->get_impu_from_impu(default_impu);
        status = (store->*action)(impu, trail);
      }
      else if (irs->is_refreshed())
      {
        // Default IMPU has changed - just overwrite it
        // This is safe to do because we've just hit a window of conflicts
        // and we know our data is better.
        impu = irs->get_impu_from_impu(mapped_impu);
        status = (store->*action)(impu, trail);
      }
      else
      {
        // We can't make any sensible progress here. The data
        // we have isn't valid, and the store data is almost
        // certainly more up to date. Just fail the transaction.
        status = Store::Status::ERROR;
      }

      delete mapped_impu;
    }
    delete impu; impu = nullptr;

  } while(status == Store::Status::DATA_CONTENTION);

  if (hook)
  {
    delete hook;
  }

  return status;
}

Store::Status MemcachedCache::update_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store,
                                              Utils::StopWatch* stopwatch)
{
  return perform_irs_impu_action(&ImpuStore::set_impu,
                                 irs,
                                 trail,
                                 store,
                                 stopwatch);
}

Store::Status MemcachedCache::delete_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store,
                                              Utils::StopWatch* stopwatch)
{
  // If we are deleting an MIRS, we are refreshing it.
  irs->mark_as_refreshed();
  return perform_irs_impu_action(&ImpuStore::delete_impu,
                                 irs,
                                 trail,
                                 store,
                                 stopwatch);
}

Store::Status MemcachedCache::put_irs_action(MemcachedImplicitRegistrationSet* irs,
                                             SAS::TrailId trail,
                                             ImpuStore* store,
                                             Utils::StopWatch* stopwatch)
{
  // We have three operations to perform here, and can't guarantee
  // perfect consistency, but we should eventually get consistency
  Store::Status status;

  if (irs->is_existing())
  {
    status = update_irs_impu(irs, trail, store, stopwatch);
  }
  else
  {
    status = create_irs_impu(irs, trail, store, stopwatch);
  }

  if (status == Store::Status::OK)
  {
    update_irs_associated_impus(irs, trail, store, stopwatch);
  }

  if (status == Store::Status::OK)
  {
    update_irs_impi_mappings(irs, trail, store, stopwatch);
  }

  return status;
}

Store::Status MemcachedCache::delete_irs_action(MemcachedImplicitRegistrationSet* irs,
                                                SAS::TrailId trail,
                                                ImpuStore* store,
                                                Utils::StopWatch* stopwatch)
{
  Store::Status status = delete_irs_impu(irs, trail, store, stopwatch);

  if (status == Store::Status::OK)
  {
    // Mark the Associated IMPUs as deleted, and update the store to match
    irs->delete_assoc_impus();
    status = update_irs_associated_impus(irs, trail, store, stopwatch);
  }

  if (status == Store::Status::OK)
  {
    // And similar with the IMPIs
    irs->delete_impis();
    status = update_irs_impi_mappings(irs, trail, store, stopwatch);
  }

  return status;
}

Store::Status MemcachedCache::delete_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                               progress_callback progress_cb,
                                                               SAS::TrailId trail,
                                                               Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs;

  if (mirs->is_existing())
  {
    store_action action =
      std::bind(&MemcachedCache::delete_irs_action, this, mirs, trail, _1, _2);
    status = perform(action, progress_cb, stopwatch);
  }
  else
  {
    TRC_WARNING("Attempted to delete IRS which hadn't been created: %s",
                irs->get_default_impu().c_str());

    // This is treated as success, so call the progress callback as if we'd
    // successfully deleted it
    progress_cb();
  }

  return status;
}

Store::Status MemcachedCache::delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                                progress_callback progress_cb,
                                                                SAS::TrailId trail,
                                                                Utils::StopWatch* stopwatch)
{
  store_action action =
    std::bind(&MemcachedCache::delete_irss_action, this, irss, trail, _1, _2);
  Store::Status status = perform(action, progress_cb, stopwatch);
  return status;
}

Store::Status MemcachedCache::delete_irss_action(const std::vector<ImplicitRegistrationSet*>& irss,
                                                 SAS::TrailId trail,
                                                 ImpuStore* store,
                                                 Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  for (ImplicitRegistrationSet* irs : irss)
  {
    MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs;
    if (mirs->is_existing())
    {
      status = delete_irs_action(mirs, trail, store, stopwatch);
    }
  }

  return status;
}

Store::Status MemcachedCache::get_ims_subscription(const std::string& impi,
                                                   SAS::TrailId trail,
                                                   Utils::StopWatch* stopwatch,
                                                   ImsSubscription*& result)
{
  Store::Status status;

  ImpuStore::ImpiMapping* mapping = nullptr;
  status = get_impi_mapping_gr(impi, mapping, trail, stopwatch);

  if (status == Store::Status::OK)
  {
    std::vector<ImplicitRegistrationSet*> irs;
    status = get_implicit_registration_sets_for_impus(mapping->get_default_impus(), trail, stopwatch, irs);

    if (status == Store::Status::OK)
    {
       result = new BaseImsSubscription(irs);
    }

    delete mapping;
  }

  return status;
}

Store::Status MemcachedCache::put_ims_subscription(ImsSubscription* subscription,
                                                   progress_callback progress_cb,
                                                   SAS::TrailId trail,
                                                   Utils::StopWatch* stopwatch)
{
  store_action action =
    std::bind(&MemcachedCache::put_ims_sub_action, this, subscription, trail, _1, _2);
  Store::Status status = perform(action, progress_cb, stopwatch);
  return status;
}

Store::Status MemcachedCache::put_ims_sub_action(ImsSubscription* subscription,
                                                 SAS::TrailId trail,
                                                 ImpuStore* store,
                                                 Utils::StopWatch* stopwatch)
{
  Store::Status status = Store::Status::OK;

  BaseImsSubscription* mis = (BaseImsSubscription*)subscription;

  for (BaseImsSubscription::Irs::value_type& irs : mis->get_irs())
  {
    MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs.second;
    put_irs_action(mirs, trail, store, stopwatch);
  }

  return status;
}
