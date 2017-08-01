/**
 * @file hss_cache_processor.h Class that interfaces with the HssCache.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSS_CACHE_PROCESSOR_H_
#define HSS_CACHE_PROCESSOR_H_

#include "hss_cache.h"
#include "threadpool.h"
#include "ims_subscription.h"

class HssCacheProcessor
{
public:
  ~HssCacheProcessor();

  // Creates the HssCacheProcessor, but not the thread pool.
  // start_threads must be called to create and start the thread pool.
  HssCacheProcessor(HssCache* cache);

  // Starts the threadpool with the required number of threads
  bool start_threads(int num_threads,
                     ExceptionHandler* exception_handler,
                     unsigned int max_queue);

  // ---------------------------------------------------------------------------
  // Funtions to get/set data in the cache.
  // Each one must provide a success and failure callback.
  // The request is run on the threadpool and the appropriate callback called.
  // ---------------------------------------------------------------------------
  void put_associated_private_id(std::function<void()> success_cb,
                                 std::function<void(Store::Status)> failure_cb,
                                 std::vector<std::string> impus,
                                 std::string default_public_id,
                                 std::string impi,
                                 int ttl);

  void get_ims_subscription_xml(std::function<void(ImsSubscription)> success_cb,
                                std::function<void(Store::Status)> failure_cb,
                                std::string impu);

  void put_ims_subscription_xml(std::function<void()> success_cb,
                                std::function<void(Store::Status)> failure_cb,
                                ImsSubscription xml,
                                int ttl);

  void delete_public_ids(std::function<void()> success_cb,
                         std::function<void(Store::Status)> failure_cb,
                         std::vector<std::string> impis,
                         std::vector<std::string> impus);

  void list_impus(std::function<void(std::vector<std::string>)> success_cb,
                  std::function<void(Store::Status)> failure_cb);

  void get_associated_primary_public_ids(std::function<void(std::vector<std::string>)> success_cb,
                                         std::function<void(Store::Status)> failure_cb,
                                         std::vector<std::string> impis);
  
  void dissociate_irs_from_impis(std::function<void()> success_cb,
                                 std::function<void(Store::Status)> failure_cb,
                                 std::vector<std::string> impis,
                                 std::vector<std::string> impus,
                                 bool delete_impi_mappings);

  void put_ppr_data(std::function<void()> success_cb,
                    std::function<void(Store::Status)> failure_cb,
                    std::string impis,
                    ImsSubscription ppr_data);

private:
  // Dummy exception handler callback for the thread pool
  static void inline exception_callback(std::function<void()> callable)
  {

  }

  // The actual HssCache object used to store the data
  HssCache* _cache;

  // The threadpool on which the requests are run.
  FunctorThreadPool* _thread_pool;
};

#endif
