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
#ifndef MEMCACHED_CACHE_H_
#define MEMCACHED_CACHE_H_

#include "hss_cache.h"
#include "impu_store.h"

class MemcachedImsSubscription : public ImsSubscription
{
public:
  MemcachedImsSubscription(ImpuStore::ImpiMapping*& mapping,
                           std::vector<ImplicitRegistrationSet*>& irss) :
    ImsSubscription()
  {
  }
};

class MemcachedImplicitRegistrationSet : public ImplicitRegistrationSet
{
public:
  MemcachedImplicitRegistrationSet(ImpuStore::DefaultImpu* default_impu) :
    ImplicitRegistrationSet(default_impu->impu),
    _store(default_impu->store)
  {
  }

  MemcachedImplicitRegistrationSet(const std::string& default_impu) :
    ImplicitRegistrationSet(default_impu),
    _store(nullptr)
  {
  }

  bool is_existing(){ return _existing; }
  bool has_changed(){ return _changed; }
  bool is_refreshed(){ return _refreshed; }

  const std::vector<std::string>& added_impis(){ return _added_impis; }
  const std::vector<std::string>& unchanged_impis(){ return _unchanged_impis; }
  const std::vector<std::string>& deleted_impis(){ return _deleted_impis; }

  std::vector<std::string>& added_associated_impus(){ return _added_associated_impus; }
  std::vector<std::string>& unchanged_associated_impus(){ return _unchanged_associated_impus; }
  std::vector<std::string>& deleted_associated_impus(){ return _deleted_associated_impus; }

  // Get an IMPU representing this IRS without any CAS
  ImpuStore::DefaultImpu* get_impu();

  // Get an IMPU representing this IRS based on the given IMPU's CAS value
  ImpuStore::DefaultImpu* get_impu_from_impu(ImpuStore::Impu* with_cas);

  // Get an IMPU for this IRS representing the given store
  ImpuStore::DefaultImpu* get_impu_for_store(ImpuStore* store);

  // Update the IRS with an IMPU with some details from the store
  void update_from_store(ImpuStore::DefaultImpu* impu);

private:
  bool _changed = false;
  bool _refreshed = false;
  bool _existing = true;

  const ImpuStore* _store;

  std::vector<std::string> _added_impis;
  std::vector<std::string> _unchanged_impis;
  std::vector<std::string> _deleted_impis;

  std::vector<std::string> _added_associated_impus;
  std::vector<std::string> _unchanged_associated_impus;
  std::vector<std::string> _deleted_associated_impus;
};

class MemcachedCache : public HssCache
{
public:
  MemcachedCache(ImpuStore* local_store,
                 std::vector<ImpuStore*> remote_stores) :
    HssCache(),
    _local_store(local_store),
    _remote_stores(remote_stores)
  {
  }

  virtual ~MemcachedCache()
  {
  }

  // Create an IRS for the given IMPU
  virtual ImplicitRegistrationSet* create_implicit_registration_set(const std::string& impu)
  {
    return new MemcachedImplicitRegistrationSet(impu);
  }

  // Get the IRS for a given IMPU
  virtual Store::Status get_implicit_registration_set_for_impu(const std::string& impu,
                                                               SAS::TrailId trail,
                                                               ImplicitRegistrationSet*& result);

  // Get the list of IRSs for the given list of impus
  // Used for RTR when we have a list of impus
  virtual Store::Status get_implicit_registration_sets_for_impis(const std::vector<std::string>& impis,
                                                                 SAS::TrailId trail,
                                                                 std::vector<ImplicitRegistrationSet*>& result);

  // Get the list of IRSs for the given list of imps
  // Used for RTR when we have a list of impis
  virtual Store::Status get_implicit_registration_sets_for_impus(const std::vector<std::string>& impus,
                                                                 SAS::TrailId trail,
                                                                 std::vector<ImplicitRegistrationSet*>& result);

  // Save the IRS in the cache
  // Must include updating the impi mapping table if impis have been added
  virtual Store::Status put_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                      SAS::TrailId trail);

  // Used for de-registration
  virtual Store::Status delete_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                         SAS::TrailId trail);

  // Deletes several registration sets
  // Used for an RTR when we have several registration sets to delete
  virtual Store::Status delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                          SAS::TrailId trail);

  // Gets the whole IMS subscription for this impi
  // This is used when we get a PPR, and we have to update charging functions
  // as we'll need to updated every IRS that we've stored
  virtual Store::Status get_ims_subscription(const std::string& impi,
                                             SAS::TrailId trail,
                                             ImsSubscription*& result);

  // This is used to save the state that we changed in the PPR
  virtual Store::Status put_ims_subscription(ImsSubscription* subscription,
                                             SAS::TrailId trail);
private:
  ImpuStore* _local_store;
  std::vector<ImpuStore*> _remote_stores;

  ImpuStore::Impu* get_impu_for_impu_gr(const std::string& impu,
                                        SAS::TrailId trail);

  ImpuStore::ImpiMapping* get_impi_mapping_gr(const std::string& impi,
                                              SAS::TrailId trail);
  Store::Status put_implicit_registration_set(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  Store::Status create_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  Store::Status update_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  Store::Status update_irs_associated_impus(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  Store::Status update_irs_impi_mappings(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);
};

#endif
