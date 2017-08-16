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

#include <string>

#include "memcached_cache.h"

#include "log.h"

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::create_impu(uint64_t cas)
{
  std::vector<std::string> impis = get_associated_impis();
  std::vector<std::string> impus = get_associated_impus();

  return new ImpuStore::DefaultImpu(default_impu,
                                    impus,
                                    impis,
                                    _registration_state,
                                    get_ims_sub_xml(),
                                    cas);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu()
{
  return create_impu(0L);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu_from_impu(ImpuStore::Impu* with_cas)
{
  return create_impu(with_cas->cas);
}

ImpuStore::DefaultImpu* MemcachedImplicitRegistrationSet::get_impu_for_store(ImpuStore* store)
{
  if (_store == store)
  {
    return create_impu(_cas);
  }
  else
  {
    return nullptr;
  }
}

bool in_vector(const std::string& element,
               const std::vector<std::string>& vec)
{
  return std::find(vec.begin(), vec.end(), element) != vec.end();
}

void update_from_store(const std::vector<std::string>& store,
                       MemcachedImplicitRegistrationSet::Data data,
                       MemcachedImplicitRegistrationSet::State state)
{
  for (const std::string& member : store)
  {
    data.emplace(member, state);
  }
}

void set_elements(const std::vector<std::string>& updated,
                  MemcachedImplicitRegistrationSet::Data& data)
{
  for (std::pair<const std::string, MemcachedImplicitRegistrationSet::State>& entry : data)
  {
    if (!in_vector(entry.first, updated))
    {
      entry.second = MemcachedImplicitRegistrationSet::State::DELETED;
    }
  }

  for (const std::string& entry : updated)
  {
    MemcachedImplicitRegistrationSet::State& value = data[entry];

    switch (value)
    {
      case MemcachedImplicitRegistrationSet::State::ADDED:
      case MemcachedImplicitRegistrationSet::State::UNCHANGED:
        break;
      case MemcachedImplicitRegistrationSet::State::DELETED:
        value = MemcachedImplicitRegistrationSet::State::ADDED;
    }
  }
}

void MemcachedImplicitRegistrationSet::set_associated_impis(std::vector<std::string> impis)
{
  set_elements(impis, _impis);
}

void MemcachedImplicitRegistrationSet::set_associated_impus(std::vector<std::string> impus)
{
  set_elements(impus, _associated_impus);
}

void MemcachedImplicitRegistrationSet::update_from_store(ImpuStore::DefaultImpu* impu)
{
  MemcachedImplicitRegistrationSet::State state;

  // Update the IRS with the details in impu
  if (_refreshed)
  {
    // If we are marked as refreshed, then the data from the store is *less* up
    // to date than our data, so we should mark it as changed.

    state = MemcachedImplicitRegistrationSet::State::DELETED;
  }
  else
  {
    // If we are marked as not refreshed, the data from the store should be
    // considered valid, and we should mark it as unchanged, if we weren't
    // previously aware of it.

    state = MemcachedImplicitRegistrationSet::State::UNCHANGED;
  }

  if (!_registration_state_set)
  {
    _registration_state = impu->registration_state;
  }

  ::update_from_store(impu->impis,
                      _impis,
                      state);

  ::update_from_store(impu->associated_impus,
                      _associated_impus,
                      state);
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

// Helper function to the details of an IMPU.
// Note this doesn't sort out associated versus default impus.
ImpuStore::Impu* MemcachedCache::get_impu_for_impu_gr(const std::string& impu,
                                                     SAS::TrailId trail)
{
  ImpuStore::Impu* data =  _local_store->get_impu(impu, trail);

  if (!data)
  {
    for (ImpuStore* remote_store : _remote_stores)
    {
      data = remote_store->get_impu(impu, trail);

      if (data)
      {
        break;
      }
    }
  }

  return data;
}

ImpuStore::ImpiMapping* MemcachedCache::get_impi_mapping_gr(const std::string& impi,
                                                           SAS::TrailId trail)
{
  ImpuStore::ImpiMapping* mapping = _local_store->get_impi_mapping(impi, trail);

  if (!mapping)
  {
    for (ImpuStore* remote_store : _remote_stores)
    {
      mapping = remote_store->get_impi_mapping(impi, trail);

      if (mapping)
      {
        break;
      }
    }
  }

  return mapping;
}

Store::Status MemcachedCache::get_implicit_registration_set_for_impu(const std::string& impu,
                                                     SAS::TrailId trail,
                                                     ImplicitRegistrationSet*& result)
{
  ImpuStore::Impu* data = get_impu_for_impu_gr(impu, trail);

  if (data && !data->is_default_impu())
  {
    ImpuStore::AssociatedImpu* assoc_impu = (ImpuStore::AssociatedImpu*)data;

    TRC_INFO("IMPU: %s maps to IMPU: %s", impu.c_str(), assoc_impu->default_impu.c_str());

    ImpuStore::Impu* data = get_impu_for_impu_gr(assoc_impu->default_impu, trail);

    delete assoc_impu;

    if (data)
    {
      bool is_default = data->is_default_impu();
      bool is_associated = (is_default && ((ImpuStore::DefaultImpu*)data)->has_associated_impu(impu));

      if (!is_default || !is_associated)
      {
        if (!is_default)
        {
          TRC_INFO("None default IMPU pointed by associated IMPU record");
        }
        else if (!is_associated)
        {
          TRC_INFO("Default IMPU does not contain IMPU as associated");
        }

        delete data;
        return Store::Status::NOT_FOUND;
      }
    }
  }

  if (!data)
  {
    TRC_INFO("No IMPU record found");
    return Store::Status::NOT_FOUND;
  }

  result = new MemcachedImplicitRegistrationSet((ImpuStore::DefaultImpu*) data);
  delete data;

  return Store::Status::OK;
}

Store::Status MemcachedCache::get_implicit_registration_sets_for_impis(const std::vector<std::string>& impis,
                                                                       SAS::TrailId trail,
                                                                       std::vector<ImplicitRegistrationSet*>& result)
{
  for (const std::string& impi : impis)
  {
    ImpuStore::ImpiMapping* mapping = get_impi_mapping_gr(impi, trail);
    get_implicit_registration_sets_for_impus(mapping->get_default_impus(), trail, result);
  }

  return Store::Status::OK;
}

Store::Status MemcachedCache::get_implicit_registration_sets_for_impus(const std::vector<std::string>& impus,
                                                                       SAS::TrailId trail,
                                                                       std::vector<ImplicitRegistrationSet*>& result)
{
  for (const std::string& impu : impus)
  {
    ImplicitRegistrationSet* irs;
    get_implicit_registration_set_for_impu(impu, trail, irs);
    result.push_back(irs);
  }

  return Store::Status::OK;
}


Store::Status MemcachedCache::perform(MemcachedCache::irs_store_action action,
                                      MemcachedImplicitRegistrationSet* mirs,
                                      SAS::TrailId trail)
{
   Store::Status status = (this->*action)(mirs, trail, _local_store);

   if (status == Store::Status::OK)
   {
     for (ImpuStore* remote_store : _remote_stores)
     {
       status = (this->*action)(mirs, trail, remote_store);
     }
   }

   return status;
}

Store::Status MemcachedCache::put_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                            SAS::TrailId trail)
{
  Store::Status status = Store::Status::OK;

  MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs;

  if (mirs->has_changed())
  {
    status = perform(&MemcachedCache::put_implicit_registration_set, mirs, trail);
  }

  return status;
}

Store::Status MemcachedCache::update_irs_impi_mappings(MemcachedImplicitRegistrationSet* irs,
                                                       SAS::TrailId trail,
                                                       ImpuStore* store)
{
  Store::Status status = Store::Status::OK;

  // Remove old IMPI mappings
  for (const std::string& impi : irs->impis(MemcachedImplicitRegistrationSet::State::DELETED))
  {
    do
    {
      ImpuStore::ImpiMapping* mapping = store->get_impi_mapping(impi, trail);

      if (mapping)
      {
        if (mapping->has_default_impu(irs->default_impu))
        {
          mapping->remove_default_impu(irs->default_impu);

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
        ImpuStore::ImpiMapping* mapping = store->get_impi_mapping(impi, trail);

        if (mapping)
        {
          if (!mapping->has_default_impu(irs->default_impu))
          {
            mapping->add_default_impu(irs->default_impu);
          }
        }
        else
        {
          mapping = new ImpuStore::ImpiMapping(impi, irs->default_impu);
        }

        status = store->set_impi_mapping(mapping, trail);

        delete mapping;
      } while(status == Store::Status::DATA_CONTENTION);
    }
  }

  // Add new IMPIs
  for (const std::string& impi : irs->impis(MemcachedImplicitRegistrationSet::State::ADDED))
  {
    ImpuStore::ImpiMapping* mapping = new ImpuStore::ImpiMapping(impi, irs->default_impu);

    do
    {
      status = store->set_impi_mapping(mapping, trail);

      if (status == Store::Status::DATA_CONTENTION)
      {
        mapping = store->get_impi_mapping(impi, trail);

        if (!mapping->has_default_impu(irs->default_impu))
        {
          mapping->add_default_impu(irs->default_impu);
        }
        else if (!irs->is_refreshed())
        {
          // We aren't being refreshed, and the IMPU is present, so just mark
          // the data as good
          status = Store::Status::OK;
        }
      }
    } while(status == Store::Status::DATA_CONTENTION);
  }

  return status;
}

Store::Status MemcachedCache::update_irs_associated_impus(MemcachedImplicitRegistrationSet* irs,
                                                          SAS::TrailId trail,
                                                          ImpuStore* store)
{
  Store::Status status = Store::Status::OK;

  // Remove old associated IMPUs
  for (const std::string& associated_impu : irs->impus(MemcachedImplicitRegistrationSet::State::DELETED))
  {
    do
    {
      ImpuStore::Impu* mapping = store->get_impu(associated_impu, trail);

      if (mapping && !mapping->is_default_impu())
      {
        ImpuStore::AssociatedImpu* assoc_mapping = (ImpuStore::AssociatedImpu*)mapping;

        if (assoc_mapping->default_impu == irs->default_impu)
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
      ImpuStore::AssociatedImpu* impu = new ImpuStore::AssociatedImpu(associated_impu, irs->default_impu, 0L);

      store->set_impu_without_cas(impu, trail);
    }
  }

  // Add new associated IMPUs
  for (const std::string& associated_impu : irs->impus(MemcachedImplicitRegistrationSet::State::ADDED))
  {
    ImpuStore::AssociatedImpu* impu = new ImpuStore::AssociatedImpu(associated_impu, irs->default_impu, 0L);

    store->set_impu_without_cas(impu, trail);
  }

  return status;
}

Store::Status MemcachedCache::create_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store)
{
  ImpuStore::DefaultImpu* impu = irs->get_impu();

  Store::Status status = store->set_impu_without_cas(impu, trail);

  return status;
}

typedef Store::Status (ImpuStore::*irs_impu_action)(ImpuStore::Impu*, SAS::TrailId);

Store::Status perform_irs_impu_action(irs_impu_action action,
                                      MemcachedImplicitRegistrationSet* irs,
                                      SAS::TrailId trail,
                                      ImpuStore* store)
{
  Store::Status status = Store::Status::OK;
  ImpuStore::DefaultImpu* impu = irs->get_impu_for_store(store);

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
      // have any idea how to CAS this data into place. Start by fetching
      // the stores view of the data. As we merge in remote stores, we'll get a
      // fuller picture of the data, but we don't attempt to update local
      // stores, and just assume they'll fall into place.
      ImpuStore::Impu* mapped_impu = store->get_impu(irs->default_impu, trail);

      if (mapped_impu->is_default_impu())
      {
        ImpuStore::DefaultImpu* default_impu = (ImpuStore::DefaultImpu*) mapped_impu;

        // Merge details from the store into our IRS.
        irs->update_from_store(default_impu);
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
    }

    delete impu; impu = nullptr;

  } while(status == Store::Status::DATA_CONTENTION);

  return status;
}

Store::Status MemcachedCache::update_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store)
{
  return perform_irs_impu_action(&ImpuStore::set_impu,
                                   irs,
                                   trail,
                                   store);
}

Store::Status MemcachedCache::delete_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store)
{
  // If we are deleting an MIRS, we are refreshing it.
  irs->mark_as_refreshed();
  return perform_irs_impu_action(&ImpuStore::delete_impu,
                                 irs,
                                 trail,
                                 store);
}

