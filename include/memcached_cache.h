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

#include <map>
#include <string>
#include <vector>

class MemcachedImsSubscription : public ImsSubscription
{
public:
  MemcachedImsSubscription(ImpuStore::ImpiMapping*& mapping,
                           std::vector<ImplicitRegistrationSet*>& irss) :
    ImsSubscription(),
    _irss(irss)
  {
  }

  std::vector<ImplicitRegistrationSet*>& get_irs()
  {
    return _irss;
  }

private:
  std::vector<ImplicitRegistrationSet*> _irss;
};

class MemcachedImplicitRegistrationSet : public ImplicitRegistrationSet
{
public:
  MemcachedImplicitRegistrationSet(ImpuStore::DefaultImpu* default_impu) :
    ImplicitRegistrationSet(default_impu->impu),
    _store(default_impu->store),
    _cas(default_impu->cas),
    _changed(false),
    _refreshed(false),
    _existing(true)
  {
  }

  MemcachedImplicitRegistrationSet(const std::string& default_impu) :
    ImplicitRegistrationSet(default_impu),
    _store(nullptr),
    _cas(0L),
    _changed(true),
    _refreshed(true),
    _existing(false)
  {
  }

  // Inherited functions

  virtual std::string get_ims_sub_xml() const
  {
    return _ims_sub_xml;
  }

  virtual RegistrationState get_reg_state() const
  {
    return _reg_state;
  }

  virtual std::vector<std::string> get_associated_impis() const
  {
    std::vector<std::string> impis;

    for (const std::pair<std::string, State>& entry : _impis)
    {
      if (entry.second == State::UNCHANGED || entry.second == State::DELETED)
      {
        impis.push_back(entry.first);
      }
    }

    return impis;
  }

  virtual std::vector<std::string> get_associated_impus() const
  {
    std::vector<std::string> associated_impus;

    for (const std::pair<std::string, State>& entry : _associated_impus)
    {
      if (entry.second == State::UNCHANGED || entry.second == State::DELETED)
      {
        associated_impus.push_back(entry.first);
      }
    }

    return associated_impus;
  }

  virtual const ChargingAddresses& get_charging_addresses() const
  {
    return _charging_addresses;
  }

  virtual int32_t get_ttl() const
  {
    return _ttl;
  }

  virtual void set_ims_sub_xml(std::string xml)
  {
    _ims_sub_xml = xml;
  }

  virtual void set_reg_state(RegistrationState state)
  {
    _reg_state = state;
  }

  virtual void set_associated_impus(std::vector<std::string> impis);
  virtual void set_associated_impis(std::vector<std::string> impis);

  virtual void set_charging_addresses(ChargingAddresses addresses)
  {
    _charging_addresses = addresses;
  }

  virtual void set_ttl(int32_t ttl)
  {
    _ttl = ttl;
  }

  // Functions for MemcachedCache

  bool is_existing(){ return _existing; }
  bool has_changed(){ return _changed; }
  bool is_refreshed(){ return _refreshed; }

  void mark_as_refreshed(){ _refreshed = true; }

  // Get an IMPU representing this IRS without any CAS
  ImpuStore::DefaultImpu* get_impu();

  // Get an IMPU representing this IRS based on the given IMPU's CAS value
  ImpuStore::DefaultImpu* get_impu_from_impu(ImpuStore::Impu* with_cas);

  // Get an IMPU for this IRS representing the given store
  ImpuStore::DefaultImpu* get_impu_for_store(ImpuStore* store);

  // Update the IRS with an IMPU with some details from the store
  void update_from_store(ImpuStore::DefaultImpu* impu);

  // Delete all of the associated IMPUs
  void delete_assoc_impus();

  // Delete all of the IMPIs
  void delete_impis();

private:
  const ImpuStore* _store;
  const uint64_t _cas;

  std::string _ims_sub_xml;
  RegistrationState _reg_state;
  ChargingAddresses _charging_addresses;
  int32_t _ttl;

  bool _changed;
  bool _refreshed;
  bool _existing;

  enum State
  {
    ADDED,
    UNCHANGED,
    DELETED
  };

  typedef std::map<std::string, State> Data;

  static std::vector<std::string> get(const Data& data, State status)
  {
    std::vector<std::string> v;
    for (const std::pair<const std::string, State>& entry : data)
    {
      if (entry.second == status)
      {
        v.push_back(entry.first);
      }
    }

    return v;
  }

  Data _impis;
  Data _associated_impus;

  RegistrationState _registration_state;
  bool _registration_state_set = false;

  ImpuStore::DefaultImpu* create_impu(uint64_t cas);

public:
  std::vector<std::string> impis(State status)
  {
    return get(_impis, status);
  }

  std::vector<std::string> impus(State status)
  {
    return get(_associated_impus, status);
  }

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

  // Per Store IRS methods

  typedef Store::Status (MemcachedCache::*irs_store_action)(MemcachedImplicitRegistrationSet*,
                                                            SAS::TrailId,
                                                            ImpuStore*);

  Store::Status perform(irs_store_action action,
                        MemcachedImplicitRegistrationSet*,
                        SAS::TrailId trail);

  Store::Status put_implicit_registration_set(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  Store::Status delete_implicit_registration_set(MemcachedImplicitRegistrationSet* irs,
                                                 SAS::TrailId trail,
                                                 ImpuStore* store);

  // IRS IMPU handling methods

  Store::Status create_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                SAS::TrailId trail,
                                ImpuStore* store);

  Store::Status update_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                SAS::TrailId trail,
                                ImpuStore* store);

  Store::Status delete_irs_impu(MemcachedImplicitRegistrationSet* irs,
                                SAS::TrailId trail,
                                ImpuStore* store);

  // Associated IMPU handling

  Store::Status update_irs_associated_impus(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);

  // IMPI Mapping Handling

  Store::Status update_irs_impi_mappings(MemcachedImplicitRegistrationSet* irs,
                                              SAS::TrailId trail,
                                              ImpuStore* store);
};

#endif
