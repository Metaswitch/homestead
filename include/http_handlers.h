/**
 * @file http_handlers.h handlers for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HTTP_HANDLERS_H__
#define HTTP_HANDLERS_H__

#include "cx.h"
#include "diameterstack.h"
#include "httpstack_utils.h"
#include "sas.h"
#include "sproutconnection.h"
#include "health_checker.h"
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

  inline HssCacheProcessor* cache() const
  {
    return _cache;
  }

  void on_diameter_timeout();

protected:
  static std::string _configured_server_name;
  static HssCacheProcessor* _cache;
  static HssConnection::HssConnection* _hss;
  static HealthChecker* _health_checker;
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
#endif
