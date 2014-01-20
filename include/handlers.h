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
#include "cache.h"
#include "httpstack.h"

// Result-Code AVP constants
const int DIAMETER_SUCCESS = 2001;
const int DIAMETER_COMMAND_UNSUPPORTED = 3001;
const int DIAMETER_TOO_BUSY = 3004;
const int DIAMETER_AUTHORIZATION_REJECTED = 5003;
const int DIAMETER_UNABLE_TO_COMPLY = 5012;
// Experimental-Result-Code AVP constants
const int DIAMETER_FIRST_REGISTRATION = 2001;
const int DIAMETER_SUBSEQUENT_REGISTRATION = 2002;
const int DIAMETER_UNREGISTERED_SERVICE = 2003;
const int DIAMETER_ERROR_USER_UNKNOWN = 5001;
const int DIAMETER_ERROR_IDENTITIES_DONT_MATCH = 5002;
const int DIAMETER_ERROR_IDENTITY_NOT_REGISTERED = 5003;
const int DIAMETER_ERROR_ROAMING_NOT_ALLOWED = 5004;

// JSON string constants
const std::string JSON_RC = "result-code";
const std::string JSON_SCSCF = "scscf";

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
                                 const std::string& server_name,
                                 Cx::Dictionary* dict);
  static void configure_cache(Cache* cache);

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
        boost::bind(_response_clbk, _handler, rsp)();
      }
    }
  };

  template <class H>
  class CacheTransaction : public Cache::Transaction
  {
  public:
    CacheTransaction(Cache::Request* request, H* handler) :
      Cache::Transaction(request),
      _handler(handler),
      _success_clbk(NULL),
      _failure_clbk(NULL)
    {};

    typedef void(H::*success_clbk_t)(Cache::Request*);
    typedef void(H::*failure_clbk_t)(Cache::Request*, Cache::ResultCode, std::string&);

    void set_success_clbk(success_clbk_t fun)
    {
      _success_clbk = fun;
    }

    void set_failure_clbk(failure_clbk_t fun)
    {
      _failure_clbk = fun;
    }

  protected:
    H* _handler;
    success_clbk_t _success_clbk;
    failure_clbk_t _failure_clbk;

    void on_success()
    {
      if ((_handler != NULL) && (_success_clbk != NULL))
      {
        boost::bind(_success_clbk, _handler, _req)();
      }
    }

    void on_failure(Cache::ResultCode error, std::string& text)
    {
      if ((_handler != NULL) && (_failure_clbk != NULL))
      {
        boost::bind(_failure_clbk, _handler, _req, error, text)();
      }
    }
  };

protected:
  static Diameter::Stack* _diameter_stack;
  static std::string _dest_realm;
  static std::string _dest_host;
  static std::string _server_name;
  static Cx::Dictionary* _dict;
  static Cache* _cache;
};

class ImpiHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true, int _impu_cache_ttl = 0) : query_cache_av(!_hss_configured), impu_cache_ttl(_impu_cache_ttl) {}
    bool query_cache_av;
    int impu_cache_ttl;
  };

  ImpiHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impi(), _impu(), _scheme(), _authorization()
  {}

  void run();
  virtual bool parse_request() = 0;
  void query_cache_av();
  void on_get_av_success(Cache::Request* request);
  void on_get_av_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void get_av();
  void query_cache_impu();
  void on_get_impu_success(Cache::Request* request);
  void on_get_impu_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void send_mar();
  void on_mar_response(Diameter::Message& rsp);
  virtual void send_reply(const DigestAuthVector& av) = 0;
  virtual void send_reply(const AKAAuthVector& av) = 0;

  typedef HssCacheHandler::CacheTransaction<ImpiHandler> CacheTransaction;
  typedef HssCacheHandler::DiameterTransaction<ImpiHandler> DiameterTransaction;

protected:
  static const std::string SCHEME_UNKNOWN;
  static const std::string SCHEME_SIP_DIGEST;
  static const std::string SCHEME_DIGEST_AKAV1_MD5;

  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _scheme;
  std::string _authorization;
};

class ImpiDigestHandler : public ImpiHandler
{
public:
  ImpiDigestHandler(HttpStack::Request& req, const ImpiHandler::Config* cfg) : ImpiHandler(req, cfg) {}

  bool parse_request();
  void send_reply(const DigestAuthVector& av);
  void send_reply(const AKAAuthVector& av);
};


class ImpiAvHandler : public ImpiHandler
{
public:
  ImpiAvHandler(HttpStack::Request& req, const ImpiHandler::Config* cfg) : ImpiHandler(req, cfg) {}

  bool parse_request();
  void send_reply(const DigestAuthVector& av);
  void send_reply(const AKAAuthVector& av);
};

class ImpiRegistrationStatusHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true) : hss_configured(_hss_configured) {}
    bool hss_configured;
  };

  ImpiRegistrationStatusHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impi(), _impu(), _visited_network(), _authorization_type()
  {}

  void run();
  void on_uar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::DiameterTransaction<ImpiRegistrationStatusHandler> DiameterTransaction;

private:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _visited_network;
  std::string _authorization_type;
};

class ImpuLocationInfoHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true) : hss_configured(_hss_configured) {}
    bool hss_configured;
  };

  ImpuLocationInfoHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impu(), _originating(), _authorization_type()
  {}

  void run();
  void on_lir_response(Diameter::Message& rsp);

  typedef HssCacheHandler::DiameterTransaction<ImpuLocationInfoHandler> DiameterTransaction;

private:
  const Config* _cfg;
  std::string _impu;
  std::string _originating;
  std::string _authorization_type;
};

class ImpuIMSSubscriptionHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true, int _ims_sub_cache_ttl = 3600) : hss_configured(_hss_configured), ims_sub_cache_ttl(_ims_sub_cache_ttl) {}
    bool hss_configured;
    int ims_sub_cache_ttl;
  };

  ImpuIMSSubscriptionHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impi(), _impu()
  {}

  void run();
  void on_get_ims_subscription_success(Cache::Request* request);
  void on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void on_sar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::CacheTransaction<ImpuIMSSubscriptionHandler> CacheTransaction;
  typedef HssCacheHandler::DiameterTransaction<ImpuIMSSubscriptionHandler> DiameterTransaction;

private:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;

  static std::vector<std::string> get_public_ids(const std::string& user_data);
};

#endif
