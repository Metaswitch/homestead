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
#include "statisticsmanager.h"
#include "serverassignmenttype.h"

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

// Result-Code AVP strings used in set_result_code function
const std::string DIAMETER_REQ_SUCCESS = "DIAMETER_SUCCESS";
const std::string DIAMETER_REQ_FAILURE = "DIAMETER_UNABLE_TO_COMPLY";

// JSON string constants
const std::string JSON_DIGEST_HA1 = "digest_ha1";
const std::string JSON_DIGEST = "digest";
const std::string JSON_HA1 = "ha1";
const std::string JSON_REALM = "realm";
const std::string JSON_QOP = "qop";
const std::string JSON_AKA = "aka";
const std::string JSON_CHALLENGE = "challenge";
const std::string JSON_RESPONSE = "response";
const std::string JSON_CRYPTKEY = "cryptkey";
const std::string JSON_INTEGRITYKEY = "integritykey";
const std::string JSON_RC = "result-code";
const std::string JSON_SCSCF = "scscf";

// Server Assignment Types
const ServerAssignmentType REG(false, ServerAssignmentType::REGISTRATION, false);
const ServerAssignmentType REREG(true, ServerAssignmentType::RE_REGISTRATION, false);
const ServerAssignmentType DEREG_USER(false, ServerAssignmentType::USER_DEREGISTRATION, true);
const ServerAssignmentType DEREG_TIMEOUT(false, ServerAssignmentType::TIMEOUT_DEREGISTRATION, true);
const ServerAssignmentType DEREG_AUTH_FAIL(false, ServerAssignmentType::AUTHENTICATION_FAILURE, true);
const ServerAssignmentType DEREG_AUTH_TIMEOUT(false, ServerAssignmentType::AUTHENTICATION_TIMEOUT, true);
const ServerAssignmentType DEREG_ADMIN(false, ServerAssignmentType::ADMINISTRATIVE_DEREGISTRATION, true);
const ServerAssignmentType CALL_REG(true, ServerAssignmentType::NO_ASSIGNMENT, false);
const ServerAssignmentType CALL_UNREG(true, ServerAssignmentType::UNREGISTERED_USER, false);
const std::map<std::string, ServerAssignmentType> SERVER_ASSIGNMENT_TYPES = {{"reg", REG},
                                                                             {"rereg", REREG},
                                                                             {"dereg-user", DEREG_USER},
                                                                             {"dereg-timeout", DEREG_TIMEOUT},
                                                                             {"dereg-auth-fail", DEREG_AUTH_FAIL},
                                                                             {"dereg-auth-timeout", DEREG_AUTH_TIMEOUT},
                                                                             {"dereg-admin", DEREG_ADMIN},
                                                                             {"call-reg", CALL_REG},
                                                                             {"call-unreg", CALL_UNREG}};

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
  static void configure_stats(StatisticsManager* stats_manager);

  inline Cache* cache() const {return _cache;}

  void on_diameter_timeout();

  // Stats the HSS cache handlers can update.
  enum StatsFlags
  {
    STAT_HSS_LATENCY              = 0x1,
    STAT_HSS_DIGEST_LATENCY       = 0x2,
    STAT_HSS_SUBSCRIPTION_LATENCY = 0x4,
    STAT_CACHE_LATENCY            = 0x8,
  };

  template <class H>
  class DiameterTransaction : public Diameter::Transaction
  {
  public:

    DiameterTransaction(Cx::Dictionary* dict,
                        H* handler,
                        StatsFlags stat_updates) :
      Diameter::Transaction(dict),
      _handler(handler),
      _timeout_clbk(&HssCacheHandler::on_diameter_timeout),
      _response_clbk(NULL),
      _stat_updates(stat_updates)
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
    StatsFlags _stat_updates;

    void on_timeout()
    {
      update_latency_stats();

      if ((_handler != NULL) && (_timeout_clbk != NULL))
      {
        boost::bind(_timeout_clbk, _handler)();
      }
    }

    void on_response(Diameter::Message& rsp)
    {
      update_latency_stats();

      // If we got an overload response (result code of 3004) record a penalty
      // for the purposes of overload control.
      int32_t result_code;
      if (rsp.result_code(result_code) && (result_code == 3004))
      {
        _handler->record_penalty();
      }

      if ((_handler != NULL) && (_response_clbk != NULL))
      {
        boost::bind(_response_clbk, _handler, rsp)();
      }
    }

  private:
    void update_latency_stats()
    {
      StatisticsManager* stats = HssCacheHandler::_stats_manager;

      if (stats != NULL)
      {
        unsigned long latency = 0;
        if (get_duration(latency))
        {
          if (_stat_updates & STAT_HSS_LATENCY)
          {
            stats->update_H_hss_latency_us(latency);
          }
          if (_stat_updates & STAT_HSS_DIGEST_LATENCY)
          {
            stats->update_H_hss_digest_latency_us(latency);
          }
          if (_stat_updates & STAT_HSS_SUBSCRIPTION_LATENCY)
          {
            stats->update_H_hss_subscription_latency_us(latency);
          }
        }
      }
    }
  };

  template <class H>
  class CacheTransaction : public Cache::Transaction
  {
  public:
    CacheTransaction(H* handler) :
      Cache::Transaction(),
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

    void on_success(Cache::Request* req)
    {
      update_latency_stats();

      if ((_handler != NULL) && (_success_clbk != NULL))
      {
        boost::bind(_success_clbk, _handler, req)();
      }
    }

    void on_failure(Cache::Request* req,
                    Cache::ResultCode error,
                    std::string& text)
    {
      update_latency_stats();

      if ((_handler != NULL) && (_failure_clbk != NULL))
      {
        boost::bind(_failure_clbk, _handler, req, error, text)();
      }
    }

  private:
    void update_latency_stats()
    {
      StatisticsManager* stats = HssCacheHandler::_stats_manager;

      unsigned long latency = 0;
      if ((stats != NULL) && get_duration(latency))
      {
        stats->update_H_cache_latency_us(latency);
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
  static StatisticsManager* _stats_manager;
};

class ImpiHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true,
           int _impu_cache_ttl = 0,
           std::string _scheme_unknown = "Unknown",
           std::string _scheme_digest = "SIP Digest",
           std::string _scheme_aka = "Digest-AKAv1-MD5") :
    query_cache_av(!_hss_configured),
    impu_cache_ttl(_impu_cache_ttl),
    scheme_unknown(_scheme_unknown),
    scheme_digest(_scheme_digest),
    scheme_aka(_scheme_aka) {}

    bool query_cache_av;
    int impu_cache_ttl;
    std::string scheme_unknown;
    std::string scheme_digest;
    std::string scheme_aka;
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

  // Initialise the type field to reflect default behaviour if no type is specified.
  // That is, look for IMS subscription information in the cache. If we don't find
  // any, assume we have a first registration and query the cache with
  // Server-Assignment-Type set to REGISTRATION (1) and cache any IMS subscription
  // information that gets returned.
  ImpuIMSSubscriptionHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impi(), _impu(), _type(true, ServerAssignmentType::REGISTRATION, false)
  {}

  void run();
  void on_get_ims_subscription_success(Cache::Request* request);
  void on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void send_server_assignment_request();
  void on_sar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::CacheTransaction<ImpuIMSSubscriptionHandler> CacheTransaction;
  typedef HssCacheHandler::DiameterTransaction<ImpuIMSSubscriptionHandler> DiameterTransaction;

private:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  ServerAssignmentType _type;
};

