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

typedef std::function<void(Store::Status)> failure_callback;
typedef std::function<void(ImplicitRegistrationSet*)> irs_success_callback;
typedef std::function<void(std::vector<ImplicitRegistrationSet*>)> irs_vector_success_callback;
typedef std::function<void()> void_success_cb;
typedef std::function<void(ImsSubscription*)> ims_sub_success_cb;
typedef std::function<void(std::vector<std::string>*)> impi_vector_success_callback;

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

  // Stops the threadpool
  void stop();

  // Waits for the threadpool to terminate
  void wait_stopped();

  // ---------------------------------------------------------------------------
  // Funtions to get/set data in the cache.
  // Each one must provide a success and failure callback.
  // The request is run on the threadpool and the appropriate callback called.
  // The result of a get request is provided as the argument to the success
  // callback.
  // If the request fails, the Store::Status code is provided as an argument to
  // the failure callback.
  // ---------------------------------------------------------------------------

  // Get the IRS for a given impu
  void get_implicit_registration_set_for_impu(irs_success_callback success_cb,
                                              failure_callback failure_cb,
                                              std::string impu);

  // Get the list of IRSs for the given list of impus
  // Used for RTR when we have a list of impus
  void get_implicit_registration_sets_for_impis(irs_vector_success_callback success_cb,
                                                failure_callback failure_cb,
                                                std::vector<std::string>* impis);

  // Get the list of IRSs for the given list of imps
  // Used for RTR when we have a list of impis
  void get_implicit_registration_sets_for_impus(irs_vector_success_callback success_cb,
                                                failure_callback failure_cb,
                                                std::vector<std::string>* impus);

  // Save the IRS in the cache
  // Must include updating the impi mapping table if impis have been added
  void put_implicit_registration_set(void_success_cb success_cb,
                                     failure_callback failure_cb,
                                     ImplicitRegistrationSet* irs);

  // Used for de-registration
  void delete_implicit_registration_set(void_success_cb success_cb,
                                        failure_callback failure_cb,
                                        ImplicitRegistrationSet* irs);

  // Deletes several registration sets
  // Used for an RTR when we have several registration sets to delete
  void delete_implicit_registration_sets(void_success_cb success_cb,
                                         failure_callback failure_cb,
                                         std::vector<ImplicitRegistrationSet*>* irss);

  // Gets the whole IMS subscription for this impi
  // This is used when we get a PPR, and we have to update charging functions
  // as we'll need to updated every IRS that we've stored
  void get_ims_subscription(ims_sub_success_cb success_cb,
                            failure_callback failure_cb,
                            std::string impi);

  // This is used to save the state that we changed in the PPR
  void put_ims_subscription(void_success_cb success_cb,
                            failure_callback failure_cb,
                            ImsSubscription* subscription);

  // Lists impus, starting at starting_from, limited to count
  void list_impus(impi_vector_success_callback success_cb,
                  failure_callback failure_cb,
                  int count,
                  std::string last_impu);
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
