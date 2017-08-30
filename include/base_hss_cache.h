/**
 * Base implementation of a HSS Cache
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef BASE_HSS_CACHE_H_
#define BASE_HSS_CACHE_H_

#include "hss_cache.h"

class BaseHssCache : public HssCache
{
public:
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

  // Deletes several registration sets
  // Used for an RTR when we have several registration sets to delete
  virtual Store::Status delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                          SAS::TrailId trail);

protected:
  virtual Store::Status get_implicit_registration_sets_for_impi(const std::string& impi,
                                                                                                                              SAS::TrailId trail,
                                                                                                                              std::vector<ImplicitRegistrationSet*>& result);

  virtual Store::Status get_impus_for_impi(const std::string& impi,
                                           SAS::TrailId trail,
                                           std::vector<std::string>& impus) = 0;
};

#endif
