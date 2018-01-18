/**
 * @file mock_sproutconnection.h
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_SPROUTCONNECTION_H__
#define MOCK_SPROUTCONNECTION_H__

#include "gmock/gmock.h"
#include "sproutconnection.h"

class MockSproutConnection : public SproutConnection
{
public:
  MockSproutConnection();
  virtual ~MockSproutConnection();

  MOCK_METHOD4(deregister_bindings, HTTPCode(const bool& send_notifications,
                                             const std::vector<std::string>& default_public_ids,
                                             const std::vector<std::string>& impis,
                                             SAS::TrailId trail));

  MOCK_METHOD3(change_associated_identities, HTTPCode(const std::string& default_id,
                                                      const std::string& user_data_xml,
                                                      SAS::TrailId trail));
};

#endif
