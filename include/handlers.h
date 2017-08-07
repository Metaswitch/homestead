/**
 * @file handlers.h handlers for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HANDLERS_H__
#define HANDLERS_H__

#include <boost/bind.hpp>

#include "cx.h"
#include "diameterstack.h"
#include "cache.h"
#include "httpstack_utils.h"
#include "statisticsmanager.h"
#include "sas.h"
#include "sproutconnection.h"
#include "health_checker.h"
#include "snmp_cx_counter_table.h"

// Result-Code AVP constants
const int32_t DIAMETER_SUCCESS = 2001;
const int32_t DIAMETER_COMMAND_UNSUPPORTED = 3001;
const int32_t DIAMETER_UNABLE_TO_DELIVER = 3002;
const int32_t DIAMETER_TOO_BUSY = 3004;
const int32_t DIAMETER_AUTHORIZATION_REJECTED = 5003;
const int32_t DIAMETER_UNABLE_TO_COMPLY = 5012;
// 3GPP Vendor ID - RFC 5516
const uint32_t VENDOR_ID_3GPP = 10415;
// Experimental-Result-Code AVP constants
const int32_t DIAMETER_FIRST_REGISTRATION = 2001;
const int32_t DIAMETER_SUBSEQUENT_REGISTRATION = 2002;
const int32_t DIAMETER_UNREGISTERED_SERVICE = 2003;
const int32_t DIAMETER_ERROR_USER_UNKNOWN = 5001;
const int32_t DIAMETER_ERROR_IDENTITIES_DONT_MATCH = 5002;
const int32_t DIAMETER_ERROR_IDENTITY_NOT_REGISTERED = 5003;
const int32_t DIAMETER_ERROR_ROAMING_NOT_ALLOWED = 5004;
const int32_t DIAMETER_ERROR_IN_ASSIGNMENT_TYPE = 5007;

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
const std::string JSON_VERSION = "version";
const std::string JSON_RC = "result-code";
const std::string JSON_SCSCF = "scscf";
const std::string JSON_IMPUS = "impus";
const std::string JSON_WILDCARD = "wildcard-identity";

// HTTP query string field names
const std::string AUTH_FIELD_NAME = "resync-auth";

class HssCacheTask : public HttpStackUtils::Task
{
public:
  HssCacheTask(HttpStack::Request& req, SAS::TrailId trail) :
    HttpStackUtils::Task(req, trail)
  {};

  static void configure_diameter(Diameter::Stack* diameter_stack,
                                 const std::string& dest_realm,
                                 const std::string& dest_host,
                                 const std::string& server_name,
                                 Cx::Dictionary* dict);
  static void configure_cache(Cache* cache);
  static void configure_health_checker(HealthChecker* hc);
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
    typedef void(H::*timeout_clbk_t)();
    typedef void(H::*response_clbk_t)(Diameter::Message&);

    DiameterTransaction(Cx::Dictionary* dict,
                        H* handler,
                        StatsFlags stat_updates,
                        response_clbk_t response_clbk,
                        SNMP::CxCounterTable* cx_results_tbl,
                        timeout_clbk_t timeout_clbk = &HssCacheTask::on_diameter_timeout) :
      Diameter::Transaction(dict,
                            ((handler != NULL) ? handler->trail() : 0)),
      _handler(handler),
      _stat_updates(stat_updates),
      _response_clbk(response_clbk),
      _timeout_clbk(timeout_clbk),
      _cx_results_tbl(cx_results_tbl)
    {};

  protected:
    H* _handler;
    StatsFlags _stat_updates;
    response_clbk_t _response_clbk;
    timeout_clbk_t _timeout_clbk;
    SNMP::CxCounterTable* _cx_results_tbl;

    void on_timeout()
    {
      update_latency_stats();
      // No result-code returned on timeout, so use 0.
      _cx_results_tbl->increment(SNMP::DiameterAppId::TIMEOUT, 0);

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
      StatisticsManager* stats = HssCacheTask::_stats_manager;

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
  class CacheTransaction : public CassandraStore::Transaction
  {
  public:
    typedef void(H::*success_clbk_t)(CassandraStore::Operation*);
    typedef void(H::*failure_clbk_t)(CassandraStore::Operation*,
                                     CassandraStore::ResultCode,
                                     std::string&);
    CacheTransaction() :
      CassandraStore::Transaction(0),
      _handler(NULL),
      _success_clbk(NULL),
      _failure_clbk(NULL)
    {};

    CacheTransaction(H* handler,
                     success_clbk_t success_clbk,
                     failure_clbk_t failure_clbk) :
      CassandraStore::Transaction((handler != NULL) ? handler->trail() : 0),
      _handler(handler),
      _success_clbk(success_clbk),
      _failure_clbk(failure_clbk)
    {};

  protected:
    H* _handler;
    success_clbk_t _success_clbk;
    failure_clbk_t _failure_clbk;

    void on_success(CassandraStore::Operation* op)
    {
      update_latency_stats();

      if ((_handler != NULL) && (_success_clbk != NULL))
      {
        boost::bind(_success_clbk, _handler, op)();
      }
    }

    void on_failure(CassandraStore::Operation* op)
    {
      update_latency_stats();

      if ((_handler != NULL) && (_failure_clbk != NULL))
      {
        boost::bind(_failure_clbk,
                    _handler,
                    op,
                    op->get_result_code(),
                    op->get_error_text())();
      }
    }

  private:
    void update_latency_stats()
    {
      StatisticsManager* stats = HssCacheTask::_stats_manager;

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
  static std::string _configured_server_name;
  static Cx::Dictionary* _dict;
  static Cache* _cache;
  static HealthChecker* _health_checker;
  static StatisticsManager* _stats_manager;
};

class ImpiTask : public HssCacheTask
{
public:
  struct Config
  {
    Config(bool _hss_configured,
           int _impu_cache_ttl,
           std::string _scheme_unknown,
           std::string _scheme_digest,
           std::string _scheme_akav1,
           std::string _scheme_akav2,
           int _diameter_timeout_ms = 200) :
      query_cache_av(!_hss_configured),
      impu_cache_ttl(_impu_cache_ttl),
      scheme_unknown(_scheme_unknown),
      scheme_digest(_scheme_digest),
      scheme_akav1(_scheme_akav1),
      scheme_akav2(_scheme_akav2),
      diameter_timeout_ms(_diameter_timeout_ms) {}

    bool query_cache_av;
    int impu_cache_ttl;
    std::string scheme_unknown;
    std::string scheme_digest;
    std::string scheme_akav1;
    std::string scheme_akav2;
    int diameter_timeout_ms;
  };

  ImpiTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impi(), _impu(), _scheme(), _authorization(), _maa(NULL)
  {}

  void run();
  virtual ~ImpiTask();
  virtual bool parse_request() = 0;
  void query_cache_av();
  void on_get_av_success(CassandraStore::Operation* op);
  void on_get_av_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text);
  void get_av();
  void query_cache_impu();
  void on_get_impu_success(CassandraStore::Operation* op);
  void on_get_impu_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text);
  void on_put_assoc_impu_success(CassandraStore::Operation* op);
  void on_put_assoc_impu_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text);
  void send_mar();
  void on_mar_response(Diameter::Message& rsp);
  virtual void send_reply(const DigestAuthVector& av) = 0;
  virtual void send_reply(const AKAAuthVector& av) = 0;
  typedef HssCacheTask::CacheTransaction<ImpiTask> CacheTransaction;
  typedef HssCacheTask::DiameterTransaction<ImpiTask> DiameterTransaction;

protected:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _scheme;
  std::string _authorization;
  Cx::MultimediaAuthAnswer *_maa;
};

class ImpiDigestTask : public ImpiTask
{
public:
  ImpiDigestTask(HttpStack::Request& req,
                 const ImpiTask::Config* cfg,
                 SAS::TrailId trail) :
    ImpiTask(req, cfg, trail)
  {}

  bool parse_request();
  void send_reply(const DigestAuthVector& av);
  void send_reply(const AKAAuthVector& av);
};


class ImpiAvTask : public ImpiTask
{
public:
  ImpiAvTask(HttpStack::Request& req,
             const ImpiTask::Config* cfg,
             SAS::TrailId trail) :
    ImpiTask(req, cfg, trail)
  {}

  bool parse_request();
  void send_reply(const DigestAuthVector& av);
  void send_reply(const AKAAuthVector& av);
};

class ImpiRegistrationStatusTask : public HssCacheTask
{
public:
  struct Config
  {
    Config(bool _hss_configured = true,
           int _diameter_timeout_ms = 200) :
      hss_configured(_hss_configured),
      diameter_timeout_ms(_diameter_timeout_ms) {}
    bool hss_configured;
    int diameter_timeout_ms;
  };

  ImpiRegistrationStatusTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impi(), _impu(), _visited_network(), _authorization_type(), _emergency()
  {}

  void run();
  void on_uar_response(Diameter::Message& rsp);
  void sas_log_hss_failure(int32_t result_code,int32_t experimental_result_code);

  typedef HssCacheTask::DiameterTransaction<ImpiRegistrationStatusTask> DiameterTransaction;

private:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _visited_network;
  std::string _authorization_type;
  bool _emergency;
};

class ImpuLocationInfoTask : public HssCacheTask
{
public:
  struct Config
  {
    Config(bool _hss_configured = true,
           int _diameter_timeout_ms = 200) :
      hss_configured(_hss_configured),
      diameter_timeout_ms(_diameter_timeout_ms) {}
    bool hss_configured;
    int diameter_timeout_ms;
  };

  ImpuLocationInfoTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impu(), _originating(), _authorization_type()
  {}

  void run();
  void on_lir_response(Diameter::Message& rsp);
  void sas_log_hss_failure(int32_t result_code,int32_t experimental_result_code);
  void query_cache_reg_data();
  void on_get_reg_data_success(CassandraStore::Operation* op);
  void on_get_reg_data_failure(CassandraStore::Operation* op,
                               CassandraStore::ResultCode error,
                               std::string& text);

  typedef HssCacheTask::DiameterTransaction<ImpuLocationInfoTask> DiameterTransaction;
  typedef HssCacheTask::CacheTransaction<ImpuLocationInfoTask> CacheTransaction;

private:
  const Config* _cfg;
  std::string _impu;
  std::string _originating;
  std::string _authorization_type;
};

class ImpuRegDataTask : public HssCacheTask
{
public:
  struct Config
  {
    Config(bool _hss_configured = true,
           int _hss_reregistration_time = 3600,
           int _record_ttl = 7200,
           int _diameter_timeout_ms = 200,
           bool _support_shared_ifcs = true) :
      hss_configured(_hss_configured),
      hss_reregistration_time(_hss_reregistration_time),
      record_ttl(_record_ttl),
      diameter_timeout_ms(_diameter_timeout_ms),
      support_shared_ifcs(_support_shared_ifcs) {}

    bool hss_configured;
    int hss_reregistration_time;
    int record_ttl;
    int diameter_timeout_ms;
    bool support_shared_ifcs;
  };

  ImpuRegDataTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impi(), _impu(), _http_rc(HTTP_OK)
  {}
  virtual ~ImpuRegDataTask() {};
  virtual void run();
  void get_reg_data();
  void on_get_reg_data_success(CassandraStore::Operation* op);
  void on_get_reg_data_failure(CassandraStore::Operation* op,
                               CassandraStore::ResultCode error,
                               std::string& text);
  void send_server_assignment_request(Cx::ServerAssignmentType type);
  void on_sar_response(Diameter::Message& rsp);
  void on_put_reg_data_success(CassandraStore::Operation* op);
  void on_put_reg_data_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text);
  void on_del_impu_success(CassandraStore::Operation* op);
  void on_del_impu_benign(CassandraStore::Operation* op, bool not_found);
  void on_del_impu_failure(CassandraStore::Operation* op, CassandraStore::ResultCode error, std::string& text);

  typedef HssCacheTask::CacheTransaction<ImpuRegDataTask> CacheTransaction;
  typedef HssCacheTask::DiameterTransaction<ImpuRegDataTask> DiameterTransaction;

  std::string public_id();
  std::string wildcard_id();

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
  std::string server_name_from_body(std::string body);
  std::string wildcard_from_body(std::string body);
  std::vector<std::string> get_associated_private_ids();

  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _type_param;
  RequestType _type;
  std::string _xml;
  RegistrationState _original_state;
  RegistrationState _new_state;
  ChargingAddresses _charging_addrs;
  long _http_rc;
  std::string _provided_server_name;

  // Save off the wildcard sent from sprout and the wildcard received from the
  // HSS as separate class variables, so that they can be compared.
  // This is necessary so we can tell if the HSS has sent an updated wildcard to
  // Homestead, as the wildcard from the HSS will not write over the original
  // wildcard sent from Sprout.
  std::string _sprout_wildcard;
  std::string _hss_wildcard;
};

class ImpuReadRegDataTask : public ImpuRegDataTask
{
public:
  // Just use the superclass's constructor.
  using ImpuRegDataTask::ImpuRegDataTask;

  virtual ~ImpuReadRegDataTask() {}
  virtual void run();
};

class RegistrationTerminationTask : public Diameter::Task
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
    int reg_max_expires;
  };

  RegistrationTerminationTask(const Diameter::Dictionary* dict,
                              struct msg** fd_msg,
                              const Config* cfg,
                              SAS::TrailId trail):
    Diameter::Task(dict, fd_msg, trail), _cfg(cfg), _rtr(_msg)
  {}

  void run();

  typedef HssCacheTask::CacheTransaction<RegistrationTerminationTask> CacheTransaction;

private:
  const Config* _cfg;
  Cx::RegistrationTerminationRequest _rtr;

  int32_t _deregistration_reason;
  std::vector<std::string> _impis;
  std::vector<std::string> _impus;
  std::vector< std::pair<std::string, std::vector<std::string>> > _registration_sets;

  void get_assoc_primary_public_ids_success(CassandraStore::Operation* op);
  void get_assoc_primary_public_ids_failure(CassandraStore::Operation* op,
                                            CassandraStore::ResultCode error,
                                            std::string& text);
  void get_registration_sets();
  void get_registration_set_success(CassandraStore::Operation* op);
  void get_registration_set_failure(CassandraStore::Operation* op,
                                    CassandraStore::ResultCode error,
                                    std::string& text);
  void delete_registrations();
  void dissociate_implicit_registration_sets();
  void delete_impi_mappings();
  void send_rta(const std::string result_code);
};

class PushProfileTask : public Diameter::Task
{
public:
  struct Config
  {
    Config(Cache* _cache,
           Cx::Dictionary* _dict,
           int _impu_cache_ttl = 0,
           int _hss_reregistration_time = 3600,
           int _record_ttl = 7200) :
      cache(_cache),
      dict(_dict),
      impu_cache_ttl(_impu_cache_ttl),
      hss_reregistration_time(_hss_reregistration_time),
      record_ttl(_record_ttl) {}

    Cache* cache;
    Cx::Dictionary* dict;
    int impu_cache_ttl;
    int hss_reregistration_time;
    int record_ttl;
  };

  PushProfileTask(const Diameter::Dictionary* dict,
                  struct msg** fd_msg,
                  const Config* cfg,
                  SAS::TrailId trail) :
    Diameter::Task(dict, fd_msg, trail), _cfg(cfg), _ppr(_msg)
  {}

  void run();

  typedef HssCacheTask::CacheTransaction<PushProfileTask> CacheTransaction;

private:
  const Config* _cfg;
  Cx::PushProfileRequest _ppr;

  bool _ims_sub_present;
  std::string _ims_subscription;
  bool _charging_addrs_present;
  ChargingAddresses _charging_addrs;
  std::string _impi;
  std::string _default_public_id;
  std::string _first_default_impu;
  std::string _new_default_impu;
  std::vector<std::string> _impus;
  std::vector<std::string> _default_impus;
  std::vector<std::string> _irs_impus;
  std::vector<std::string> _impus_to_delete;
  RegistrationState _reg_state;
  ChargingAddresses _reg_charging_addrs;


  void on_get_primary_impus_success(CassandraStore::Operation* op);
  void on_get_primary_impus_failure(CassandraStore::Operation* op,
                                    CassandraStore::ResultCode error,
                                    std::string& text);
  void get_irs();
  void on_get_irs_success(CassandraStore::Operation* op);
  void on_get_irs_failure(CassandraStore::Operation* op,
                                      CassandraStore::ResultCode error,
                                      std::string& text);
  inline bool is_first_irs()
  {
    return (_default_public_id == _first_default_impu);
  }
  bool default_impus_match();
  void ims_sub_get_ids();
  void update_reg_data();
  void update_reg_data_success(CassandraStore::Operation* op);
  void update_reg_data_failure(CassandraStore::Operation* op,
                               CassandraStore::ResultCode error,
                               std::string& text);
  void find_impus_to_delete();
  void delete_impus();
  bool any_new_impus();
  inline bool should_send_ppa()
  {
    return is_first_irs();
  }
  void send_ppa(const std::string result_code);
};

class ImpuListTask : public HssCacheTask
{
public:
  struct Config {};

  ImpuListTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg)
  {}

  virtual ~ImpuListTask() {};

  virtual void run();

  void on_list_impu_success(CassandraStore::Operation* op);
  void on_list_impu_failure(CassandraStore::Operation* op,
                            CassandraStore::ResultCode error,
                            std::string& text);

  typedef HssCacheTask::CacheTransaction<ImpuListTask> CacheTransaction;

protected:
  const Config* _cfg;
};

void configure_cx_results_tables(SNMP::CxCounterTable* mar_results_table,
                                 SNMP::CxCounterTable* sar_results_table,
                                 SNMP::CxCounterTable* uar_results_table,
                                 SNMP::CxCounterTable* lir_results_table,
                                 SNMP::CxCounterTable* ppr_results_table,
                                 SNMP::CxCounterTable* rtr_results_table);
#endif
