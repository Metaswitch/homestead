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
#include "hss_connection.h"
#include "hss_cache_processor.h"
#include "implicit_reg_set.h"

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
const std::string SERVER_NAME_FIELD = "server-name";

class HssCacheTask : public HttpStackUtils::Task
{
public:
  HssCacheTask(HttpStack::Request& req, SAS::TrailId trail) :
    HttpStackUtils::Task(req, trail)
  {};

  static void configure_hss_connection(HssConnection::HssConnection* hss,
                                       std::string server_name);
  static void configure_cache(HssCacheProcessor* cache);
  static void configure_health_checker(HealthChecker* hc);
  static void configure_stats(StatisticsManager* stats_manager);

  inline HssCacheProcessor* cache() const
  {
    return _cache;
  }

  void on_diameter_timeout();

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
  static std::string _configured_server_name;
  static HssCacheProcessor* _cache;
  static HssConnection::HssConnection* _hss;
  static HealthChecker* _health_checker;
  static StatisticsManager* _stats_manager;
};

class ImpiTask : public HssCacheTask
{
public:
  struct Config
  {
    Config(std::string _scheme_unknown,
           std::string _scheme_digest,
           std::string _scheme_akav1,
           std::string _scheme_akav2) :
      scheme_unknown(_scheme_unknown),
      scheme_digest(_scheme_digest),
      scheme_akav1(_scheme_akav1),
      scheme_akav2(_scheme_akav2) {}

    std::string scheme_unknown;
    std::string scheme_digest;
    std::string scheme_akav1;
    std::string scheme_akav2;
    std::string default_realm;
  };

  ImpiTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impi(), _impu(), _scheme(), _authorization()
  {}

  void run();
  virtual ~ImpiTask() {};
  virtual bool parse_request() = 0;
  void get_av();
  void send_mar();
  void on_mar_response(const HssConnection::MultimediaAuthAnswer& maa);
  virtual void send_reply(const DigestAuthVector& av) = 0;
  virtual void send_reply(const AKAAuthVector& av) = 0;

protected:
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _scheme;
  std::string _authorization;
  std::string _provided_server_name;
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
    Config(std::string default_realm) :
      default_realm(default_realm) {}
    std::string default_realm;
  };

  ImpiRegistrationStatusTask(HttpStack::Request& req, const Config* cfg, SAS::TrailId trail) :
    HssCacheTask(req, trail), _cfg(cfg), _impi(), _impu(), _visited_network(), _authorization_type(), _emergency()
  {}

  void run();
  void on_uar_response(const HssConnection::UserAuthAnswer& uaa);
  void sas_log_hss_failure(int32_t result_code,int32_t experimental_result_code);

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
  void on_lir_response(const HssConnection::LocationInfoAnswer& lia);
  void sas_log_hss_failure(int32_t result_code,int32_t experimental_result_code);

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
  void on_get_reg_data_success(ImplicitRegistrationSet* irs);
  void on_get_reg_data_failure(Store::Status rc);
  void send_server_assignment_request(Cx::ServerAssignmentType type);
  void on_sar_response(const HssConnection::ServerAssignmentAnswer& saa);
  void on_put_reg_data_success();
  void on_put_reg_data_failure(Store::Status rc);
  void on_del_impu_success();
  void on_del_impu_benign(bool not_found);
  void on_del_impu_failure(Store::Status rc);

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

  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _type_param;
  RequestType _type;
  RegistrationState _original_state;

  // These are now in the ImplicitRegistrationSet*
  std::string _xml;
  RegistrationState _new_state;
  ChargingAddresses _charging_addrs;

  // TODO
  ImplicitRegistrationSet* _irs;

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
    Config(HssCacheProcessor* _cache,
           Cx::Dictionary* _dict,
           SproutConnection* _sprout_conn,
           int _hss_reregistration_time = 3600) :
      cache(_cache),
      dict(_dict),
      sprout_conn(_sprout_conn),
      hss_reregistration_time(_hss_reregistration_time) {}

    HssCacheProcessor* cache;
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
  void get_registration_sets_success(std::vector<ImplicitRegistrationSet*> reg_sets);
  void get_registration_sets_failure(Store::Status rc);
  void delete_reg_sets_success();
  void delete_reg_sets_failure(Store::Status rc);

  void send_rta(const std::string result_code);
};

class PushProfileTask : public Diameter::Task
{
public:
  struct Config
  {
    Config(HssCacheProcessor* _cache,
           Cx::Dictionary* _dict,
           int _impu_cache_ttl = 0,
           int _hss_reregistration_time = 3600,
           int _record_ttl = 7200) :
      cache(_cache),
      dict(_dict),
      impu_cache_ttl(_impu_cache_ttl),
      hss_reregistration_time(_hss_reregistration_time),
      record_ttl(_record_ttl) {}

    HssCacheProcessor* cache;
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
  std::string _first_default_id;
  std::vector<std::string> _impus;
  std::vector<std::string> _default_impus;
  std::vector<std::string> _irs_impus;
  std::vector<std::string> _impus_to_delete;
  RegistrationState _reg_state;
  ChargingAddresses _reg_charging_addrs;


  void on_get_ims_sub_success(ImsSubscription* ims_sub);
  void on_get_ims_sub_failure(Store::Status rc);

  void on_save_ims_sub_success();
  void on_save_ims_sub_failure(Store::Status rc);

  // Return true if the default ID is the first to be updated.
  // Else return false (if updating a further registration set.)
  inline bool check_if_first()
  {
  return (_default_public_id == _first_default_id);
  }
  void ims_sub_compare_default_ids();
  void ims_sub_get_ids();
  void no_ims_set_first_default();
  void find_impus_to_delete();
  void delete_impus();
  bool check_impus_added();
  void decide_if_send_ppa();
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
