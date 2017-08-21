/**
 * @file diameter_handlers.cpp Diameter handlers for homestead
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "diameter_handlers.h"
#include "homestead_xml_utils.h"
#include "servercapabilities.h"
#include "homesteadsasevent.h"
#include "snmp_cx_counter_table.h"
#include "log.h"
#include "boost/algorithm/string/join.hpp"

static SNMP::CxCounterTable* ppr_results_tbl;
static SNMP::CxCounterTable* rtr_results_tbl;

const std::string SIP_URI_PRE = "sip:";

void RegistrationTerminationTask::run()
{
  // Save off the deregistration reason and all private and public
  // identities on the request.
  _deregistration_reason = _rtr.deregistration_reason();
  std::string impi = _rtr.impi();
  _impis.push_back(impi);
  std::vector<std::string> associated_identities = _rtr.associated_identities();
  _impis.insert(_impis.end(), associated_identities.begin(), associated_identities.end());

  TRC_INFO("Received Registration-Termination request with dereg reason %d",
           _deregistration_reason);

  SAS::Event rtr_received(this->trail(), SASEvent::RTR_RECEIVED, 0);
  rtr_received.add_var_param(impi);
  rtr_received.add_static_param(associated_identities.size());
  SAS::report_event(rtr_received);

  // Create the cache success and failure callbacks
  irs_vector_success_callback success_cb =
    std::bind(&RegistrationTerminationTask::get_registration_sets_success, this, std::placeholders::_1);

  failure_callback failure_cb =
    std::bind(&RegistrationTerminationTask::get_registration_sets_failure, this, std::placeholders::_1);

  // Figure out which registration sets we're de-registering
  if ((_deregistration_reason != SERVER_CHANGE) &&
      (_deregistration_reason != NEW_SERVER_ASSIGNED))
  {
    // We only record the list of public identities on the request of the reason
    // isn't SERVER_CHANGE or NEW_SERVER_ASSIGNED (in those cases, we will
    // deregister all the IRSs for the provided IMPIs)
    _impus = _rtr.impus();
  }

  if ((_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                           (_deregistration_reason == REMOVE_SCSCF) ||
                           (_deregistration_reason == SERVER_CHANGE) ||
                           (_deregistration_reason == NEW_SERVER_ASSIGNED)))
  {
    // If we don't have a list of impus and the reason is allowed, we want to
    // deregister all of the IRSs for the provided list of IMPIs, so start by
    // getting all the IRSs for these IMPIs from the cache
    std::string impis_str = boost::algorithm::join(_impis, ", ");
    TRC_DEBUG("Looking up registration sets from the cache for IMPIs: %s", impis_str.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_IMPIS, 0);
    event.add_var_param(impis_str);
    SAS::report_event(event);

    _cfg->cache->get_implicit_registration_sets_for_impis(success_cb,
                                                          failure_cb,
                                                          _impis,
                                                          this->trail());
  }
  else if ((!_impus.empty()) && ((_deregistration_reason == PERMANENT_TERMINATION) ||
                                 (_deregistration_reason == REMOVE_SCSCF)))
  {
    // If we have a list of IMPUs to deregister and the reason is allowed, we
    // want to only deregister those specific impus, so start by getting all the
    // IRSs from the cache for those IMPUS
    std::string impus_str = boost::algorithm::join(_impus, ", ");
    TRC_DEBUG("Looking up registration sets from the cache for IMPUs %s", impus_str.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA, 0);
    event.add_var_param(impus_str);
    SAS::report_event(event);

    _cfg->cache->get_implicit_registration_sets_for_impus(success_cb,
                                                          failure_cb,
                                                          _impus,
                                                          this->trail());
  }
  else
  {
    // This is an invalid deregistration reason.
    TRC_ERROR("Registration-Termination request received with invalid deregistration reason %d",
              _deregistration_reason);
    SAS::Event event(this->trail(), SASEvent::INVALID_DEREG_REASON, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_FAILURE);
    delete this;
  }
}

void RegistrationTerminationTask::get_registration_sets_success(std::vector<ImplicitRegistrationSet*> reg_sets)
{
  // Save the vector of IRSs, which we are now responsible for deleting
  _reg_sets = reg_sets;

  if (_reg_sets.empty())
  {
    TRC_DEBUG("No registered IMPUs to deregister found");
    SAS::Event event(this->trail(), SASEvent::NO_IMPU_DEREG, 0);
    SAS::report_event(event);
    send_rta(DIAMETER_REQ_SUCCESS);
    delete this;
  }
  else
  {
    // We have some registration sets to delete
    HTTPCode ret_code = 0;
    std::vector<std::string> empty_vector;
    std::vector<std::string> default_public_identities;

    // Extract the default public identities from the registration sets.
    for (ImplicitRegistrationSet* reg_set : _reg_sets)
    {
      default_public_identities.push_back(reg_set->get_default_impu());
    }

    // We need to notify sprout of the deregistrations. What we send to sprout
    // depends on the deregistration reason.
    switch (_deregistration_reason)
    {
    case PERMANENT_TERMINATION:
      ret_code = _cfg->sprout_conn->deregister_bindings(false,
                                                        default_public_identities,
                                                        _impis,
                                                        this->trail());
      break;

    case REMOVE_SCSCF:
    case SERVER_CHANGE:
      ret_code = _cfg->sprout_conn->deregister_bindings(true,
                                                        default_public_identities,
                                                        empty_vector,
                                                        this->trail());
      break;

    case NEW_SERVER_ASSIGNED:
      ret_code = _cfg->sprout_conn->deregister_bindings(false,
                                                        default_public_identities,
                                                        empty_vector,
                                                        this->trail());
      break;

    default:
      // LCOV_EXCL_START - We can't get here because we've already filtered these out.
      TRC_ERROR("Unexpected deregistration reason %d on RTR", _deregistration_reason);
      break;
      // LCOV_EXCL_STOP
    }

    switch (ret_code)
    {
      case HTTP_OK:
      {
        TRC_DEBUG("Send Registration-Termination answer indicating success");
        SAS::Event event(this->trail(), SASEvent::DEREG_SUCCESS, 0);
        SAS::report_event(event);
        send_rta(DIAMETER_REQ_SUCCESS);
      }
      break;

      case HTTP_BADMETHOD:
      case HTTP_BAD_REQUEST:
      case HTTP_SERVER_ERROR:
      {
        TRC_DEBUG("Send Registration-Termination answer indicating failure");
        SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
        SAS::report_event(event);
        send_rta(DIAMETER_REQ_FAILURE);
      }
      break;

      default:
      {
        TRC_ERROR("Unexpected HTTP return code, send Registration-Termination answer indicating failure");
        SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
        SAS::report_event(event);
        send_rta(DIAMETER_REQ_FAILURE);
      }
      break;
    }

    // Now delete our cached registration sets
    SAS::Event event(this->trail(), SASEvent::CACHE_DELETE_REG_DATA, 0);
    std::string impus_str = boost::algorithm::join(default_public_identities, ", ");
    event.add_var_param(impus_str);
    SAS::report_event(event);

    void_success_cb success_cb =
      std::bind(&RegistrationTerminationTask::delete_reg_sets_success, this);

    failure_callback failure_cb =
      std::bind(&RegistrationTerminationTask::delete_reg_sets_failure, this, std::placeholders::_1);

    _cfg->cache->delete_implicit_registration_sets(success_cb, failure_cb, _reg_sets, this->trail());
  }
}

void RegistrationTerminationTask::get_registration_sets_failure(Store::Status rc)
{
  TRC_DEBUG("Failed to get a registration set - report failure to HSS");
  SAS::Event event(this->trail(), SASEvent::DEREG_FAIL, 0);
  SAS::report_event(event);
  send_rta(DIAMETER_REQ_FAILURE);
  delete this;
}

// These two callbacks are used so that we don't delete the task until we're
// completely done with Cache operations.
// This allows us to delete each of the ImplicitRegistrationSets in the
// _reg_sets vector in the task's destructor.
void RegistrationTerminationTask::delete_reg_sets_success()
{
  // We have already sent the reponse, so we do nothing here
  delete this;
}

// LCOV_EXCL_START - nothing interesting to UT.
void RegistrationTerminationTask::delete_reg_sets_failure(Store::Status rc)
{
  // We have already sent the reponse, so we do nothing here
  delete this;
}
// LCOV_EXCL_STOP

void RegistrationTerminationTask::send_rta(const std::string result_code)
{
  // Use our Cx layer to create a RTA object and add the correct AVPs. The RTA is
  // created from the RTR.
  Cx::RegistrationTerminationAnswer rta(_rtr,
                                        _cfg->dict,
                                        result_code,
                                        _msg.auth_session_state(),
                                        _impis);

  if (result_code == DIAMETER_REQ_SUCCESS)
  {
    rtr_results_tbl->increment(SNMP::DiameterAppId::BASE, 2001);
  }
  else if (result_code == DIAMETER_REQ_FAILURE)
  {
    rtr_results_tbl->increment(SNMP::DiameterAppId::BASE, 5012);
  }

  // Send the RTA back to the HSS.
  TRC_INFO("Ready to send RTA");
  rta.send(this->trail());
}

void PushProfileTask::run()
{
  SAS::Event ppr_received(trail(), SASEvent::PPR_RECEIVED, 0);
  SAS::report_event(ppr_received);

  // Received a Push Profile Request. We may need to update an IMS
  // subscription and/or charging address information in the cache
  _ims_sub_present = _ppr.user_data(_ims_subscription);
  _charging_addrs_present = _ppr.charging_addrs(_charging_addrs);
  _impi = _ppr.impi();

  if ((!_charging_addrs_present) && (!_ims_sub_present))
  {
    // If we have no charging addresses or IMS subscription, no actions need to
    // be taken, so send a PPA saying the PPR was successfully handled.
    send_ppa(DIAMETER_REQ_SUCCESS);
    delete this;
  }
  else
  {
    // Otherwise, we need to get the specified IMPI's entire subscription from
    // the cache
    TRC_DEBUG("Looking up registration sets from the cache for IMPI: %s", _impi.c_str());
    SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_IMPIS, 0);
    event.add_var_param(_impi);
    SAS::report_event(event);

    ims_sub_success_cb success_cb =
      std::bind(&PushProfileTask::on_get_ims_sub_success, this, std::placeholders::_1);

    failure_callback failure_cb =
      std::bind(&PushProfileTask::on_get_ims_sub_failure, this, std::placeholders::_1);

    _cfg->cache->get_ims_subscription(success_cb, failure_cb, _impi, this->trail());
  }
}

void PushProfileTask::on_get_ims_sub_success(ImsSubscription* ims_sub)
{
  // Take ownership of the ImsSubscription*
  _ims_sub = ims_sub;

  // Build up a SAS log as we go, that we'll only send if we decide we can
  // process the PPR correctly
  SAS::Event put_cache_event(this->trail(), SASEvent::CACHE_PUT_REG_DATA_IMPI, 0);
  put_cache_event.add_var_param(_impi);

  std::string new_default_id;

  // If we have IMS Subscription XML on the PPR, then we need to verify that
  // it's not going to change the default impu for that IRS
  if (_ims_sub_present)
  {
    XmlUtils::get_default_id(_ims_subscription, new_default_id);

    ImplicitRegistrationSet* irs = _ims_sub->get_irs_for_default_impu(new_default_id);
    if (!irs)
    {
      TRC_INFO("The default id of the PPR doesn't match a default id already "
               "known be belong to the IMPI %s - reject the PPR", _impi.c_str());
      SAS::Event event(this->trail(), SASEvent::PPR_CHANGE_DEFAULT_IMPU, 0);
      event.add_var_param(_impi);
      event.add_var_param(new_default_id);
      SAS::report_event(event);
      send_ppa(DIAMETER_REQ_FAILURE);

      delete this;
      return;
    }

    // If we've got here, the PPR is allowed. We should now check that the IRS
    // from the PPR contains a SIP URI and throw an error log if it doesn't,
    // although we continue as normal even if it doesn't.
    _impus = XmlUtils::get_public_ids(_ims_subscription);
    bool found_sip_uri = false;

    for (std::vector<std::string>::iterator it = _impus.begin();
        (it != _impus.end()) && (!found_sip_uri);
        ++it)
    {
      if ((*it).compare(0, SIP_URI_PRE.length(), SIP_URI_PRE) == 0)
      {
        found_sip_uri = true;
        break;
      }
    }

    if (!found_sip_uri)
    {
      TRC_ERROR("No SIP URI in Implicit Registration Set");
      SAS::Event event(this->trail(), SASEvent::NO_SIP_URI_IN_IRS, 0);
      event.add_compressed_param(_ims_subscription, &SASEvent::PROFILE_SERVICE_PROFILE);
      SAS::report_event(event);
    }

    // We can now update the IRS with the new XML. We will handle updating
    // charging addresses later.
    // Note - we don't update the TTL for the data, since we only do that on
    // (re)-registration
    irs->set_ims_sub_xml(_ims_subscription);
  }

  if (_ims_sub_present)
  {
    // Add the impu and XMl to the SAS event
    put_cache_event.add_var_param(new_default_id);
    put_cache_event.add_compressed_param(_ims_subscription, &SASEvent::PROFILE_SERVICE_PROFILE);
  }
  else
  {
    // Put an empty string for the impu, and note that the XML is unchanged
    put_cache_event.add_var_param("");
    put_cache_event.add_compressed_param("IMS subscription unchanged", &SASEvent::PROFILE_SERVICE_PROFILE);
  }

  // We now may have to update the charging addresses
  if (_charging_addrs_present)
  {
    _ims_sub->set_charging_addrs(_charging_addrs);
    put_cache_event.add_var_param(_charging_addrs.log_string());
  }
  else
  {
    put_cache_event.add_var_param("Charging addresses unchanged");
  }

  SAS::report_event(put_cache_event);

  // Now, save the updated ImsSubscription in the cache.
  // The cache is smart enough to not write any IRSs which haven't been touched
  // by this PPR
  void_success_cb success_cb =
    std::bind(&PushProfileTask::on_save_ims_sub_success, this);

  failure_callback failure_cb =
    std::bind(&PushProfileTask::on_save_ims_sub_failure, this, std::placeholders::_1);

  _cfg->cache->put_ims_subscription(success_cb, failure_cb, _ims_sub, this->trail());
}

void PushProfileTask::on_get_ims_sub_failure(Store::Status rc)
{
  SAS::Event event(this->trail(), SASEvent::CACHE_GET_REG_DATA_FAIL, 0);
  SAS::report_event(event);
  TRC_DEBUG("Failed to get IMS subscription from cache - report to HSS");
  send_ppa(DIAMETER_REQ_FAILURE);
  delete this;
}

void PushProfileTask::on_save_ims_sub_success()
{
  SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA_SUCCESS, 0);
  SAS::report_event(event);
  send_ppa(DIAMETER_REQ_SUCCESS);
  delete this;
}

void PushProfileTask::on_save_ims_sub_failure(Store::Status rc)
{
  TRC_DEBUG("Failed to update registration data - report failure to HSS");
  SAS::Event event(this->trail(), SASEvent::CACHE_PUT_REG_DATA_FAIL, 0);
  event.add_static_param(rc);
  SAS::report_event(event);
  send_ppa(DIAMETER_REQ_FAILURE);
  delete this;
}

void PushProfileTask::send_ppa(const std::string result_code)
{
  // Use our Cx layer to create a PPA object and add the correct AVPs. The PPA is
  // created from the PPR.
  Cx::PushProfileAnswer ppa(_ppr,
                            _cfg->dict,
                            result_code,
                            _msg.auth_session_state());

  if (result_code == DIAMETER_REQ_SUCCESS)
  {
    ppr_results_tbl->increment(SNMP::DiameterAppId::BASE, 2001);
  }
  else if (result_code == DIAMETER_REQ_FAILURE)
  {
    ppr_results_tbl->increment(SNMP::DiameterAppId::BASE, 5012);
  }

  // Send the PPA back to the HSS.
  TRC_INFO("Ready to send PPA");
  ppa.send(this->trail());
}

void configure_handler_cx_results_tables(SNMP::CxCounterTable* ppr_results_table,
                                         SNMP::CxCounterTable* rtr_results_table)
{
  ppr_results_tbl = ppr_results_table;
  rtr_results_tbl = rtr_results_table;
}
