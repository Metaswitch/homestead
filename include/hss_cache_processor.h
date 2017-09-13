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
#include "sas.h"

typedef std::function<void(Store::Status)> failure_callback;
typedef std::function<void(ImplicitRegistrationSet*)> irs_success_callback;
typedef std::function<void(std::vector<ImplicitRegistrationSet*>)> irs_vector_success_callback;
typedef std::function<void()> void_success_cb;
typedef std::function<void()> progress_callback;
typedef std::function<void(ImsSubscription*)> ims_sub_success_cb;

class HssCacheProcessor
{
public:
  virtual ~HssCacheProcessor() {};

  // Creates the HssCacheProcessor, but not the thread pool.
  // start_threads() must be called to create and start the thread pool.
  HssCacheProcessor(HssCache* cache);

  // Starts the threadpool with the required number of threads
  bool start_threads(int num_threads,
                     ExceptionHandler* exception_handler,
                     unsigned int max_queue);

  // Stops the threadpool
  void stop();

  // Waits for the threadpool to terminate
  // It is illegal to call anything that adds work to the threadpool after
  // wait_stopped()
  void wait_stopped();

  // Factory method for creating implicit registration sets.
  // Note, this doesn't follow the async API that the rest of the
  // HSS cache processor does.
  virtual ImplicitRegistrationSet* create_implicit_registration_set();

  // ---------------------------------------------------------------------------
  // Funtions to get/set data in the cache.
  // Each one must provide a success and failure callback.
  // The request is run on the threadpool and the appropriate callback called.
  //
  // The result of a get request is provided as the argument to the success
  // callback. Ownership of pointer results is passed to the calling function.
  //
  // All put/delete operations take a progress callback as well as the success/
  // failure callbacks. The progress callback is called once the cache has
  // made enough progress that it can be called a success. However, it may still
  // have work to do after this point, and so the progress callback must not
  // delete the pointer that was put/deleted.
  // Once the progress callback returns, the cache continues doing whatever work
  // it deems necessary. Once it has finished, it returns and the success
  // callback is called. This is the point at which the caller can delete the
  // put/deleted object.
  //
  // If the request fails, the Store::Status code is provided as an argument to
  // the failure callback. The progress callback will not be called in a failure
  // scenario, so the failure callback must delete any put/deleted data.
  // ---------------------------------------------------------------------------

  // Get the IRS for a given impu
  virtual void get_implicit_registration_set_for_impu(irs_success_callback success_cb,
                                                      failure_callback failure_cb,
                                                      std::string impu,
                                                      SAS::TrailId trail);

  // Get the list of IRSs for the given list of impus
  // Used for RTR when we have a list of impus
  virtual void get_implicit_registration_sets_for_impis(irs_vector_success_callback success_cb,
                                                        failure_callback failure_cb,
                                                        std::vector<std::string> impis,
                                                        SAS::TrailId trail);

  // Get the list of IRSs for the given list of imps
  // Used for RTR when we have a list of impis
  virtual void get_implicit_registration_sets_for_impus(irs_vector_success_callback success_cb,
                                                        failure_callback failure_cb,
                                                        std::vector<std::string> impus,
                                                        SAS::TrailId trail);

  // Save the IRS in the cache
  // Must include updating the impi mapping table if impis have been added
  virtual void put_implicit_registration_set(void_success_cb success_cb,
                                             progress_callback progress_cb,
                                             failure_callback failure_cb,
                                             ImplicitRegistrationSet* irs,
                                             SAS::TrailId trail);

  // Used for de-registration
  virtual void delete_implicit_registration_set(void_success_cb success_cb,
                                                progress_callback progress_cb,
                                                failure_callback failure_cb,
                                                ImplicitRegistrationSet* irs,
                                                SAS::TrailId trail);

  // Deletes several registration sets
  // Used for an RTR when we have several registration sets to delete
  virtual void delete_implicit_registration_sets(void_success_cb success_cb,
                                                 progress_callback progress_cb,
                                                 failure_callback failure_cb,
                                                 std::vector<ImplicitRegistrationSet*> irss,
                                                 SAS::TrailId trail);

  // Gets the whole IMS subscription for this impi
  // This is used when we get a PPR, and we have to update charging functions
  // as we'll need to updated every IRS that we've stored
  virtual void get_ims_subscription(ims_sub_success_cb success_cb,
                                    failure_callback failure_cb,
                                    std::string impi,
                                    SAS::TrailId trail);

  // This is used to save the state that we changed in the PPR
  virtual void put_ims_subscription(void_success_cb success_cb,
                                    progress_callback progress_cb,
                                    failure_callback failure_cb,
                                    ImsSubscription* subscription,
                                    SAS::TrailId trail);

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