Store::Status MemcachedCache::put_implicit_registration_set(MemcachedImplicitRegistrationSet* irs,
                                                            SAS::TrailId trail,
                                                            ImpuStore* store)
{
  Store::Status status;

  if (irs->is_existing())
  {
    status = update_irs_impu(irs, trail, store);
  }
  else
  {
    status = create_irs_impu(irs, trail, store);
  }

  if (status == Store::Status::OK)
  {
    update_irs_associated_impus(irs, trail, store);
  }

  if (status == Store::Status::OK)
  {
    update_irs_impi_mappings(irs, trail, store);
  }

  return status;
}

Store::Status MemcachedCache::delete_implicit_registration_set(MemcachedImplicitRegistrationSet* irs,
                                                               SAS::TrailId trail,
                                                               ImpuStore* store)
{
  Store::Status status = delete_irs_impu(irs, trail, store);

  if (status == Store::Status::OK)
  {
    // Mark the Associated IMPUs as deleted, and update the store to match
    irs->delete_assoc_impus();
    update_irs_associated_impus(irs, trail, store);
  }

  if (status == Store::Status::OK)
  {
    // And similar with the IMPIs
    irs->delete_impis();
    update_irs_impi_mappings(irs, trail, store);
  }

  return status;
}

Store::Status MemcachedCache::delete_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                               SAS::TrailId trail)
{
  Store::Status status = Store::Status::OK;

  MemcachedImplicitRegistrationSet* mirs = (MemcachedImplicitRegistrationSet*)irs;

  if (mirs->is_existing())
  {
    status = perform(&MemcachedCache::delete_implicit_registration_set, mirs, trail);
  }
  else
  {
    TRC_WARNING("Attempted to delete IRS which hadn't been created: %s", irs->default_impu.c_str());
  }

  return status;
}

