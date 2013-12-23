/**
 * @file cx.h class definition wrapping Cx
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#ifndef CX_H__
#define CX_H__

#include "diameterstack.h"
#include "authvector.h"
#include "servercapabilities.h"

namespace Cx
{
class Dictionary : public Diameter::Dictionary
{
public:
  Dictionary();
  const Diameter::Dictionary::Vendor TGPP;
  const Diameter::Dictionary::Application CX;
  const Diameter::Dictionary::Message USER_AUTHORIZATION_REQUEST;
  const Diameter::Dictionary::Message USER_AUTHORIZATION_ANSWER;
  const Diameter::Dictionary::Message LOCATION_INFO_REQUEST;
  const Diameter::Dictionary::Message LOCATION_INFO_ANSWER;
  const Diameter::Dictionary::Message MULTIMEDIA_AUTH_REQUEST;
  const Diameter::Dictionary::Message MULTIMEDIA_AUTH_ANSWER;
  const Diameter::Dictionary::Message SERVER_ASSIGNMENT_REQUEST;
  const Diameter::Dictionary::Message SERVER_ASSIGNMENT_ANSWER;
  const Diameter::Dictionary::AVP PUBLIC_IDENTITY;
  const Diameter::Dictionary::AVP SIP_AUTH_DATA_ITEM;
  const Diameter::Dictionary::AVP SIP_AUTH_SCHEME;
  const Diameter::Dictionary::AVP SIP_AUTHORIZATION;
  const Diameter::Dictionary::AVP SIP_NUMBER_AUTH_ITEMS;
  const Diameter::Dictionary::AVP SERVER_NAME;
  const Diameter::Dictionary::AVP SIP_DIGEST_AUTHENTICATE;
  const Diameter::Dictionary::AVP CX_DIGEST_HA1;
  const Diameter::Dictionary::AVP CX_DIGEST_REALM;
  const Diameter::Dictionary::AVP VISITED_NETWORK_IDENTIFIER;
  const Diameter::Dictionary::AVP SERVER_CAPABILITIES;
  const Diameter::Dictionary::AVP MANDATORY_CAPABILITY;
  const Diameter::Dictionary::AVP OPTIONAL_CAPABILITY;
  const Diameter::Dictionary::AVP SERVER_ASSIGNMENT_TYPE;
  const Diameter::Dictionary::AVP USER_AUTHORIZATION_TYPE;
  const Diameter::Dictionary::AVP ORIGINATING_REQUEST;
  const Diameter::Dictionary::AVP USER_DATA_ALREADY_AVAILABLE;
  const Diameter::Dictionary::AVP USER_DATA;
  const Diameter::Dictionary::AVP CX_DIGEST_QOP;
  const Diameter::Dictionary::AVP SIP_AUTHENTICATE;
  const Diameter::Dictionary::AVP CONFIDENTIALITY_KEY;
  const Diameter::Dictionary::AVP INTEGRITY_KEY;
};

class UserAuthorizationRequest : public Diameter::Message
{
public:
  UserAuthorizationRequest(const Dictionary* dict,
                           const std::string& dest_host,
                           const std::string& dest_realm,
                           const std::string& impi,
                           const std::string& impu,
                           const std::string& visited_network_identifier,
                           int user_authorization_type);
  inline UserAuthorizationRequest(Diameter::Message& msg) : Diameter::Message(msg) {};
};

class UserAuthorizationAnswer : public Diameter::Message
{
public:
  UserAuthorizationAnswer(const Dictionary* dict);
  inline UserAuthorizationAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  int result_code() const;
  int experimental_result_code() const;
  std::string server_name() const;
  ServerCapabilities server_capabilities() const;
};


class LocationInfoRequest : public Diameter::Message
{
public:
  LocationInfoRequest(const Dictionary* dict,
                      const std::string& dest_host,
                      const std::string& dest_realm,
                      int originating_request,
                      const std::string& impu,
                      int user_authorization_type);
  inline LocationInfoRequest(Diameter::Message& msg) : Diameter::Message(msg) {};
};

class LocationInfoAnswer : public Diameter::Message
{
public:
  LocationInfoAnswer(const Dictionary* dict);
  inline LocationInfoAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  int result_code() const;
  int experimental_result_code() const;
  std::string server_name() const;
  ServerCapabilities server_capabilities() const;
};

class MultimediaAuthRequest : public Diameter::Message
{
public:
  MultimediaAuthRequest(const Dictionary* dict,
                        const std::string& dest_realm,
                        const std::string& dest_host,
                        const std::string& impi,
                        const std::string& impu,
                        const std::string& server_name,
                        const std::string& sip_auth_scheme,
                        const std::string& sip_authorization = "");
  inline MultimediaAuthRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  std::string impi() const;
  std::string impu() const;
  std::string server_name() const;
  std::string sip_auth_scheme() const;
};

class MultimediaAuthAnswer : public Diameter::Message
{
public:
  MultimediaAuthAnswer(const Dictionary* dict,
                       int result_code);
  inline MultimediaAuthAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  int result_code() const;
  int experimental_result_code() const;
  std::string sip_auth_scheme() const;
  DigestAuthVector digest_auth_vector() const;
  AKAAuthVector aka_auth_vector() const;

private:
  static std::string hex(const uint8_t* data, size_t len);
  static std::string base64(const uint8_t* data, size_t len);
};

class ServerAssignmentRequest : public Diameter::Message
{
public:
  ServerAssignmentRequest(const Dictionary* dict,
                          const std::string& dest_host,
                          const std::string& dest_realm,
                          const std::string& impi,
                          const std::string& impu,
                          const std::string& server_name);
  inline ServerAssignmentRequest(Diameter::Message& msg) : Diameter::Message(msg) {};
};

class ServerAssignmentAnswer : public Diameter::Message
{
public:
  ServerAssignmentAnswer(const Dictionary* dict);
  inline ServerAssignmentAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  int result_code() const;
  int experimental_result_code() const;
  std::string user_data() const;
};
};

#endif
