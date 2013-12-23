/**
 * @file handlers.cpp handlers for homestead
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

#ifndef HANDLERS_H__
#define HANDLERS_H__

#include <boost/bind.hpp>

#include "cx.h"
#include "diameterstack.h"
#include "httpstack.h"

class PingHandler : public HttpStack::Handler
{
public:
  PingHandler(HttpStack::Request& req) : HttpStack::Handler(req) {};
  void run();
};

class HssCacheHandler : public HttpStack::Handler
{
public:
  HssCacheHandler(HttpStack::Request& req) : HttpStack::Handler(req) {};

  static void configure_diameter(Diameter::Stack* diameter_stack,
                                 const std::string& dest_realm,
                                 const std::string& dest_host,
                                 const std::string& server_name);

  void on_diameter_timeout();

  template <class H>
  class DiameterTransaction : public Diameter::Transaction
  {
  public:
    DiameterTransaction(Cx::Dictionary* dict, H* handler) :
      Diameter::Transaction(dict),
      _handler(handler),
      _timeout_clbk(&HssCacheHandler::on_diameter_timeout),
      _response_clbk(NULL)
    {};

    typedef void(H::*timeout_clbk_t)();
    typedef void(H::*response_clbk_t)(Diameter::Message&);

    void set_timeout_clbk(timeout_clbk_t fun)
    {
      _timeout_clbk = fun;
    }

    void set_response_clbk(response_clbk_t fun)
    {
      _response_clbk = fun;
    }

  protected:
    H* _handler;
    timeout_clbk_t _timeout_clbk;
    response_clbk_t _response_clbk;

    void on_timeout()
    {
      if ((_handler != NULL) && (_timeout_clbk != NULL))
      {
        boost::bind(_timeout_clbk, _handler)();
      }
    }

    void on_response(Diameter::Message& rsp)
    {
      if ((_handler != NULL) && (_response_clbk != NULL))
      {
        boost::bind(_response_clbk, _handler, rsp);
      }
    }
  };

protected:
  static Diameter::Stack* _diameter_stack;
  static std::string _dest_realm;
  static std::string _dest_host;
  static std::string _server_name;
  static Cx::Dictionary _dict;
};

class ImpiDigestHandler : public HssCacheHandler
{
public:
  ImpiDigestHandler(HttpStack::Request& req) :
    HssCacheHandler(req), _impi(), _impu()
  {}

  void run();
  void on_mar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::DiameterTransaction<ImpiDigestHandler> DiameterTransaction;

private:
  std::string _impi;
  std::string _impu;
};


class ImpiAvHandler : public HssCacheHandler
{
public:
  ImpiAvHandler(HttpStack::Request& req) :
    HssCacheHandler(req), _impi(), _impu(), _scheme(), _authorization()
  {}

  void run();
  void on_mar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::DiameterTransaction<ImpiAvHandler> DiameterTransaction;

private:
  std::string _impi;
  std::string _impu;
  std::string _scheme;
  std::string _authorization;
};


class ImpuIMSSubscriptionHandler : public HssCacheHandler
{
public:
  ImpuIMSSubscriptionHandler(HttpStack::Request& req) :
    HssCacheHandler(req), _impi(), _impu()
  {}

  void run();
  void on_sar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::DiameterTransaction<ImpuIMSSubscriptionHandler> DiameterTransaction;

private:
  std::string _impi;
  std::string _impu;
};

#endif
