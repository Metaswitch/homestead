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

#include "log.h"

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
    get_implicit_registration_sets_for_impus(mapping->default_impus, trail, result);
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

Store::Status MemcachedCache::put_implicit_registration_set(ImplicitRegistrationSet* irs,
                                            SAS::TrailId trail)
{
  return Store::Status::OK;
}

Store::Status MemcachedCache::delete_implicit_registration_set(ImplicitRegistrationSet* irs,
                                               SAS::TrailId trail)
{
  return Store::Status::OK;
}

Store::Status MemcachedCache::delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                SAS::TrailId trail)
{
  return Store::Status::OK;
}

Store::Status MemcachedCache::get_ims_subscription(const std::string& impi,
                                   SAS::TrailId trail,
                                   ImsSubscription*& result)
{
  ImpuStore::ImpiMapping* mapping = get_impi_mapping_gr(impi, trail);
  std::vector<ImplicitRegistrationSet*> irs;
  get_implicit_registration_sets_for_impus(mapping->default_impus, trail, irs);

  if (mapping)
  {
    result = new MemcachedImsSubscription(mapping, irs);

    delete mapping;
  }

  return mapping ? Store::Status::OK : Store::Status::NOT_FOUND;
}

Store::Status MemcachedCache::put_ims_subscription(ImsSubscription* subscription,
                                   SAS::TrailId trail)
{
  return Store::Status::OK;
}
