/**
 * @file mockhttpconnection.h Mock HTTP connection.
 *
 * Copyright (C) Metaswitch Networks 2014
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKHTTPCONNECTION_H__
#define MOCKHTTPCONNECTION_H__

#include "gmock/gmock.h"
#include "httpconnection.h"

class MockHttpConnection : public HttpConnection
{
public:
  MockHttpConnection(HttpResolver* resolver) :
    HttpConnection("", false, resolver, SASEvent::HttpLogLevel::PROTOCOL, NULL)
  {};
  virtual ~MockHttpConnection() {};

  MOCK_METHOD3(send_delete, long(const std::string& path, SAS::TrailId trail, const std::string& body));
  MOCK_METHOD3(send_put, long(const std::string& path, const std::string& body, SAS::TrailId trail));

};

#endif