class RegistrationTerminationHandler : public Diameter::Stack::Handler
{
public:
  struct Config
  {
    Config(Cache* _cache,
           Cx::Dictionary* _dict,
           int _ims_sub_cache_ttl = 3600) :
           cache(_cache),
           dict(_dict),
           ims_sub_cache_ttl(_ims_sub_cache_ttl) {}

    Cache* cache;
    Cx::Dictionary* dict;
    int ims_sub_cache_ttl;
  };

  RegistrationTerminationHandler(Diameter::Message& msg, const Config* cfg) :
    Diameter::Stack::Handler(msg), _cfg(cfg)
  {}

  void run();

  typedef HssCacheHandler::CacheTransaction<RegistrationTerminationHandler> CacheTransaction;

private:
  const Config* _cfg;
  std::vector<std::string> _impis;
  std::vector<std::string> _impus;

  void on_get_public_ids_success(Cache::Request* request);
  void on_get_public_ids_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void delete_identities();
};

class PushProfileHandler : public Diameter::Stack::Handler
{
public:
  struct Config
  {
    Config(Cache* _cache,
           Cx::Dictionary* _dict,
           int _impu_cache_ttl = 0,
           int _ims_sub_cache_ttl = 3600) :
           cache(_cache),
           dict(_dict),
           impu_cache_ttl(_impu_cache_ttl),
           ims_sub_cache_ttl(_ims_sub_cache_ttl) {}

    Cache* cache;
    Cx::Dictionary* dict;
    int impu_cache_ttl;
    int ims_sub_cache_ttl;
  };

  PushProfileHandler(Diameter::Message& msg, const Config* cfg) :
    Diameter::Stack::Handler(msg), _cfg(cfg)
  {}

  void run();

private:
  const Config* _cfg;
};
#endif