Store::Status MemcachedCache::delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                                SAS::TrailId trail)
{
  Store::Status status = Store::Status::OK;

  for (ImplicitRegistrationSet* irs : irss)
  {
    status = delete_implicit_registration_set(irs, trail);

    if (status != Store::Status::OK)
    {
      break;
    }
  }

  return Store::Status::OK;
}

Store::Status MemcachedCache::get_ims_subscription(const std::string& impi,
                                                   SAS::TrailId trail,
                                                   ImsSubscription*& result)
{
  Store::Status status;

  ImpuStore::ImpiMapping* mapping = get_impi_mapping_gr(impi, trail);

  if (mapping)
  {
    std::vector<ImplicitRegistrationSet*> irs;
    status = get_implicit_registration_sets_for_impus(mapping->get_default_impus(), trail, irs);

    if (status == Store::Status::OK)
    {
       result = new MemcachedImsSubscription(mapping, irs);
    }

    delete mapping;
  }
  else
  {
    status = Store::Status::NOT_FOUND;
  }

  return status;
}

Store::Status MemcachedCache::put_ims_subscription(ImsSubscription* subscription,
                                                   SAS::TrailId trail)
{
  Store::Status status = Store::Status::OK;

  MemcachedImsSubscription* mis = (MemcachedImsSubscription*)subscription;

  for (ImplicitRegistrationSet* irs : mis->get_irs())
  {
    put_implicit_registration_set(irs, trail);
  }

  return status;
}