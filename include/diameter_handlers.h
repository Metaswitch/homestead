/**
 * @file diamter_handlers.h Diameter handlers for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef DIAMETER_HANDLERS_H__
#define DIAMETER_HANDLERS_H__

#include "cx.h"
#include "diameterstack.h"
#include "httpstack_utils.h"
#include "statisticsmanager.h"
#include "sas.h"
#include "sproutconnection.h"
#include "health_checker.h"
#include "snmp_cx_counter_table.h"
#include "hss_connection.h"
#include "hss_cache_processor.h"
#include "implicit_reg_set.h"

class RegistrationTerminationTask : public Diameter::Task
{
public:
  struct Config
  {
    Config(HssCacheProcessor* _cache,
           Cx::Dictionary* _dict,
           SproutConnection* _sprout_conn) :
      cache(_cache),
      dict(_dict),
      sprout_conn(_sprout_conn) {}

    HssCacheProcessor* cache;
    Cx::Dictionary* dict;
    SproutConnection* sprout_conn;
  };

  RegistrationTerminationTask(const Diameter::Dictionary* dict,
                              struct msg** fd_msg,
                              const Config* cfg,
                              SAS::TrailId trail):
    Diameter::Task(dict, fd_msg, trail), _cfg(cfg), _rtr(_msg)
  {}

  // We must delete all the ImplicitRegistrationSet*s in _reg_sets
  virtual ~RegistrationTerminationTask()
  {
    for (ImplicitRegistrationSet* irs : _reg_sets)
    {
      delete irs;
    }
  }

  void run();

private:
  const Config* _cfg;
  Cx::RegistrationTerminationRequest _rtr;

  std::vector<ImplicitRegistrationSet*> _reg_sets;

  int32_t _deregistration_reason;
  std::vector<std::string> _impis;
  std::vector<std::string> _impus;
  std::vector< std::pair<std::string, std::vector<std::string>> > _registration_sets;

  void get_registration_sets_success(std::vector<ImplicitRegistrationSet*> reg_sets);
  void get_registration_sets_failure(Store::Status rc);
  void delete_reg_sets_progress();
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
           SproutConnection* _sprout_conn) :
      cache(_cache),
      dict(_dict),
      sprout_conn(_sprout_conn) {}

    HssCacheProcessor* cache;
    Cx::Dictionary* dict;
    SproutConnection* sprout_conn;
  };

  PushProfileTask(const Diameter::Dictionary* dict,
                  struct msg** fd_msg,
                  const Config* cfg,
                  SAS::TrailId trail) :
    Diameter::Task(dict, fd_msg, trail), _cfg(cfg), _ppr(_msg)
  {}

  virtual ~PushProfileTask()
  {
    if (_ims_sub)
    {
      delete _ims_sub; _ims_sub = NULL;
    }
  }

  void run();

private:
  const Config* _cfg;
  Cx::PushProfileRequest _ppr;

  ImsSubscription* _ims_sub = NULL;

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


  void on_get_ims_sub_success(ImsSubscription* ims_sub);
  void on_get_ims_sub_failure(Store::Status rc);

  void on_save_ims_sub_progress();
  void on_save_ims_sub_success();
  void on_save_ims_sub_failure(Store::Status rc);

  void send_ppa(const std::string result_code);
};

void configure_handler_cx_results_tables(SNMP::CxCounterTable* ppr_results_table,
                                         SNMP::CxCounterTable* rtr_results_table);

#endif
