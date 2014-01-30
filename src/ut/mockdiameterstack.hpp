/**
 * @file mockdiameterstack.h Mock HTTP stack.
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

#ifndef MOCKDIAMETERSTACK_H__
#define MOCKDIAMETERSTACK_H__

#include "gmock/gmock.h"
#include "diameterstack.h"

class MockDictionary : public Diameter::Dictionary
{
  MockDictionary() : Diameter::Dictionary() {}
  virtual ~MockDictionary() {}

  class MockMessage : public Diameter::Dictionary::Message
  {
    MockMessage(const std::string message) : Diameter::Dictionary::Message(message) {}
    virtual ~MockMessage() {}
  };
};

class MockDiameterTransaction : public Diameter::Transaction
{
  MockDiameterTransaction(MockDictionary* dict) : Diameter::Transaction(dict) {}
  virtual ~MockDiameterTransaction() {}

  MOCK_METHOD1(on_response, void(Diameter::Message&));
  MOCK_METHOD0(on_timeout, void());
};

class MockAVP : public Diameter::AVP
{
};

class MockDiameterMessage : public Diameter::Message
{
  MockDiameterMessage(Diameter::Dictionary* dict) : Diameter::Message(dict, NULL) {}
  virtual ~MockDiameterMessage() {}

  MOCK_CONST_METHOD0(dict, Diameter::Dictionary*());
  MOCK_METHOD0(fd_msg, struct msg*());
  MOCK_METHOD0(build_response, void());
  MOCK_METHOD0(add_new_session_id, Message&());
  MOCK_METHOD0(add_vendor_spec_app_id, Message&());
  MOCK_METHOD0(add_origin, Message&());
  MOCK_METHOD1(set_result_code, Message&(char*));
  MOCK_METHOD1(add, Message&(Diameter::AVP&));
  MOCK_CONST_METHOD2(get_str_from_avp, bool(const Diameter::Dictionary::AVP&, std::string*));
  MOCK_CONST_METHOD2(get_i32_from_avp, bool(const Diameter::Dictionary::AVP&, int*));
  MOCK_CONST_METHOD0(experimental_result_code, int());
  MOCK_CONST_METHOD0(vendor_id, int());
  MOCK_CONST_METHOD1(impi, bool(std::string*));
  MOCK_CONST_METHOD1(auth_session_state, bool(int));
  MOCK_CONST_METHOD0(begin, Diameter::AVP::iterator());
  MOCK_CONST_METHOD1(begin, Diameter::AVP::iterator(Diameter::Dictionary::AVP&));
  MOCK_CONST_METHOD0(end, Diameter::AVP::iterator());
  MOCK_METHOD0(send, void());
  MOCK_METHOD1(send, void(MockDiameterTransaction*));
  MOCK_METHOD2(send, void(Diameter::Transaction*, unsigned int));
};

class MockDiameterStack : public Diameter::Stack
{
public:
  MOCK_METHOD0(initialize, void());
  MOCK_METHOD1(configure, void(const std::string&));
  MOCK_METHOD1(advertize_application, void(const Diameter::Dictionary::Application&));
  MOCK_METHOD3(register_handler, void(const Diameter::Dictionary::Application&, const Diameter::Dictionary::Message&, BaseHandlerFactory*));
  MOCK_METHOD1(register_fallback_handler, void(const Diameter::Dictionary::Application&));
  MOCK_METHOD0(start, void());
  MOCK_METHOD0(stop, void());
  MOCK_METHOD0(wait_stopped, void());
  MOCK_METHOD1(send, void(struct msg*));
  MOCK_METHOD2(send, void(struct msg*, Diameter::Transaction*));
  MOCK_METHOD3(send, void(struct msg*, Diameter::Transaction*, unsigned int timeout_ms));
};

#endif
