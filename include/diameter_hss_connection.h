/**
 * @file diameter_hss_connection.h Implementation of HssConnection that uses Diameter
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef DIAMETER_HSS_CONNECTION_H__
#define DIAMETER_HSS_CONNECTION_H__

#include <string>
#include "diameterstack.h"
#include "cx.h"
#include "snmp_cx_counter_table.h"
#include "hss_connection.h"

namespace HssConnection {

// Stats the HSS connection can update.
enum StatsFlags
{
  STAT_HSS_LATENCY              = 0x1,
  STAT_HSS_DIGEST_LATENCY       = 0x2,
  STAT_HSS_SUBSCRIPTION_LATENCY = 0x4,
};

const static StatsFlags DIGEST_STATS =
  static_cast<StatsFlags>(
    STAT_HSS_LATENCY |
    STAT_HSS_DIGEST_LATENCY);

const static StatsFlags SUBSCRIPTION_STATS =
  static_cast<StatsFlags>(
    STAT_HSS_LATENCY |
    STAT_HSS_SUBSCRIPTION_LATENCY);

class DiameterHssConnection : public HssConnection
{
public:
  virtual ~DiameterHssConnection() {};

  DiameterHssConnection(StatisticsManager* stats_manager,
                        Cx::Dictionary* dict,
                        Diameter::Stack* diameter_stack,
                        const std::string& dest_realm,
                        const std::string& dest_host,
                        int diameter_timeout_ms);

  // Send a multimedia auth request to the HSS
  virtual void send_multimedia_auth_request(maa_cb callback,
                                            MultimediaAuthRequest request,
                                            SAS::TrailId trail,
                                            Utils::StopWatch* stopwatch);

  // Send a user auth request to the HSS
  virtual void send_user_auth_request(uaa_cb callback,
                                      UserAuthRequest request,
                                      SAS::TrailId trail,
                                      Utils::StopWatch* stopwatch);

  // Send a location info request to the HSS
  virtual void send_location_info_request(lia_cb callback,
                                          LocationInfoRequest request,
                                          SAS::TrailId trail,
                                          Utils::StopWatch* stopwatch);

  // Send a server assignment request to the HSS
  virtual void send_server_assignment_request(saa_cb callback,
                                              ServerAssignmentRequest request,
                                              SAS::TrailId trail,
                                              Utils::StopWatch* stopwatch);

private:
  Cx::Dictionary* _dict;
  Diameter::Stack* _diameter_stack;
  std::string _dest_realm;
  std::string _dest_host;
  int _diameter_timeout_ms;

  // Inner classes for the DiameterTransactions.
  template <class AnswerType>
  class DiameterTransaction : public Diameter::Transaction
  {
  public:
    typedef std::function<void(const AnswerType&)> callback_t;

    DiameterTransaction(Cx::Dictionary* dict,
                        SAS::TrailId trail,
                        StatsFlags stat_updates,
                        callback_t response_clbk,
                        SNMP::CxCounterTable* cx_results_tbl,
                        StatisticsManager* stats_manager,
                        Utils::StopWatch* stopwatch) :
      Diameter::Transaction(dict, trail),
      _stat_updates(stat_updates),
      _response_clbk(response_clbk),
      _cx_results_tbl(cx_results_tbl),
      _stats_manager(stats_manager),
      _stopwatch(stopwatch)
    {};

    virtual ~DiameterTransaction() {};

  protected:
    StatsFlags _stat_updates;
    callback_t _response_clbk;
    SNMP::CxCounterTable* _cx_results_tbl;
    StatisticsManager* _stats_manager;
    Utils::StopWatch* _stopwatch;

    // Implementations will use this to create the correct answer
    virtual AnswerType create_answer(Diameter::Message& rsp) = 0;
    void on_timeout();
    void on_response(Diameter::Message& rsp);
    void increment_results(int32_t result, int32_t experimental, uint32_t vendor);
    void sas_log_hss_failure(int event_id,
                             int32_t result_code,
                             int32_t experimental_result_code);

  private:
    void update_latency_stats();
  };

  class MarDiameterTransaction : public DiameterTransaction<MultimediaAuthAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using DiameterTransaction::DiameterTransaction;

    virtual MultimediaAuthAnswer create_answer(Diameter::Message& rsp) override;
    virtual ~MarDiameterTransaction() {};
  };

  class UarDiameterTransaction : public DiameterTransaction<UserAuthAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using DiameterTransaction::DiameterTransaction;

    virtual UserAuthAnswer create_answer(Diameter::Message& rsp) override;
    virtual ~UarDiameterTransaction() {};
  };

  class LirDiameterTransaction : public DiameterTransaction<LocationInfoAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using DiameterTransaction::DiameterTransaction;

    virtual LocationInfoAnswer create_answer(Diameter::Message& rsp) override;
    virtual ~LirDiameterTransaction() {};
  };

  class SarDiameterTransaction : public DiameterTransaction<ServerAssignmentAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using DiameterTransaction::DiameterTransaction;

    virtual ServerAssignmentAnswer create_answer(Diameter::Message& rsp) override;
    virtual ~SarDiameterTransaction() {};
  };
};

void configure_cx_results_tables(SNMP::CxCounterTable* mar_results_table,
                                 SNMP::CxCounterTable* sar_results_table,
                                 SNMP::CxCounterTable* uar_results_table,
                                 SNMP::CxCounterTable* lir_results_table);

}; // namespace HssConnection
#endif
