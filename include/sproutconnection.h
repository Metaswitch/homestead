/**
 * @file sproutconnection.h 
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef SPROUTCONNECTION_H__
#define SPROUTCONNECTION_H__

#include "httpconnection.h"

class SproutConnection
{
public:
  SproutConnection(HttpConnection *http);
  virtual ~SproutConnection();

  virtual HTTPCode deregister_bindings(const bool& send_notifications,
                                       const std::vector<std::string>& default_public_ids,
                                       const std::vector<std::string>& impis,
                                       SAS::TrailId trail);

  // JSON string constants
  static const std::string JSON_REGISTRATIONS;
  static const std::string JSON_PRIMARY_IMPU;
  static const std::string JSON_IMPI;
 
private:
  std::string create_body(const std::vector<std::string>& default_public_ids,
                          const std::vector<std::string>& impis);

  HttpConnection* _http;
};
#endif
