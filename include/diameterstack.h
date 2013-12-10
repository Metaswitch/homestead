/**
 * @file diameterstack.h class definition wrapping a Diameter stack
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

#include <string>

#include <freeDiameter/freeDiameter-host.h>
#include <freeDiameter/libfdcore.h>

namespace Diameter
{
class Stack;
class Transaction;
class AVP;
class Message;

class Stack
{
public:
  class Exception
  {
  public:
    inline Exception(const char* func, int rc) : _func(func), _rc(rc) {};
    const char* _func;
    const int _rc;
  };

  static inline Stack* get_instance() {return INSTANCE;};
  virtual void initialize();
  virtual void configure(std::string filename);
  virtual void start();
  virtual void stop();
  virtual void wait_stopped();

private:
  static Stack* INSTANCE;
  static Stack DEFAULT_INSTANCE;

  Stack();
  virtual ~Stack();

  // Don't implement the following, to avoid copies of this instance.
  Stack(Stack const&);
  void operator=(Stack const&);

  bool _initialized;
};

class Dictionary
{
public:
  class Object
  {
  public:
    inline Object(struct dict_object* dict) : _dict(dict) {};
    inline struct dict_object* dict() const {return _dict;}

  private:
    struct dict_object *_dict;
  };

  class Vendor : public Object
  {
  public:
    inline Vendor(const std::string vendor) : Object(find(vendor)) {};
    static struct dict_object* find(const std::string vendor);
  };

  class Application : public Object
  {
  public:
    inline Application(const std::string application) : Object(find(application)) {};
    static struct dict_object* find(const std::string application);
  };

  class Message : public Object
  {
  public:
    inline Message(const std::string message) : Object(find(message)) {};
    static struct dict_object* find(const std::string message);
  };

  class AVP : public Object
  {
  public:
    inline AVP(const std::string avp) : Object(find(avp)) {};
    inline AVP(const std::string vendor, const std::string avp) : Object(find(vendor, avp)) {};
    static struct dict_object* find(const std::string avp);
    static struct dict_object* find(const std::string vendor, const std::string avp);
  };

  Dictionary();
  const AVP SESSION_ID;
  const AVP AUTH_SESSION_STATE;
  const AVP ORIGIN_REALM;
  const AVP ORIGIN_HOST;
  const AVP DESTINATION_REALM;
  const AVP DESTINATION_HOST;
  const AVP USER_NAME;
  const AVP RESULT_CODE;
};

class Transaction
{
public:
  Transaction(Dictionary* dict);
  virtual ~Transaction();

  virtual void on_response(Message& rsp) = 0;
  virtual void on_timeout() = 0;

  static void on_response(void* data, struct msg** rsp);
  static void on_timeout(void* data, DiamId_t to, size_t to_len, struct msg** req);

private:
  Dictionary* _dict;
};

class AVP
{
public:
  inline AVP(const Dictionary::AVP& type)
  {
    fd_msg_avp_new(type.dict(), 0, &_avp);
  }
  inline AVP(struct avp* avp) : _avp(avp) {};
  inline struct avp* avp() const {return _avp;}
  inline AVP& val_os(std::string str)
  {
    return val_os((uint8_t*)str.c_str(), str.length());
  }
  inline AVP& val_os(uint8_t* data, size_t len)
  {
    union avp_value val;
    val.os.data = data;
    val.os.len = len;
    fd_msg_avp_setvalue(_avp, &val);
    return *this;
  }
  inline AVP& val_i32(int32_t i32)
  {
    union avp_value val;
    val.i32 = i32;
    fd_msg_avp_setvalue(_avp, &val);
    return *this;
  }
  inline AVP& val_i64(int64_t i64)
  {
    union avp_value val;
    val.i64 = i64;
    fd_msg_avp_setvalue(_avp, &val);
    return *this;
  }
  inline AVP& val_u32(uint32_t u32)
  {
    union avp_value val;
    val.u32 = u32;
    fd_msg_avp_setvalue(_avp, &val);
    return *this;
  }
  inline AVP& val_u64(uint64_t u64)
  {
    union avp_value val;
    val.u64 = u64;
    fd_msg_avp_setvalue(_avp, &val);
    return *this;
  }
  inline AVP& add(AVP& avp)
  {
    fd_msg_avp_add(_avp, MSG_BRW_LAST_CHILD, avp.avp());
    return *this;
  }

private:
  struct avp* _avp;
};

class Message
{
public:
  inline Message(const Dictionary* dict, const Dictionary::Message& type) : _dict(dict)
  {
    fd_msg_new(type.dict(), MSGFL_ALLOC_ETEID, &_msg);
  }
  inline Message(Dictionary* dict, struct msg* msg) : _dict(dict), _msg(msg) {};
  virtual ~Message();
  inline Message& add(AVP& avp)
  {
    fd_msg_avp_add(_msg, MSG_BRW_LAST_CHILD, avp.avp());
    return *this;
  }
  virtual void send();
  virtual void send(Transaction* tsx);
  virtual void send(Transaction* tsx, unsigned int timeout_ms);

private:
  const Dictionary* _dict;
  struct msg* _msg;
};
};
