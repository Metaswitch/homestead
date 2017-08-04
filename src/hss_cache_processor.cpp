/**
 * @file hss_cache_processor.cpp Class that interfaces with the HssCache.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "hss_cache_processor.h"


HssCacheProcessor::HssCacheProcessor(HssCache* cache) :
  _cache(cache),
  _thread_pool(NULL)
{
}

bool HssCacheProcessor::start_threads(int num_threads,
                                      ExceptionHandler* exception_handler,
                                      unsigned int max_queue)
{
  TRC_INFO("Starting threadpool with %d threads", num_threads);
  _thread_pool = new FunctorThreadPool(num_threads,
                                       exception_handler,
                                       exception_callback,
                                       max_queue);

  return _thread_pool->start();
}

void HssCacheProcessor::stop()
{
  TRC_STATUS("Stopping threadpool");
  if (_thread_pool)
  {
    _thread_pool->stop();
  }
}

void HssCacheProcessor::wait_stopped()
{
  TRC_STATUS("Waiting for threadpool to stop");
  if (_thread_pool)
  {
    _thread_pool->join();
    delete _thread_pool; _thread_pool = NULL;
  }
}

void HssCacheProcessor::get_implicit_registration_set_for_impu(irs_success_callback success_cb,
                                                               failure_callback failure_cb,
                                                               std::string impu,
                                                               SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impu, trail, success_cb, failure_cb]()->void {
    ImplicitRegistrationSet* result = NULL;
    Store::Status rc = _cache->get_implicit_registration_set_for_impu(impu,
                                                                      trail,
                                                                      result);

    if (rc == Store::Status::OK)
    {
      success_cb(result);
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::get_implicit_registration_sets_for_impis(irs_vector_success_callback success_cb,
                                                                 failure_callback failure_cb,
                                                                 std::vector<std::string> impis,
                                                                 SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impis, trail, success_cb, failure_cb]()->void {
    std::vector<ImplicitRegistrationSet*> result;
    Store::Status rc = _cache->get_implicit_registration_sets_for_impis(impis,
                                                                        trail,
                                                                        result);

    if (rc == Store::Status::OK)
    {
      success_cb(result);
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::get_implicit_registration_sets_for_impus(irs_vector_success_callback success_cb,
                                              failure_callback failure_cb,
                                              std::vector<std::string> impus,
                                              SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impus, trail, success_cb, failure_cb]()->void {
    std::vector<ImplicitRegistrationSet*> result;
    Store::Status rc = _cache->get_implicit_registration_sets_for_impus(impus,
                                                                        trail,
                                                                        result);

    if (rc == Store::Status::OK)
    {
      success_cb(result);
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::put_implicit_registration_set(void_success_cb success_cb,
                                   failure_callback failure_cb,
                                   ImplicitRegistrationSet* irs,
                                   SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irs, trail, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->put_implicit_registration_set(irs, trail);

    if (rc == Store::Status::OK)
    {
      success_cb();
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::delete_implicit_registration_set(void_success_cb success_cb,
                                      failure_callback failure_cb,
                                      ImplicitRegistrationSet* irs,
                                      SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irs, trail, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->delete_implicit_registration_set(irs, trail);

    if (rc == Store::Status::OK)
    {
      success_cb();
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::delete_implicit_registration_sets(void_success_cb success_cb,
                                       failure_callback failure_cb,
                                       std::vector<ImplicitRegistrationSet*> irss,
                                       SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irss, trail, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->delete_implicit_registration_sets(irss, trail);

    if (rc == Store::Status::OK)
    {
      success_cb();
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::get_ims_subscription(ims_sub_success_cb success_cb,
                                             failure_callback failure_cb,
                                             std::string impi,
                                             SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impi, trail, success_cb, failure_cb]()->void {
    ImsSubscription* result = NULL;
    Store::Status rc = _cache->get_ims_subscription(impi, trail, result);

    if (rc == Store::Status::OK)
    {
      success_cb(result);
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}

void HssCacheProcessor::put_ims_subscription(void_success_cb success_cb,
                                             failure_callback failure_cb,
                                             ImsSubscription* subscription,
                                             SAS::TrailId trail)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, subscription, trail, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->put_ims_subscription(subscription, trail);

    if (rc == Store::Status::OK)
    {
      success_cb();
    }
    else
    {
      failure_cb(rc);
    }
  };

  // Add the work to the pool
  _thread_pool->add_work(work);
}