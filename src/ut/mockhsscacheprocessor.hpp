/**
 * @file mockhsscacheprocessor.hpp Mock HssCacheProcessor.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKHSSCACHEPROCESSOR_H__
#define MOCKHSSCACHEPROCESSOR_H__

#include "hss_cache_processor.h"
#include "gmock/gmock.h"

class MockHssCacheProcessor : public HssCacheProcessor
{
public:
  MockHssCacheProcessor() : HssCacheProcessor(NULL) {};
  virtual ~MockHssCacheProcessor() {};

  MOCK_METHOD0(create_implicit_registration_set,
               ImplicitRegistrationSet*());

  MOCK_METHOD4(get_implicit_registration_set_for_impu,
               void(irs_success_callback success_cb,
                    failure_callback failure_cb,
                    std::string impu,
                    SAS::TrailId trail));

  MOCK_METHOD4(get_implicit_registration_sets_for_impis,
               void(irs_vector_success_callback success_cb,
                    failure_callback failure_cb,
                    std::vector<std::string> impis,
                    SAS::TrailId trail));

  MOCK_METHOD4(get_implicit_registration_sets_for_impus,
               void(irs_vector_success_callback success_cb,
                    failure_callback failure_cb,
                    std::vector<std::string> impus,
                    SAS::TrailId trail));

  MOCK_METHOD4(put_implicit_registration_set,
               void(void_success_cb success_cb,
                    failure_callback failure_cb,
                    ImplicitRegistrationSet* irs,
                    SAS::TrailId trail));

  MOCK_METHOD4(delete_implicit_registration_set,
               void(void_success_cb success_cb,
                    failure_callback failure_cb,
                    ImplicitRegistrationSet* irs,
                    SAS::TrailId trail));

  MOCK_METHOD4(delete_implicit_registration_sets,
               void(void_success_cb success_cb,
                    failure_callback failure_cb,
                    std::vector<ImplicitRegistrationSet*> irss,
                    SAS::TrailId trail));

  MOCK_METHOD4(get_ims_subscription,
               void(ims_sub_success_cb success_cb,
                    failure_callback failure_cb,
                    std::string impi,
                    SAS::TrailId trail));

  MOCK_METHOD4(put_ims_subscription,
               void(void_success_cb success_cb,
                    failure_callback failure_cb,
                    ImsSubscription* subscription,
                    SAS::TrailId trail));

};

#endif
