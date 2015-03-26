/**
 * @file diameterstack_test.cpp UT for Homestead diameterstack module.
 *
 * Project Clearwater - IMS in the Cloud
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

#include <time.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "test_utils.hpp"
#include "test_interposer.hpp"
#include "mockfreediameter.hpp"
#include "mockcommunicationmonitor.h"
#include "barrier.h"

#include "diameterstack.h"
#include "cx.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::InvokeWithoutArgs;
using ::testing::IgnoreResult;

class DiameterTestTransaction : public Diameter::Transaction
{
public:
  DiameterTestTransaction(Diameter::Dictionary* dict) :
    Diameter::Transaction(dict, 0)
  {}

  virtual ~DiameterTestTransaction() {}

  void check_latency(unsigned long expected_latency_us)
  {
    unsigned long actual_latency_us;
    bool rc;

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);

    cwtest_advance_time_ms(1);

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);
  }

  MOCK_METHOD1(on_response, void(Diameter::Message& rsp));
  MOCK_METHOD0(on_timeout, void());
};

class DiameterRequestTest : public ::testing::Test
{
public:
  DiameterRequestTest()
  {
    _stack = Diameter::Stack::get_instance();
    _stack->initialize();
    _stack->configure(UT_DIR + "/diameterstack.conf", NULL);
    _stack->start();

    _dict = new Cx::Dictionary();

    cwtest_completely_control_time();

    // Mock out free diameter.  By default mock out all attempts to create new
    // messages, read data out of them, or bufferize them.
    mock_free_diameter(&_mock_fd);

    EXPECT_CALL(_mock_fd, fd_msg_new(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>((struct msg*)NULL), Return(0)));

    _mock_fd.hdr.msg_code = 123;
    EXPECT_CALL(_mock_fd, fd_msg_hdr(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(&_mock_fd.hdr), Return(0)));

    EXPECT_CALL(_mock_fd, fd_msg_bufferize(_, _, _))
      .WillRepeatedly(DoAll(WithArgs<1, 2>(Invoke(create_dummy_diameter_buffer)),
                            Return(0)));

    EXPECT_CALL(_mock_fd, fd_hook_get_pmd(_, _))
      .WillRepeatedly(Return(&_mock_per_msg_data));
  }

  virtual ~DiameterRequestTest()
  {
    unmock_free_diameter();
    cwtest_reset_time();

    delete _dict; _dict = NULL;

    _stack->stop();
    _stack->wait_stopped();
  }

  DiameterTestTransaction* make_trx()
  {
    return new DiameterTestTransaction(_dict);
  }

  static void create_dummy_diameter_buffer(uint8_t **buffer, size_t *len)
  {
    char* str = strdup("A fake diameter message");
    *len = strlen(str);
    *buffer = (uint8_t*)str;
  }

private:
  Diameter::Stack* _stack;
  Cx::Dictionary* _dict;
  MockFreeDiameter _mock_fd;
  fd_hook_permsgdata _mock_per_msg_data;
};


class DiameterRequestCommMonMockTest : public ::testing::Test
{
public:
  DiameterRequestCommMonMockTest()
  {
    _stack = Diameter::Stack::get_instance();
    _stack->initialize();
    _stack->configure(UT_DIR + "/diameterstack.conf", NULL, &_cm);
    _stack->start();

    _dict = new Cx::Dictionary();
  }

  virtual ~DiameterRequestCommMonMockTest()
  {
    delete _dict; _dict = NULL;

    _stack->stop();
    _stack->wait_stopped();
  }

  DiameterTestTransaction* make_trx()
  {
    return new DiameterTestTransaction(_dict);
  }

private:
  Diameter::Stack* _stack;
  Cx::Dictionary* _dict;
  MockCommunicationMonitor _cm;
};


TEST(DiameterStackTest, SimpleMainline)
{
  Diameter::Stack* stack = Diameter::Stack::get_instance();
  stack->initialize();
  stack->configure(UT_DIR + "/diameterstack.conf", NULL);
  stack->start();
  stack->stop();
  stack->wait_stopped();
}

TEST(DiameterStackTest, AdvertizeApplication)
{
  Diameter::Stack* stack = Diameter::Stack::get_instance();
  stack->initialize();
  stack->configure(UT_DIR + "/diameterstack.conf", NULL);
  Diameter::Dictionary::Application app("Cx");
  stack->advertize_application(Diameter::Dictionary::Application::AUTH, app);
  stack->start();
  stack->stop();
  stack->wait_stopped();
}

ACTION_P(AdvanceTimeMs, ms) { cwtest_advance_time_ms(ms); }
ACTION_P2(CheckLatency, trx, ms) { trx->check_latency(ms * 1000); }

TEST_F(DiameterRequestTest, NormalRequestTimesLatency)
{
  Diameter::Message req(_dict, _dict->MULTIMEDIA_AUTH_REQUEST, _stack);
  struct msg *fd_rsp = NULL;
  DiameterTestTransaction *trx = make_trx();

  EXPECT_CALL(_mock_fd, fd_msg_send(_, _, _)).WillOnce(Return(0));
  req.send(trx);

  cwtest_advance_time_ms(12);

  EXPECT_CALL(*trx, on_response(_)).WillOnce(CheckLatency(trx, 12));
  Diameter::Transaction::on_response(trx, &fd_rsp); trx = NULL;
}


TEST_F(DiameterRequestTest, TimedoutRequestTimesLatency)
{
  Diameter::Message req(_dict, _dict->MULTIMEDIA_AUTH_REQUEST, _stack);
  struct msg *fd_rsp = NULL;
  DiameterTestTransaction *trx = make_trx();

  EXPECT_CALL(_mock_fd, fd_msg_send_timeout(_, _, _, _, _)).WillOnce(Return(0));
  req.send(trx, 1000);

  cwtest_advance_time_ms(15);

  EXPECT_CALL(*trx, on_timeout()).WillOnce(CheckLatency(trx, 15));
  Diameter::Transaction::on_timeout(trx,
                                    (DiamId_t)"DiameterIdentity",
                                    0,
                                    &fd_rsp);
  trx = NULL;
}


TEST_F(DiameterRequestCommMonMockTest, ResponseOk)
{
  DiameterTestTransaction *trx = make_trx();

  Diameter::Message rsp(_dict, _dict->MULTIMEDIA_AUTH_ANSWER, _stack);
  rsp.revoke_ownership();
  rsp.set_result_code("DIAMETER_SUCCESS");

  struct msg* fd_rsp = rsp.fd_msg();

  EXPECT_CALL(*trx, on_response(_));
  EXPECT_CALL(_cm, inform_success(_));
  Diameter::Transaction::on_response(trx, &fd_rsp); trx = NULL;
}


TEST_F(DiameterRequestCommMonMockTest, ResponseError)
{
  DiameterTestTransaction *trx = make_trx();

  Diameter::Message rsp(_dict, _dict->MULTIMEDIA_AUTH_ANSWER, _stack);
  rsp.revoke_ownership();
  rsp.set_result_code("DIAMETER_UNABLE_TO_DELIVER");

  struct msg* fd_rsp = rsp.fd_msg();

  EXPECT_CALL(*trx, on_response(_));
  EXPECT_CALL(_cm, inform_failure(_));
  Diameter::Transaction::on_response(trx, &fd_rsp); trx = NULL;
}


class MockHandler : public Diameter::Stack::HandlerInterface
{
public:
  MOCK_METHOD2(process_request, void(struct msg**, SAS::TrailId));
};


class HandlerThreadPoolTest : public ::testing::Test
{
public:
  MockHandler _handler;
  struct msg* _fd_msg_ptr;
  SAS::TrailId _trail;

  void SetUp()
  {
    // Create a dummy message and trail.
    _fd_msg_ptr = (struct msg*)1234;
    _trail = 5678;
  }
};


TEST_F(HandlerThreadPoolTest, SingleThread)
{
  Barrier barrier(2);

  // Create a pool with one thread.
  Diameter::HandlerThreadPool pool(1, NULL);
  Diameter::Stack::HandlerInterface* wrapped = pool.wrap(&_handler);

  // Check the pool correctly passes through the message and trail ID.
  EXPECT_CALL(_handler, process_request(&_fd_msg_ptr, _trail))
    .WillOnce(
       IgnoreResult(
         InvokeWithoutArgs(std::bind(&Barrier::arrive, &barrier, 0))));

  // Call `process_request`, then arrive at the barrier. The threads only unblock
  // when both threads have reached it.
  wrapped->process_request(&_fd_msg_ptr, _trail);
  bool ok = barrier.arrive(10 * 1000000); // 10s timeout
  // We didn't time out waiting on the barrier.
  EXPECT_TRUE(ok);
}


TEST_F(HandlerThreadPoolTest, MultipleThreads)
{
  Barrier barrier(3);

  // Create a pool with two threads.
  Diameter::HandlerThreadPool pool(2, NULL);
  Diameter::Stack::HandlerInterface* wrapped = pool.wrap(&_handler);

  EXPECT_CALL(_handler, process_request(&_fd_msg_ptr, _trail))
    .Times(2)
    .WillRepeatedly(
       IgnoreResult(
         InvokeWithoutArgs(std::bind(&Barrier::arrive, &barrier, 0))));

  // Each call to process request returns immediately.
  wrapped->process_request(&_fd_msg_ptr, _trail);
  wrapped->process_request(&_fd_msg_ptr, _trail);

  // Wait at the barrier - check we did not time out waiting for it.
  bool ok = barrier.arrive(10 * 1000000); // 10s timeout
  EXPECT_TRUE(ok);
}


TEST_F(HandlerThreadPoolTest, ThreadReuse)
{
  bool ok;
  Barrier barrier(2);

  // Create a pool with one thread.
  Diameter::HandlerThreadPool pool(1, NULL);
  Diameter::Stack::HandlerInterface* wrapped = pool.wrap(&_handler);

  EXPECT_CALL(_handler, process_request(&_fd_msg_ptr, _trail))
    .Times(2)
    .WillRepeatedly(
       IgnoreResult(
         InvokeWithoutArgs(std::bind(&Barrier::arrive, &barrier, 0))));

  // Each call to process_request returns immediately, arriving at the barrier
  // each time unblocks the pool thread.
  wrapped->process_request(&_fd_msg_ptr, _trail);
  ok = barrier.arrive(10 * 1000000); // 10s timeout
  EXPECT_TRUE(ok);

  wrapped->process_request(&_fd_msg_ptr, _trail);
  ok = barrier.arrive(10 * 1000000); // 10s timeout
  EXPECT_TRUE(ok);
}
