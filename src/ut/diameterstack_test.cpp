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

#include "diameterstack.h"
#include "cx.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::DoAll;

class DiameterTestTransaction : public Diameter::Transaction
{
public:
  DiameterTestTransaction(Diameter::Dictionary* dict) :
    Diameter::Transaction(dict)
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
    _stack->configure(UT_DIR + "/diameterstack.conf");
    _stack->start();

    _dict = new Cx::Dictionary();

    cwtest_completely_control_time();

    // Mock out free diameter.  By default swallow all attempts to create new
    // messages or read data out of them.
    mock_free_diameter(&_mock_fd);

    EXPECT_CALL(_mock_fd, fd_msg_new(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>((struct msg*)NULL), Return(0)));

    _mock_fd.hdr.msg_code = 123;
    EXPECT_CALL(_mock_fd, fd_msg_hdr(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(&_mock_fd.hdr), Return(0)));
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

private:
  Diameter::Stack* _stack;
  Cx::Dictionary* _dict;
  MockFreeDiameter _mock_fd;
};


TEST(DiameterStackTest, SimpleMainline)
{
  Diameter::Stack* stack = Diameter::Stack::get_instance();
  stack->initialize();
  stack->configure(UT_DIR + "/diameterstack.conf");
  stack->start();
  stack->stop();
  stack->wait_stopped();
}

TEST(DiameterStackTest, AdvertizeApplication)
{
  Diameter::Stack* stack = Diameter::Stack::get_instance();
  stack->initialize();
  stack->configure(UT_DIR + "/diameterstack.conf");
  Diameter::Dictionary::Application app("Cx");
  stack->advertize_application(app);
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
