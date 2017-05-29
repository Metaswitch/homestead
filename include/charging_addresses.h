/**
 * @file charging_addresses.h A class containing a subscriber's charging
 * addresses.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef CHARGING_ADDRESSES_H__
#define CHARGING_ADDRESSES_H__

#include <string>
#include <deque>

/// An object containing a subscriber's charging addresses.
class ChargingAddresses
{
public:
  /// Default constructor.
  inline ChargingAddresses() {}

  /// Constructor which takes CCFs and ECFs.
  inline ChargingAddresses(std::deque<std::string> ccfs,
                           std::deque<std::string> ecfs) : ccfs(ccfs), ecfs(ecfs) {}


  /// Double ended queues of collect charging function addresses and event
  /// charging function addresses. These are stored in priority order, and
  /// they are stored in the format given by the provisioning server
  /// (normally the HSS).
  std::deque<std::string> ccfs;
  std::deque<std::string> ecfs;

  /// Helper function to determine whether we have any charging addresses.
  inline bool empty() const { return (ccfs.empty()) && (ecfs.empty()); }

  /// Convert the charging functions into a string to display in logs
  std::string log_string()
  {
    std::string log_str;

    if (!ccfs.empty())
    {
      log_str.append("Primary CCF: ").append(ccfs[0]);

      if (ccfs.size() > 1)
      {
        log_str.append(", Secondary CCF: ").append(ccfs[1]);
      }
    }
    if (!ecfs.empty())
    {
      if (!ccfs.empty())
      {
        log_str.append(", ");
      }

      log_str.append("Primary ECF: ").append(ecfs[0]);

      if (ecfs.size() > 1)
      {
        log_str.append(", Secondary ECF: ").append(ecfs[1]);
      }
    }

    return log_str;
  }
};

#endif
