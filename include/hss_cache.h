/**
 * @file hss_cache.h Abstract class definition of an Hss Cache.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSS_CACHE_H_
#define HSS_CACHE_H_

#include "store.h"
#include <vector>
#include <string>
#include "ims_subscription.h"
#include "implicit_reg_set.h"

class HssCache
{
public:
  virtual ~HssCache();

  // All of these methods are synchronous, and run on a thread that is OK to block
  // They return Store::Status (from cpp-common's Store) which is used to determine which callback to use
  // If they are getting/listing data, the data is put into the supplied datastructure

  // Create an IRS
  virtual ImplicitRegistrationSet* create_implicit_registration_set(const std::string& impu) = 0;

  // Get the IRS for a given impu
  virtual Store::Status get_implicit_registration_set_for_impu(std::string impu,
                                                               SAS::TrailId trail,
                                                               ImplicitRegistrationSet*& result) = 0;

  // Get the list of IRSs for the given list of impus
  // Used for RTR when we have a list of impus
  virtual Store::Status get_implicit_registration_sets_for_impis(std::vector<std::string> impis,
                                                                 SAS::TrailId trail,
                                                                 std::vector<ImplicitRegistrationSet*>& result) = 0;

  // Get the list of IRSs for the given list of imps
  // Used for RTR when we have a list of impis
  virtual Store::Status get_implicit_registration_sets_for_impus(std::vector<std::string> impus,
                                                                 SAS::TrailId trail,
                                                                 std::vector<ImplicitRegistrationSet*>& result) = 0;

  // Save the IRS in the cache
  // Must include updating the impi mapping table if impis have been added
  virtual Store::Status put_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                      SAS::TrailId trail) = 0;

  // Used for de-registration
  virtual Store::Status delete_implicit_registration_set(ImplicitRegistrationSet* irs,
                                                         SAS::TrailId trail) = 0;

  // Deletes several registration sets
  // Used for an RTR when we have several registration sets to delete
  virtual Store::Status delete_implicit_registration_sets(std::vector<ImplicitRegistrationSet*> irss,
                                                          SAS::TrailId trail) = 0;

  // Gets the whole IMS subscription for this impi
  // This is used when we get a PPR, and we have to update charging functions
  // as we'll need to updated every IRS that we've stored
  virtual Store::Status get_ims_subscription(std::string impi,
                                             SAS::TrailId trail,
                                             ImsSubscription*& result) = 0;

  // This is used to save the state that we changed in the PPR
  virtual Store::Status put_ims_subscription(ImsSubscription* subscription,
                                             SAS::TrailId trail) = 0;
};

#endif
