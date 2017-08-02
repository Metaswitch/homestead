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
    _thread_pool->wait_stopped();
  }
}

void HssCacheProcessor::get_implicit_registration_set_for_impu(irs_success_callback success_cb,
                                                               failure_callback failure_cb,
                                                               std::string impu)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impu, success_cb, failure_cb]()->void {
    ImplicitRegistrationSet* result = NULL;
    Store::Status rc = _cache->get_implicit_registration_set_for_impu(impu, result);

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

void get_implicit_registration_sets_for_impis(irs_vector_success_callback success_cb,
                                              failure_callback failure_cb,
                                              std::vector<std::string>* impis)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impis, success_cb, failure_cb]()->void {
    std::vector<ImplicitRegistrationSet*> result;
    Store::Status rc = _cache->get_implicit_registration_sets_for_impis(impis, result);

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

void get_implicit_registration_sets_for_impus(irs_vector_success_callback success_cb,
                                              failure_callback failure_cb,
                                              std::vector<std::string>* impus)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impus, success_cb, failure_cb]()->void {
    std::vector<ImplicitRegistrationSet*> result;
    Store::Status rc = _cache->get_implicit_registration_sets_for_impus(impus, result);

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

void put_implicit_registration_set(void_success_cb success_cb,
                                   failure_callback failure_cb,
                                   ImplicitRegistrationSet* irs)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irs, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->put_implicit_registration_set(irs);

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

void delete_implicit_registration_set(void_success_cb success_cb,
                                      failure_callback failure_cb,
                                      ImplicitRegistrationSet* irs)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irs, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->delete_implicit_registration_set(irs);

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

void delete_implicit_registration_sets(void_success_cb success_cb,
                                       failure_callback failure_cb,
                                       std::vector<ImplicitRegistrationSet*>* irss)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, irss, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->delete_implicit_registration_sets(irss);

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

void get_ims_subscription(ims_sub_success_cb success_cb,
                          failure_callback failure_cb,
                          std::string impi)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, impi, success_cb, failure_cb]()->void {
    ImsSubscription* result = NULL;
    Store::Status rc = _cache->get_ims_subscription(impi, result);

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

void put_ims_subscription(void_success_cb success_cb,
                          failure_callback failure_cb,
                          ImsSubscription* subscription);
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, subscription, success_cb, failure_cb]()->void {
    Store::Status rc = _cache->put_ims_subscription(subscription);

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

void list_impus(impi_vector_success_callback success_cb,
                failure_callback failure_cb,
                int count,
                std::string last_impu)
{
  // Create a work item that can run on the thread pool, capturing required
  // variables to complete the work
  std::function<void()> work = [this, count, last_impu, success_cb, failure_cb]()->void {
    std::vector<std::string>* result = NULL;
    Store::Status rc = _cache->list_impus(count, last_impu, result);

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