/**
 * @file handlers.h handlers for homestead
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
#include "sproutconnection.h"

// Result-Code AVP constants
const int32_t DIAMETER_SUCCESS = 2001;
const int32_t DIAMETER_COMMAND_UNSUPPORTED = 3001;
const int32_t DIAMETER_TOO_BUSY = 3004;
const int32_t DIAMETER_AUTHORIZATION_REJECTED = 5003;
const int32_t DIAMETER_UNABLE_TO_COMPLY = 5012;
// Experimental-Result-Code AVP constants
const int32_t DIAMETER_FIRST_REGISTRATION = 2001;
const int32_t DIAMETER_SUBSEQUENT_REGISTRATION = 2002;
const int32_t DIAMETER_UNREGISTERED_SERVICE = 2003;
const int32_t DIAMETER_ERROR_USER_UNKNOWN = 5001;
const int32_t DIAMETER_ERROR_IDENTITIES_DONT_MATCH = 5002;
const int32_t DIAMETER_ERROR_IDENTITY_NOT_REGISTERED = 5003;
const int32_t DIAMETER_ERROR_ROAMING_NOT_ALLOWED = 5004;

// Result-Code AVP strings used in set_result_code function
const std::string DIAMETER_REQ_SUCCESS = "DIAMETER_SUCCESS";
const std::string DIAMETER_REQ_FAILURE = "DIAMETER_UNABLE_TO_COMPLY";

// Deregistration-Reason Reason-Code AVP constants
const int32_t PERMANENT_TERMINATION = 0;
const int32_t NEW_SERVER_ASSIGNED = 1;
const int32_t SERVER_CHANGE = 2;
const int32_t REMOVE_SCSCF = 3;

// JSON string constants
const std::string JSON_DIGEST_HA1 = "digest_ha1";
const std::string JSON_DIGEST = "digest";
const std::string JSON_HA1 = "ha1";
const std::string JSON_REALM = "realm";
const std::string JSON_QOP = "qop";
const std::string JSON_AUTH = "auth";
const std::string JSON_AKA = "aka";
const std::string JSON_CHALLENGE = "challenge";
const std::string JSON_RESPONSE = "response";
const std::string JSON_CRYPTKEY = "cryptkey";
const std::string JSON_INTEGRITYKEY = "integritykey";
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
  static void configure_stats(StatisticsManager* stats_manager);

  inline Cache* cache() const
  {
    return _cache;
  }

  void on_diameter_timeout();

  // Stats the HSS cache handlers can update.
  enum StatsFlags
  {
    STAT_HSS_LATENCY              = 0x1,
    STAT_HSS_DIGEST_LATENCY       = 0x2,
    STAT_HSS_SUBSCRIPTION_LATENCY = 0x4,
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

class ImpuRegDataHandler : public HssCacheHandler
{
public:
  struct Config
  {
    Config(bool _hss_configured = true, int _hss_reregistration_time = 3600) :
      hss_configured(_hss_configured),
      hss_reregistration_time(_hss_reregistration_time) {}
    bool hss_configured;
    int hss_reregistration_time;
  };

  ImpuRegDataHandler(HttpStack::Request& req, const Config* cfg) :
    HssCacheHandler(req), _cfg(cfg), _impi(), _impu()
  {}
  virtual ~ImpuRegDataHandler() {};
  virtual void run();
  void on_get_ims_subscription_success(Cache::Request* request);
  void on_get_ims_subscription_failure(Cache::Request* request, Cache::ResultCode error, std::string& text);
  void send_server_assignment_request(Cx::ServerAssignmentType type);
  void on_sar_response(Diameter::Message& rsp);

  typedef HssCacheHandler::CacheTransaction<ImpuRegDataHandler> CacheTransaction;
  typedef HssCacheHandler::DiameterTransaction<ImpuRegDataHandler> DiameterTransaction;

protected:

  // Represents the possible types of request that can be made in the
  // body of a PUT. Homestead determines what action to take (e.g.
  // what to set in the database, what to send to the HSS) based on a
  // combination of this type and the user's registration state in the
  // Cassandra database.
  enum RequestType
  {
    UNKNOWN, REG, CALL, DEREG_USER, DEREG_ADMIN, DEREG_TIMEOUT, DEREG_AUTH_FAIL, DEREG_AUTH_TIMEOUT
  };

  virtual void send_reply();
  void put_in_cache();
  bool is_deregistration_request(RequestType type);
  bool is_auth_failure_request(RequestType type);
  Cx::ServerAssignmentType sar_type_for_request(RequestType type);
  RequestType request_type_from_body(std::string body);
  std::vector<std::string> get_associated_private_ids();

  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _type_param;
  RequestType _type;
  std::string _xml;
  RegistrationState _new_state;
};

class ImpuIMSSubscriptionHandler : public ImpuRegDataHandler
{
public:
  ImpuIMSSubscriptionHandler(HttpStack::Request& req, const Config* cfg) :
    ImpuRegDataHandler(req, cfg)
  {};

  void run();
private:
  void send_reply();
};

class RegistrationTerminationHandler : public Diameter::Stack::Handler
{
public:
  struct Config
  {
    Config(Cache* _cache,
           Cx::Dictionary* _dict,
           SproutConnection* _sprout_conn,
           int _hss_reregistration_time = 3600) :
      cache(_cache),
      dict(_dict),
      sprout_conn(_sprout_conn),
      hss_reregistration_time(_hss_reregistration_time) {}

    Cache* cache;
    Cx::Dictionary* dict;
    SproutConnection* sprout_conn;
    int hss_reregistration_time;
  };

  RegistrationTerminationHandler(Diameter::Dictionary* dict, struct msg** fd_msg, const Config* cfg) :
    Diameter::Stack::Handler(dict, fd_msg), _cfg(cfg)
  {}

  void run();

  typedef HssCacheHandler::CacheTransaction<RegistrationTerminationHandler> CacheTransaction;

private:
  const Config* _cfg;
  int32_t _deregistration_reason;
  std::vector<std::string> _impis;
  std::vector<std::string> _impus;
  std::vector<std::vector<std::string>> _registration_sets;

  void get_assoc_primary_public_ids_success(Cache::Request* request);
  void get_assoc_primary_public_ids_failure(Cache::Request* request,
                                            Cache::ResultCode error,
                                            std::string& text);
  void get_registration_sets();
  void get_registration_set_success(Cache::Request* request);
  void get_registration_set_failure(Cache::Request* request,
                                    Cache::ResultCode error,
                                    std::string& text);
  void delete_registrations();
  void dissociate_implicit_registration_sets();
  void delete_impi_mappings();
  void send_rta(const std::string result_code);
};

class PushProfileHandler : public Diameter::Stack::Handler
{
public:
  struct Config
  {
    Config(Cache* _cache,
           Cx::Dictionary* _dict,
           int _impu_cache_ttl = 0,
           int _hss_reregistration_time = 3600) :
      cache(_cache),
      dict(_dict),
      impu_cache_ttl(_impu_cache_ttl),
      hss_reregistration_time(_hss_reregistration_time) {}

    Cache* cache;
    Cx::Dictionary* dict;
    int impu_cache_ttl;
    int hss_reregistration_time;
  };

  PushProfileHandler(Diameter::Dictionary* dict, struct msg** fd_msg, const Config* cfg) :
    Diameter::Stack::Handler(dict, fd_msg), _cfg(cfg)
  {}

  void run();

  typedef HssCacheHandler::CacheTransaction<PushProfileHandler> CacheTransaction;

private:
  const Config* _cfg;
  std::string _impi;
  DigestAuthVector _digest_av;
  std::string _ims_subscription;

  void update_av();
  void update_av_success(Cache::Request* request);
  void update_av_failure(Cache::Request* request,
                         Cache::ResultCode error,
                         std::string& text);
  void update_ims_subscription();
  void update_ims_subscription_success(Cache::Request* request);
  void update_ims_subscription_failure(Cache::Request* request,
                                       Cache::ResultCode error,
                                       std::string& text);
  void send_ppa(const std::string result_code);
};
#endif
