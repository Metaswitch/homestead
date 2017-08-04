/**
 * @file mockhssconnection.h Fake HSS Connection.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKHSSCONNECTION_H__
#define MOCKHSSCONNECTION_H__

#include "hss_connection.h"
#include "gmock/gmock.h"

class MockHssConnection : public ::HssConnection::HssConnection
{
public:
  MockHssConnection() {};
  virtual ~MockHssConnection() {};

  MOCK_METHOD2(send_multimedia_auth_request,
               void(::HssConnection::maa_cb cb, ::HssConnection::MultimediaAuthRequest req));

  MOCK_METHOD2(send_user_auth_request,
               void(::HssConnection::uaa_cb cb, ::HssConnection::UserAuthRequest req));

  MOCK_METHOD2(send_location_info_request,
               void(::HssConnection::lia_cb cb, ::HssConnection::LocationInfoRequest req));

  MOCK_METHOD2(send_server_assignment_request,
               void(::HssConnection::saa_cb cb, ::HssConnection::ServerAssignmentRequest req));
};

#endif
