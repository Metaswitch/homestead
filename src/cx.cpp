/**
 * @file cx.cpp class definition wrapping Cx
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

#include "cx.h"

#include "log.h"

#include <string>
#include <sstream>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

using namespace Cx;

Dictionary::Dictionary() :
  TGPP("3GPP"),
  TGPP2("3GPP2"),
  CX("Cx"),
  USER_AUTHORIZATION_REQUEST("3GPP/User-Authorization-Request"),
  USER_AUTHORIZATION_ANSWER("3GPP/User-Authorization-Answer"),
  LOCATION_INFO_REQUEST("3GPP/Location-Info-Request"),
  LOCATION_INFO_ANSWER("3GPP/Location-Info-Answer"),
  MULTIMEDIA_AUTH_REQUEST("3GPP/Multimedia-Auth-Request"),
  MULTIMEDIA_AUTH_ANSWER("3GPP/Multimedia-Auth-Answer"),
  SERVER_ASSIGNMENT_REQUEST("3GPP/Server-Assignment-Request"),
  SERVER_ASSIGNMENT_ANSWER("3GPP/Server-Assignment-Answer"),
  REGISTRATION_TERMINATION_REQUEST("3GPP/Registration-Termination-Request"),
  REGISTRATION_TERMINATION_ANSWER("3GPP/Registration-Termination-Answer"),
  PUSH_PROFILE_REQUEST("3GPP/Push-Profile-Request"),
  PUSH_PROFILE_ANSWER("3GPP/Push-Profile-Answer"),
  PUBLIC_IDENTITY("3GPP", "Public-Identity"),
  SIP_AUTH_DATA_ITEM("3GPP", "SIP-Auth-Data-Item"),
  SIP_AUTH_SCHEME("3GPP", "SIP-Authentication-Scheme"),
  SIP_AUTHORIZATION("3GPP", "SIP-Authorization"),
  SIP_NUMBER_AUTH_ITEMS("3GPP", "SIP-Number-Auth-Items"),
  SERVER_NAME("3GPP", "Server-Name"),
  SIP_DIGEST_AUTHENTICATE("3GPP", "SIP-Digest-Authenticate"),
  CX_DIGEST_HA1("3GPP", "Digest-HA1"),
  CX_DIGEST_REALM("3GPP", "Digest-Realm"),
  VISITED_NETWORK_IDENTIFIER("3GPP", "Visited-Network-Identifier"),
  SERVER_CAPABILITIES("3GPP", "Server-Capabilities"),
  MANDATORY_CAPABILITY("3GPP", "Mandatory-Capability"),
  OPTIONAL_CAPABILITY("3GPP", "Optional-Capability"),
  SERVER_ASSIGNMENT_TYPE("3GPP", "Server-Assignment-Type"),
  USER_AUTHORIZATION_TYPE("3GPP", "User-Authorization-Type"),
  ORIGINATING_REQUEST("3GPP", "Originating-Request"),
  USER_DATA_ALREADY_AVAILABLE("3GPP", "User-Data-Already-Available"),
  USER_DATA("3GPP", "User-Data"),
  CX_DIGEST_QOP("3GPP", "Digest-QoP"),
  SIP_AUTHENTICATE("3GPP", "SIP-Authenticate"),
  CONFIDENTIALITY_KEY("3GPP", "Confidentiality-Key"),
  INTEGRITY_KEY("3GPP", "Integrity-Key"),
  ASSOCIATED_IDENTITIES("3GPP", "Associated-Identities"),
  DEREGISTRATION_REASON("3GPP", "Deregistration-Reason"),
  IDENTITY_WITH_EMERGENCY_REGISTRATION("3GPP", "Identity-with-Emergency-Registration"),
  CHARGING_INFORMATION("3GPP", "Charging-Information")
{
}

UserAuthorizationRequest::UserAuthorizationRequest(const Dictionary* dict,
                                                   Diameter::Stack* stack,
                                                   const std::string& dest_host,
                                                   const std::string& dest_realm,
                                                   const std::string& impi,
                                                   const std::string& impu,
                                                   const std::string& visited_network_identifier,
                                                   const std::string& authorization_type) :
                                                   Diameter::Message(dict, dict->USER_AUTHORIZATION_REQUEST, stack)
{
  LOG_DEBUG("Building User-Authorization request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_vendor_spec_app_id();  
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));
  add(Diameter::AVP(dict->VISITED_NETWORK_IDENTIFIER).val_str(visited_network_identifier));

  // USER_AUTHORIZATION_TYPE AVP is an enumeration. These values are as per 3GPP TS 29.229.
  // Default is 0 (REGISTATION).
  if (authorization_type == "DEREG")
  {
    add(Diameter::AVP(dict->USER_AUTHORIZATION_TYPE).val_i32(1));
  }
  else if (authorization_type == "CAPAB")
  {
    add(Diameter::AVP(dict->USER_AUTHORIZATION_TYPE).val_i32(2));
  }
  else
  {
    add(Diameter::AVP(dict->USER_AUTHORIZATION_TYPE).val_i32(0));
  }
}

UserAuthorizationAnswer::UserAuthorizationAnswer(const Dictionary* dict,
                                                 Diameter::Stack* stack,
                                                 const int32_t& result_code,
                                                 const int32_t& experimental_result_code,
                                                 const std::string& server_name,
                                                 const ServerCapabilities& capabs) :
                                                 Diameter::Message(dict, dict->USER_AUTHORIZATION_ANSWER, stack)
{
  LOG_DEBUG("Building User-Authorization answer");

  // This method creates a UAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    add(experimental_result);
  }

  if (!server_name.empty())
  {
    add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
  }

  Diameter::AVP server_capabilities(dict->SERVER_CAPABILITIES);
  if (!capabs.mandatory_capabilities.empty())
  {
    for (std::vector<int32_t>::const_iterator it = capabs.mandatory_capabilities.begin();
        it != capabs.mandatory_capabilities.end();
        ++it)
    {
      server_capabilities.add(Diameter::AVP(dict->MANDATORY_CAPABILITY).val_i32(*it));
    }
  }
  if (!capabs.optional_capabilities.empty())
  {
    for (std::vector<int32_t>::const_iterator it = capabs.optional_capabilities.begin();
         it != capabs.optional_capabilities.end();
         ++it)
    {
      server_capabilities.add(Diameter::AVP(dict->OPTIONAL_CAPABILITY).val_i32(*it));
    }
  }
  add(server_capabilities);
}

ServerCapabilities UserAuthorizationAnswer::server_capabilities() const
{
  ServerCapabilities server_capabilities({}, {});

  // Server capabilities are grouped into mandatory capabilities and optional capabilities
  // underneath the SERVER_CAPABILITIES AVP.
  LOG_DEBUG("Getting server capabilties from User-Authorization answer");
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SERVER_CAPABILITIES);
  if (avps != end())
  {
    Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->MANDATORY_CAPABILITY);
    while (avps2 != end())
    {
      LOG_DEBUG("Found mandatory capability %d", avps2->val_i32());
      server_capabilities.mandatory_capabilities.push_back(avps2->val_i32());
      avps2++;
    }
    avps2 = avps->begin(((Cx::Dictionary*)dict())->OPTIONAL_CAPABILITY);
    while (avps2 != end())
    {
      LOG_DEBUG("Found optional capability %d", avps2->val_i32());
      server_capabilities.optional_capabilities.push_back(avps2->val_i32());
      avps2++;
    }
  }
  return server_capabilities;
}

LocationInfoRequest::LocationInfoRequest(const Dictionary* dict,
                                         Diameter::Stack* stack,
                                         const std::string& dest_host,
                                         const std::string& dest_realm,
                                         const std::string& originating_request,
                                         const std::string& impu,
                                         const std::string& authorization_type) :
                                         Diameter::Message(dict, dict->LOCATION_INFO_REQUEST, stack)
{
  LOG_DEBUG("Building Location-Info request for %s", impu.c_str());
  add_new_session_id();
  add_vendor_spec_app_id();
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));

  // Only add the ORIGINATING_REQUEST AVP if we are originating. This AVP is an
  // enumeration. 0 corresponds to ORIGINATING.
  if (originating_request == "true")
  {
    add(Diameter::AVP(dict->ORIGINATING_REQUEST).val_i32(0));
  }
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));

  // Only add the USER_AUTHORIZATION_TYPE AVP if we require capability information.
  // This AVP is an enumeration. 2 corresponds to REGISTRATION_AND_CAPABILITIES.
  if (authorization_type == "CAPAB")
  {
    add(Diameter::AVP(dict->USER_AUTHORIZATION_TYPE).val_i32(2));
  }
}

LocationInfoAnswer::LocationInfoAnswer(const Dictionary* dict,
                                       Diameter::Stack* stack,
                                       const int32_t& result_code,
                                       const int32_t& experimental_result_code,
                                       const std::string& server_name,
                                       const ServerCapabilities& capabs) :
                                       Diameter::Message(dict, dict->USER_AUTHORIZATION_ANSWER, stack)
{
  LOG_DEBUG("Building Location-Info answer");

  // This method creates an LIA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    add(experimental_result);
  }

  if (!server_name.empty())
  {
    add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
  }

  Diameter::AVP server_capabilities(dict->SERVER_CAPABILITIES);
  if (!capabs.mandatory_capabilities.empty())
  {
    for (std::vector<int32_t>::const_iterator it = capabs.mandatory_capabilities.begin();
        it != capabs.mandatory_capabilities.end();
        ++it)
    {
      server_capabilities.add(Diameter::AVP(dict->MANDATORY_CAPABILITY).val_i32(*it));
    }
  }
  if (!capabs.optional_capabilities.empty())
  {
    for (std::vector<int32_t>::const_iterator it = capabs.optional_capabilities.begin();
        it != capabs.optional_capabilities.end();
        ++it)
    {
      server_capabilities.add(Diameter::AVP(dict->OPTIONAL_CAPABILITY).val_i32(*it));
    }
  }
  add(server_capabilities);
}

ServerCapabilities LocationInfoAnswer::server_capabilities() const
{
  ServerCapabilities server_capabilities({}, {});

  // Server capabilities are grouped into mandatory capabilities and optional capabilities
  // underneath the SERVER_CAPABILITIES AVP.
  LOG_DEBUG("Getting server capabilties from Location-Info answer");
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SERVER_CAPABILITIES);
  if (avps != end())
  {
    Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->MANDATORY_CAPABILITY);
    while (avps2 != end())
    {
      LOG_DEBUG("Found mandatory capability %d", avps2->val_i32());
      server_capabilities.mandatory_capabilities.push_back(avps2->val_i32());
      avps2++;
    }
    avps2 = avps->begin(((Cx::Dictionary*)dict())->OPTIONAL_CAPABILITY);
    while (avps2 != end())
    {
      LOG_DEBUG("Found optional capability %d", avps2->val_i32());
      server_capabilities.optional_capabilities.push_back(avps2->val_i32());
      avps2++;
    }
  }
  return server_capabilities;
}

MultimediaAuthRequest::MultimediaAuthRequest(const Dictionary* dict,
                                             Diameter::Stack* stack,
                                             const std::string& dest_realm,
                                             const std::string& dest_host,
                                             const std::string& impi,
                                             const std::string& impu,
                                             const std::string& server_name,
                                             const std::string& sip_auth_scheme,
                                             const std::string& sip_authorization) :
                                             Diameter::Message(dict, dict->MULTIMEDIA_AUTH_REQUEST, stack)
{
  LOG_DEBUG("Building Multimedia-Auth request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_vendor_spec_app_id();
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  add_origin();
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));
  Diameter::AVP sip_auth_data_item(dict->SIP_AUTH_DATA_ITEM);
  sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTH_SCHEME).val_str(sip_auth_scheme));
  if (!sip_authorization.empty())
  {
    LOG_DEBUG("Specifying SIP-Authorization %s", sip_authorization.c_str());
    sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTHORIZATION).val_str(sip_authorization));
  }
  add(sip_auth_data_item);
  add(Diameter::AVP(dict->SIP_NUMBER_AUTH_ITEMS).val_i32(1));
  add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
}

std::string MultimediaAuthRequest::sip_auth_scheme() const
{
  std::string sip_auth_scheme;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTH_SCHEME);
    if (avps != end())
    {
      sip_auth_scheme = avps->val_str();
      LOG_DEBUG("Got SIP-Auth-Scheme %s", sip_auth_scheme.c_str());
    }
  }
  return sip_auth_scheme;
}

std::string MultimediaAuthRequest::sip_authorization() const
{
  std::string sip_authorization;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTHORIZATION);
    if (avps != end())
    {
      sip_authorization = avps->val_str();
      LOG_DEBUG("Got SIP-Authorization %s", sip_authorization.c_str());
    }
  }
  return sip_authorization;
}

MultimediaAuthAnswer::MultimediaAuthAnswer(const Dictionary* dict,
                                           Diameter::Stack* stack,
                                           const int32_t& result_code,
                                           const std::string& scheme,
                                           const DigestAuthVector& digest_av,
                                           const AKAAuthVector& aka_av) :
                                           Diameter::Message(dict, dict->MULTIMEDIA_AUTH_ANSWER, stack)
{
  LOG_DEBUG("Building Multimedia-Authorization answer");

  // This method creates an MAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  Diameter::AVP sip_auth_data_item(dict->SIP_AUTH_DATA_ITEM);
  sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTH_SCHEME).val_str(scheme));
  Diameter::AVP sip_digest_authenticate(dict->SIP_DIGEST_AUTHENTICATE);
  sip_digest_authenticate.add(Diameter::AVP(dict->CX_DIGEST_HA1).val_str(digest_av.ha1));
  sip_digest_authenticate.add(Diameter::AVP(dict->CX_DIGEST_REALM).val_str(digest_av.realm));
  sip_digest_authenticate.add(Diameter::AVP(dict->CX_DIGEST_QOP).val_str(digest_av.qop));
  sip_auth_data_item.add(sip_digest_authenticate);
  sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTHENTICATE).val_str(aka_av.challenge));
  sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTHORIZATION).val_str(aka_av.response));
  sip_auth_data_item.add(Diameter::AVP(dict->CONFIDENTIALITY_KEY).val_str(aka_av.crypt_key));
  sip_auth_data_item.add(Diameter::AVP(dict->INTEGRITY_KEY).val_str(aka_av.integrity_key));
  add(sip_auth_data_item);
}

std::string MultimediaAuthAnswer::sip_auth_scheme() const
{
  std::string sip_auth_scheme;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTH_SCHEME);
    if (avps != end())
    {
      sip_auth_scheme = avps->val_str();
      LOG_DEBUG("Got SIP-Auth-Scheme %s", sip_auth_scheme.c_str());
    }
  }
  return sip_auth_scheme;
}

DigestAuthVector MultimediaAuthAnswer::digest_auth_vector() const
{
  LOG_DEBUG("Getting digest authentication vector from Multimedia-Auth answer");
  DigestAuthVector digest_auth_vector;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_DIGEST_AUTHENTICATE);
    if (avps != end())
    {
      // Look for the digest.
      Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_HA1);
      if (avps2 != end())
      {
        digest_auth_vector.ha1 = avps2->val_str();
        LOG_DEBUG("Found Digest-HA1 %s", digest_auth_vector.ha1.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-HA1.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_HA1);
        if (avps2 != end())
        {
          digest_auth_vector.ha1 = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-HA1 %s", digest_auth_vector.ha1.c_str());
        }
      }
      // Look for the realm.
      avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_REALM);
      if (avps2 != end())
      {
        digest_auth_vector.realm = avps2->val_str();
        LOG_DEBUG("Found Digest-Realm %s", digest_auth_vector.realm.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-Realm.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_REALM);
        if (avps2 != end())
        {
          digest_auth_vector.realm = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-Realm %s", digest_auth_vector.realm.c_str());
        }
      }
      // Look for the QoP.
      avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_QOP);
      if (avps2 != end())
      {
        digest_auth_vector.qop = avps2->val_str();
        LOG_DEBUG("Found Digest-QoP %s", digest_auth_vector.qop.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-QoP.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_QOP);
        if (avps2 != end())
        {
          digest_auth_vector.qop = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-QoP %s", digest_auth_vector.qop.c_str());
        }
      }
    }
  }
  return digest_auth_vector;
}

AKAAuthVector MultimediaAuthAnswer::aka_auth_vector() const
{
  LOG_DEBUG("Getting AKA authentication vector from Multimedia-Auth answer");
  AKAAuthVector aka_auth_vector;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    // Look for the challenge.
    Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTHENTICATE);
    if (avps2 != end())
    {
      size_t len;
      const uint8_t* data = avps2->val_os(len);
      aka_auth_vector.challenge = base64(data, len);
      LOG_DEBUG("Found SIP-Authenticate (challenge) %s", aka_auth_vector.challenge.c_str());
    }
    // Look for the response.
    avps2 = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTHORIZATION);
    if (avps2 != end())
    {
      size_t len;
      const uint8_t* data = avps2->val_os(len);
      aka_auth_vector.response = hex(data, len);
      LOG_DEBUG("Found SIP-Authorization (response) %s", aka_auth_vector.response.c_str());
    }
    // Look for the encryption key.
    avps2 = avps->begin(((Cx::Dictionary*)dict())->CONFIDENTIALITY_KEY);
    if (avps2 != end())
    {
      size_t len;
      const uint8_t* data = avps2->val_os(len);
      aka_auth_vector.crypt_key = hex(data, len);
      LOG_DEBUG("Found Confidentiality-Key %s", aka_auth_vector.crypt_key.c_str());
    }
    // Look for the integrity key.
    avps2 = avps->begin(((Cx::Dictionary*)dict())->INTEGRITY_KEY);
    if (avps2 != end())
    {
      size_t len;
      const uint8_t* data = avps2->val_os(len);
      aka_auth_vector.integrity_key = hex(data, len);
      LOG_DEBUG("Found Integrity-Key %s", aka_auth_vector.integrity_key.c_str());
    }
  }
  return aka_auth_vector;
}

std::string MultimediaAuthAnswer::hex(const uint8_t* data, size_t len)
{
  static const char* const hex_lookup = "0123456789abcdef";
  std::string result;
  result.reserve(2 * len);
  for (size_t ii = 0; ii < len; ++ii)
  {
    const uint8_t b = data[ii];
    result.push_back(hex_lookup[b >> 4]);
    result.push_back(hex_lookup[b & 0x0f]);
  }
  return result;
}

std::string MultimediaAuthAnswer::base64(const uint8_t* data, size_t len)
{
  std::stringstream os;
  std::copy(boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<const uint8_t*,6,8> >(data),
            boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<const uint8_t*,6,8> >(data + len),
            boost::archive::iterators::ostream_iterator<char>(os));
  return os.str();
}

ServerAssignmentRequest::ServerAssignmentRequest(const Dictionary* dict,
                                                 Diameter::Stack* stack,
                                                 const std::string& dest_host,
                                                 const std::string& dest_realm,
                                                 const std::string& impi,
                                                 const std::string& impu,
                                                 const std::string& server_name,
                                                 const ServerAssignmentType::Type& type) :
                                                 Diameter::Message(dict, dict->SERVER_ASSIGNMENT_REQUEST, stack)
{
  LOG_DEBUG("Building Server-Assignment request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_vendor_spec_app_id();
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  if (!impi.empty())
  {
    LOG_DEBUG("Specifying User-Name %s", impi.c_str());
    add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  }
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));
  add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
  add(Diameter::AVP(dict->SERVER_ASSIGNMENT_TYPE).val_i32(type));
  add(Diameter::AVP(dict->USER_DATA_ALREADY_AVAILABLE).val_i32(0));
}

ServerAssignmentAnswer::ServerAssignmentAnswer(const Dictionary* dict,
                                               Diameter::Stack* stack,
                                               const int32_t& result_code,
                                               const std::string& ims_subscription) :
                                               Diameter::Message(dict, dict->SERVER_ASSIGNMENT_ANSWER, stack)
{
  LOG_DEBUG("Building Server-Assignment answer");

  // This method creates an SAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  add(Diameter::AVP(dict->USER_DATA).val_str(ims_subscription));
}

RegistrationTerminationRequest::RegistrationTerminationRequest(const Dictionary* dict,
                                                               Diameter::Stack* stack,
                                                               const std::string& impi,
                                                               std::vector<std::string>& associated_identities,
                                                               std::vector<std::string>& impus,
                                                               const int32_t& auth_session_state) :
                                                               Diameter::Message(dict, dict->REGISTRATION_TERMINATION_REQUEST, stack)
{
  LOG_DEBUG("Building Registration-Termination request");
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  if (!associated_identities.empty())
  {
    Diameter::AVP associated_identities_avp(dict->ASSOCIATED_IDENTITIES);
    for (std::vector<std::string>::iterator it = associated_identities.begin();
         it != associated_identities.end();
         ++it)
    {
      LOG_DEBUG("Adding Associated-Identities/User-Name %s", it->c_str());
      associated_identities_avp.add(Diameter::AVP(dict->USER_NAME).val_str(*it));
    }
    add(associated_identities_avp);
  }
  if (!impus.empty())
  {
    for (std::vector<std::string>::iterator it = impus.begin();
         it != impus.end();
         ++it)
    {
      LOG_DEBUG("Adding Public-Identity %s", it->c_str());
      add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(*it));
    }
  }
}

std::vector<std::string> RegistrationTerminationRequest::associated_identities() const
{
  std::vector<std::string> associated_identities;

  // Associated Identities are found in USER_NAME AVPS inside the ASSOCIATED_IDENTITIES AVP.
  LOG_DEBUG("Getting Associated-Identities from Registration-Termination request");
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->ASSOCIATED_IDENTITIES);
  if (avps != end())
  {
    Diameter::AVP::iterator avps2 = avps->begin(dict()->USER_NAME);
    while (avps2 != end())
    {
      LOG_DEBUG("Found User-Name %s", avps2->val_str().c_str());
      associated_identities.push_back(avps2->val_str());
      avps2++;
    }
  }
  return associated_identities;
}

std::vector<std::string> RegistrationTerminationRequest::impus() const
{
  std::vector<std::string> impus;

  // Find all the PUBLIC_IDENTITY AVPS.
  LOG_DEBUG("Getting Public-Identities from Registration-Termination request");
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY);
  while (avps != end())
  {
    LOG_DEBUG("Found Public-Identity %s", avps->val_str().c_str());
    impus.push_back(avps->val_str());
    avps++;
  }
  return impus;
}

RegistrationTerminationAnswer::RegistrationTerminationAnswer(Diameter::Message& msg,
                                                             Dictionary* dict,
                                                             const std::string result_code,
                                                             int32_t auth_session_state,
                                                             std::vector<std::string> associated_identities) :
                                                             Diameter::Message(msg)
{
  LOG_DEBUG("Building Registration-Termination answer");
  build_response(msg);
  add_vendor_spec_app_id();
  set_result_code(result_code);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));

  // Add all the private IDs we've deleted in an Associated-Identities AVP.
  if (!associated_identities.empty())
  {
    Diameter::AVP associated_identities_avp(dict->ASSOCIATED_IDENTITIES);
    for (std::vector<std::string>::iterator it = associated_identities.begin();
         it != associated_identities.end();
         ++it)
    {
      LOG_DEBUG("Adding Associated-Identities/User-Name %s", it->c_str());
      associated_identities_avp.add(Diameter::AVP(dict->USER_NAME).val_str(*it));
    }
    add(associated_identities_avp);
  }
}

std::vector<std::string> RegistrationTerminationAnswer::associated_identities() const
{
  std::vector<std::string> associated_identities;

  // Associated Identities are found in USER_NAME AVPS inside the ASSOCIATED_IDENTITIES AVP.
  LOG_DEBUG("Getting Associated-Identities from Registration-Termination answer");
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->ASSOCIATED_IDENTITIES);
  if (avps != end())
  {
    Diameter::AVP::iterator avps2 = avps->begin(dict()->USER_NAME);
    while (avps2 != end())
    {
      LOG_DEBUG("Found User-Name %s", avps2->val_str().c_str());
      associated_identities.push_back(avps2->val_str());
      avps2++;
    }
  }
  return associated_identities;
}

PushProfileRequest::PushProfileRequest(const Dictionary* dict,
                                       Diameter::Stack* stack,
                                       const std::string& impi,
                                       const DigestAuthVector& digest_av,
                                       const std::string& ims_subscription,
                                       const int32_t& auth_session_state) :
                                       Diameter::Message(dict, dict->PUSH_PROFILE_REQUEST, stack)
{
  LOG_DEBUG("Building Push-Profile request");
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  add(Diameter::AVP(dict->USER_DATA).val_str(ims_subscription));
  Diameter::AVP sip_auth_data_item(dict->SIP_AUTH_DATA_ITEM);
  Diameter::AVP sip_digest_authenticate(dict->SIP_DIGEST_AUTHENTICATE);
  sip_digest_authenticate.add(Diameter::AVP(dict->DIGEST_HA1).val_str(digest_av.ha1));
  sip_digest_authenticate.add(Diameter::AVP(dict->DIGEST_REALM).val_str(digest_av.realm));
  sip_digest_authenticate.add(Diameter::AVP(dict->DIGEST_QOP).val_str(digest_av.qop));
  sip_auth_data_item.add(sip_digest_authenticate);
  add(sip_auth_data_item);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
}

DigestAuthVector PushProfileRequest::digest_auth_vector() const
{
  LOG_DEBUG("Getting digest authentication vector from Push-Profile request");
  DigestAuthVector digest_auth_vector;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_DIGEST_AUTHENTICATE);
    if (avps != end())
    {
      // Look for the digest.
      Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_HA1);
      if (avps2 != end())
      {
        digest_auth_vector.ha1 = avps2->val_str();
        LOG_DEBUG("Found Digest-HA1 %s", digest_auth_vector.ha1.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-HA1.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_HA1);
        if (avps2 != end())
        {
          digest_auth_vector.ha1 = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-HA1 %s", digest_auth_vector.ha1.c_str());
        }
      }
      // Look for the realm.
      avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_REALM);
      if (avps2 != end())
      {
        digest_auth_vector.realm = avps2->val_str();
        LOG_DEBUG("Found Digest-Realm %s", digest_auth_vector.realm.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-Realm.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_REALM);
        if (avps2 != end())
        {
          digest_auth_vector.realm = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-Realm %s", digest_auth_vector.realm.c_str());
        }
      }
      // Look for the QoP.
      avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_QOP);
      if (avps2 != end())
      {
        digest_auth_vector.qop = avps2->val_str();
        LOG_DEBUG("Found Digest-QoP %s", digest_auth_vector.qop.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-QoP.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_QOP);
        if (avps2 != end())
        {
          digest_auth_vector.qop = avps2->val_str();
          LOG_DEBUG("Found (non-3GPP) Digest-QoP %s", digest_auth_vector.qop.c_str());
        }
      }
    }
  }
  return digest_auth_vector;
}

PushProfileAnswer::PushProfileAnswer(Diameter::Message& msg,
                                     Dictionary* dict,
                                     const std::string result_code,
                                     int32_t auth_session_state) :
                                     Diameter::Message(msg)

{
  LOG_DEBUG("Building Push-Profile answer");
  build_response(msg);
  add_vendor_spec_app_id();
  set_result_code(result_code);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
}
