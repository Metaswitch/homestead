/**
 * @file httpstack_utils_test.cpp UT for HttpStack utilities.
 *
 * Project Clearwater - IMS in the cloud.
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#define GTEST_HAS_POSIX_RE 0
#include <time.h>
#include <semaphore.h>

#include "test_utils.hpp"
#include "test_interposer.hpp"

#include "httpstack_utils.h"

#include "mockhttpstack.hpp"
#include "mock_sas.h"

SAS::TrailId FAKE_TRAIL_ID = 0x1234567890abcdef;

class ControllerUtilsTest : public testing::Test
{
  ControllerUtilsTest()
  {
    _httpstack = new MockHttpStack();
  }

  ~ControllerUtilsTest()
  {
    delete _httpstack; _httpstack = NULL;
  }

  MockHttpStack* _httpstack;
};

/// An implementation of the barrier synchronozation pattern. The barrier has a
/// capacity of N and threads that reach the barrier block until the Nth thread
/// arrives, at which point they all unblock.
///
/// This implemenetion allows the barrier to be reused, so that threads N+1 to
/// 2n-1 (inclusive) block until thread 2N arrives.
class Barrier
{
public:
  Barrier(unsigned int capacity) :
    _capacity(capacity), _waiters(0), _trigger_count(0)
  {
    pthread_mutex_init(&_mutex, NULL);
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
  }

  ~Barrier()
  {
    pthread_cond_destroy(&_cond);
    pthread_mutex_destroy(&_mutex);
  }

  /// Called when a thread arrives at the barrier.
  ///
  /// @param timeout_us the maximum time (in microseconds) to wait for the
  ///   barrier to trigger.
  /// @return true if the barrier triggered successfully, false if the arrive
  ///   call timed out.
  bool arrive(uint64_t timeout_us = 0)
  {
    bool success = true;
    timespec timeout_ts;

    if (timeout_us != 0)
    {
      // Calculate the time at which the arrive call should timeout.
      clock_gettime(CLOCK_MONOTONIC, &timeout_ts);
      timeout_ts.tv_nsec += (timeout_us % 1000000) * 1000;
      timeout_ts.tv_sec += (timeout_us / 1000000);

      if (timeout_ts.tv_nsec >= 1000000000)
      {
        timeout_ts.tv_nsec -= 1000000000;
        timeout_ts.tv_sec += 1;
      }
    }

    pthread_mutex_lock(&_mutex);
    _waiters++;

    // Keep track of the current trigger count in a local variable. This is
    // used to guard against spurious wakeups when waiting for the barrier to
    // trigger.
    unsigned int local_trigger_count = _trigger_count;

    if (_waiters >= _capacity)
    {
      // We have reached the required number of waiters so wake up the other
      // threads.
      _trigger_count++;
      _waiters = 0;
      pthread_cond_broadcast(&_cond);
    }

    while (local_trigger_count == _trigger_count)
    {
      // Barrier hasn't been triggered since we first arrived. Wait for it to
      // be triggered.
      if (timeout_us != 0)
      {
        int wait_rc = pthread_cond_timedwait(&_cond, &_mutex, &timeout_ts);

        if (wait_rc == ETIMEDOUT)
        {
          // Timed out. Give up on waiting.
          success = false;
          break;
        }
      }
      else
      {
        pthread_cond_wait(&_cond, &_mutex);
      }
    }

    pthread_mutex_unlock(&_mutex);
    return success;
  }

private:
  // The number of threads that must have arrived before the barrier triggers
  // and they all unblock.
  unsigned int _capacity;

  // The number of threads currently waiting for thebarrier to trigger.
  unsigned int _waiters;

  // The number of times the barrier has been triggered.
  unsigned int _trigger_count;

  pthread_cond_t _cond;
  pthread_mutex_t _mutex;
};

//
// Some test handlers.
//

// Handler that hits a barrier when it is invoked.
class TestBarrierHandler : public HttpStackUtils::Handler
{
public:
  struct Config
  {
    Barrier* barrier;
  };

  TestBarrierHandler(HttpStack::Request& req,
                     const Config* cfg,
                     SAS::TrailId trail) :
    HttpStackUtils::Handler(req, trail), _cfg(cfg)
  {}

  void run()
  {
    bool ok = _cfg->barrier->arrive(60 * 1000000); // 60s timeout
    ASSERT_TRUE(ok);
    delete this; return;
  }

private:
  const Config* _cfg;
};

// Controller that posts to a semaphore when it is invoked.  This also provides
// a method to wait on the semaphore (with a timeout).
class TestSemaphoreHandler : public HttpStackUtils::Handler
{
public:
  struct Config
  {
    sem_t* sema;
  };

  TestSemaphoreHandler(HttpStack::Request& req,
                       const Config* cfg,
                       SAS::TrailId trail) :
    HttpStackUtils::Handler(req, trail), _cfg(cfg)
  {}

  void run()
  {
    sem_post(_cfg->sema);
    delete this; return;
  }

  static bool wait_for_request(sem_t* sema, uint64_t timeout_us = 0)
  {
    bool success = true;

    if (timeout_us == 0)
    {
      sem_wait(sema);
    }
    else
    {
      // Calculate the time at which the arrive call should timeout.
      timespec timeout_ts;
      clock_gettime(CLOCK_REALTIME, &timeout_ts);
      timeout_ts.tv_nsec += (timeout_us % 1000000) * 1000;
      timeout_ts.tv_sec += (timeout_us / 1000000);

      if (timeout_ts.tv_nsec >= 1000000000)
      {
        timeout_ts.tv_nsec -= 1000000000;
        timeout_ts.tv_sec += 1;
      }

      success = (sem_timedwait(sema, &timeout_ts) == 0);
    }

    return success;
  }

private:
  const Config* _cfg;
};


// Test handler that counts the number of times it has been instantiated and
// run.
class TestCountingHandler : public HttpStackUtils::Handler
{
public:
  class Config {};

  TestCountingHandler(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HttpStackUtils::Handler(req, trail)
  {
    construction_count++;
  }

  void run()
  {
    run_count++;
    delete this; return;
  }

  static void reset_counts()
  {
    construction_count = 0;
    run_count = 0;
  }

  static int construction_count;
  static int run_count;
};

int TestCountingHandler::construction_count;
int TestCountingHandler::run_count;


class TestChronosController : public HttpStack::ControllerInterface
{
public:
  void process_request(HttpStack::Request& req, SAS::TrailId trail)
  {
  }

  HttpStack::SasLogger* sas_logger(HttpStack::Request& req)
  {
    return &HttpStackUtils::CHRONOS_SAS_LOGGER;
  }
};

//
// Testcases.
//

TEST_F(ControllerUtilsTest, SingleThread)
{
  // Check that the thread pool actually transfers control to a worker thread.
  //
  // Test this by using a barrier with a capcity of 2. This will only be
  // triggered when 2 threads arrive at it (the testbed main thread, and the
  // worker thread).
  bool ok;
  Barrier barrier(2);

  HttpStackUtils::ControllerThreadPool pool(1);
  pool.start();

  TestBarrierHandler::Config cfg;
  cfg.barrier = &barrier;

  HttpStackUtils::SpawningController
    <TestBarrierHandler, TestBarrierHandler::Config>
      controller(&cfg, &pool);

  MockHttpStack::Request req(_httpstack, "/", "kermit");
  controller.process_request(req, FAKE_TRAIL_ID);
  ok = barrier.arrive(10 * 1000000); // 10s timeout
  EXPECT_TRUE(ok);

  pool.stop();
  pool.join();
}


TEST_F(ControllerUtilsTest, MultipleThreads)
{
  // Check that the thread pool processes requests in parallel.
  //
  // Test this using a barrier with a capacity of 5, and 4 requests. This will
  // only be triggered when 5 threads arrive at it (the tesbed thread, and the
  // threads handling the requests).
  bool ok;
  Barrier barrier(5);

  HttpStackUtils::ControllerThreadPool pool(10);
  pool.start();

  TestBarrierHandler::Config cfg;
  cfg.barrier = &barrier;

  HttpStackUtils::SpawningController
    <TestBarrierHandler, TestBarrierHandler::Config>
      controller(&cfg, &pool);

  for (int i = 0; i < 4; ++i)
  {
    MockHttpStack::Request req(_httpstack, "/", "kermit");
    controller.process_request(req, FAKE_TRAIL_ID);
  }

  ok = barrier.arrive(10 * 1000000); // 10s timeout
  EXPECT_TRUE(ok);

  pool.stop();
  pool.join();
}


TEST_F(ControllerUtilsTest, SingleThreadReuse)
{
  // Check that each worker thread can handle multiple requests.
  //
  // Test this by posting to a semaphore on every request and then waiting on
  // this semaphore once for each request.
  sem_t sema;
  sem_init(&sema, 0, 0);

  TestSemaphoreHandler::Config cfg;
  cfg.sema = &sema;

  HttpStackUtils::ControllerThreadPool pool(1);
  pool.start();

  HttpStackUtils::SpawningController
    <TestSemaphoreHandler, TestSemaphoreHandler::Config>
      controller(&cfg, &pool);

  const int NUM_REQUESTS = 5;

  for (int i = 0; i < NUM_REQUESTS; ++i)
  {
    MockHttpStack::Request req(_httpstack, "/", "kermit");
    controller.process_request(req, FAKE_TRAIL_ID);
  }

  for (int i = 0; i < NUM_REQUESTS; ++i)
  {
    bool ok = TestSemaphoreHandler::wait_for_request(&sema, 10 * 1000000); // 10s timeout.
    EXPECT_TRUE(ok);
  }

  sem_destroy(&sema);

  pool.stop();
  pool.join();
}


TEST_F(ControllerUtilsTest, SpawningController)
{
  // Check that the spawning controller actually constructs and runs a handler
  // for each request.
  TestCountingHandler::Config cfg;
  HttpStackUtils::SpawningController
    <TestCountingHandler, TestCountingHandler::Config> controller(&cfg);

  TestCountingHandler::reset_counts();

  const int NUM_REQUESTS = 5;

  for (int i = 0; i < NUM_REQUESTS; ++i)
  {
    MockHttpStack::Request req(_httpstack, "/", "kermit");
    controller.process_request(req, FAKE_TRAIL_ID);
  }

  EXPECT_EQ(TestCountingHandler::construction_count, NUM_REQUESTS);
  EXPECT_EQ(TestCountingHandler::run_count, NUM_REQUESTS);
}


TEST_F(ControllerUtilsTest, ChronosLogging)
{
  // Check that the chronos SAS logger logs events with the correct event ID.
  mock_sas_collect_messages(true);
  MockSASMessage* event;

  TestChronosController chronos_controller;

  MockHttpStack::Request req(_httpstack, "/", "kermit");
  req.set_sas_logger(chronos_controller.sas_logger(req));

  req.sas_log_rx_http_req(FAKE_TRAIL_ID, 0);
  event = mock_sas_find_event(SASEvent::RX_HTTP_REQ_DETAIL);
  EXPECT_TRUE(event != NULL);

  req.sas_log_tx_http_rsp(FAKE_TRAIL_ID, 200, 0);
  event = mock_sas_find_event(SASEvent::TX_HTTP_RSP_DETAIL);
  EXPECT_TRUE(event != NULL);

  req.sas_log_overload(FAKE_TRAIL_ID, 503, 0);
  event = mock_sas_find_event(SASEvent::HTTP_REJECTED_OVERLOAD_DETAIL);
  EXPECT_TRUE(event != NULL);

  mock_sas_collect_messages(false);
}
