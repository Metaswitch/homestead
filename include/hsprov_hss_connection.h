/**
 * @file hsprov_hss_connection.h Implementation of HssConnection that uses HSProv
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSPROV_HSS_CONNECTION_H__
#define HSPROV_HSS_CONNECTION_H__

#include <string>
#include "hss_connection.h"
#include "hsprov_store.h"

namespace HssConnection {

class HsProvHssConnection : public HssConnection
{
public:
  virtual ~HsProvHssConnection() {};

  // The Store is passed in the constructor so that we can mock it out in UTs
  HsProvHssConnection(StatisticsManager* stats, HsProvStore* store, std::string server_name);

  // Send a multimedia auth request to the HSS
  virtual void send_multimedia_auth_request(maa_cb callback,
                                            MultimediaAuthRequest request,
                                            SAS::TrailId trail,
                                            Utils::StopWatch* stopwatch) override;

  // Send a user auth request to the HSS
  virtual void send_user_auth_request(uaa_cb callback,
                                      UserAuthRequest request,
                                      SAS::TrailId trail,
                                      Utils::StopWatch* stopwatch) override;

  // Send a location info request to the HSS
  virtual void send_location_info_request(lia_cb callback,
                                          LocationInfoRequest request,
                                          SAS::TrailId trail,
                                          Utils::StopWatch* stopwatch) override;

  // Send a server assignment request to the HSS
  virtual void send_server_assignment_request(saa_cb callback,
                                              ServerAssignmentRequest request,
                                              SAS::TrailId trail,
                                              Utils::StopWatch* stopwatch) override;

  template <class AnswerType>
  class HsProvTransaction : public CassandraStore::Transaction
  {
  public:
    typedef std::function<void(const AnswerType&)> callback_t;

    HsProvTransaction(SAS::TrailId trail,
                      callback_t callback,
                      StatisticsManager* stats_manager) :
      CassandraStore::Transaction(trail),
      _response_clbk(callback),
      _stats_manager(stats_manager)
    {};

    virtual ~HsProvTransaction() {};

  protected:
    callback_t _response_clbk;
    StatisticsManager* _stats_manager;

    // Implementations will use these to create the correct answer
    virtual AnswerType create_answer(CassandraStore::Operation* op) = 0;
    void on_response(CassandraStore::Operation* op);

    void on_success(CassandraStore::Operation* op)
    {
      on_response(op);
    }

    void on_failure(CassandraStore::Operation* op)
    {
      on_response(op);
    }

  private:
    void update_latency_stats();
  };

  class MarHsProvTransaction : public HsProvTransaction<MultimediaAuthAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using HsProvTransaction::HsProvTransaction;

    virtual MultimediaAuthAnswer create_answer(CassandraStore::Operation* op) override;
    virtual ~MarHsProvTransaction() {};
  };

  class LirHsProvTransaction : public HsProvTransaction<LocationInfoAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using HsProvTransaction::HsProvTransaction;

    virtual LocationInfoAnswer create_answer(CassandraStore::Operation* op) override;
    virtual ~LirHsProvTransaction() {};
  };

  class SarHsProvTransaction : public HsProvTransaction<ServerAssignmentAnswer>
  {
  public:
    // Inherit the superclass' constructor
    using HsProvTransaction::HsProvTransaction;

    virtual ServerAssignmentAnswer create_answer(CassandraStore::Operation* op) override;
    virtual ~SarHsProvTransaction() {};
  };

private:
  HsProvStore* _store;
  static std::string _configured_server_name;
};
}; // namespace HssConnection
#endif
