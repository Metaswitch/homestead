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
#include "base_hss_cache.h"

Store::Status BaseHssCache::get_implicit_registration_sets_for_impi(const std::string& impi,
                                                                SAS::TrailId trail,
                                                                std::vector<ImplicitRegistrationSet*>& result)
{
  std::vector<std::string> impus;

  Store::Status status = get_impus_for_impi(impi, trail, impus);

  if (status == Store::Status::OK)
  {
    status = BaseHssCache::get_implicit_registration_sets_for_impus(impus, trail, result);
  }

  return status;
}

Store::Status BaseHssCache::get_implicit_registration_sets_for_impus(const std::vector<std::string>& impus,
                                                                     SAS::TrailId trail,
                                                                     std::vector<ImplicitRegistrationSet*>& result)
{
  Store::Status status = Store::Status::OK;

  for (const std::string& impu : impus)
  {
    ImplicitRegistrationSet* irs;
    Store::Status inner_status = get_implicit_registration_set_for_impu(impu, trail, irs);

    if (inner_status == Store::Status::OK)
    {
      result.push_back(irs);
      break;
    }
    // LCOV_EXCL_START
    // Not hittable in UTs
    else if (inner_status != Store::Status::NOT_FOUND)
    {
      status = inner_status;
      break;
    }
    // LCOV_EXCL_STOP
  }

  // LCOV_EXCL_START
  // Not hittable in UTs
  if (status != Store::Status::OK)
  {
    for (ImplicitRegistrationSet* irs : result)
    {
      delete irs;
    }

    result.clear();
  }
  // LCOV_EXCL_STOP

  return status;
}

Store::Status BaseHssCache::get_implicit_registration_sets_for_impis(const std::vector<std::string>& impis,
                                                                     SAS::TrailId trail,
                                                                     std::vector<ImplicitRegistrationSet*>& result)
{
  Store::Status status = Store::Status::OK;

  for (const std::string& impi : impis)
  {
    std::vector<ImplicitRegistrationSet*> inner_result;
    Store::Status inner_status = get_implicit_registration_sets_for_impi(impi, trail, inner_result);

    if (inner_status == Store::Status::OK)
    {
      result.insert(result.end(), inner_result.begin(), inner_result.end());
    }
    // LCOV_EXCL_START
    // Not hittable in UTs
    else if (inner_status != Store::Status::NOT_FOUND)
    {
      status = inner_status;
      break;
    }
    // LCOV_EXCL_STOP
  }

  // LCOV_EXCL_START
  // Not hittable in UTs
  if (status != Store::Status::OK)
  {
    for (ImplicitRegistrationSet*& irs : result)
    {
      delete irs;
    }

    result.clear();
  }
  // LCOV_EXCL_STOP

  return status;
}

Store::Status BaseHssCache::delete_implicit_registration_sets(const std::vector<ImplicitRegistrationSet*>& irss,
                                                              progress_callback progress_cb,
                                                              SAS::TrailId trail)
{
  Store::Status status = Store::Status::OK;

  for (ImplicitRegistrationSet* irs : irss)
  {
    Store::Status inner_status = delete_implicit_registration_set(irs, progress_cb, trail);

    if (inner_status != Store::Status::OK &&
        inner_status != Store::Status::NOT_FOUND)
    {
      status = inner_status;
      break;
    }
  }

  return status;
}
