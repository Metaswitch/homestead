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
#include "base64.h"

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
  REASON_CODE("3GPP", "Reason-Code"),
  IDENTITY_WITH_EMERGENCY_REGISTRATION("3GPP", "Identity-with-Emergency-Registration"),
  CHARGING_INFORMATION("3GPP", "Charging-Information"),
  PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME("3GPP", "Primary-Charging-Collection-Function-Name"),
  SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME("3GPP", "Secondary-Charging-Collection-Function-Name"),
  PRIMARY_EVENT_CHARGING_FUNCTION_NAME("3GPP", "Primary-Event-Charging-Function-Name"),
  SECONDARY_EVENT_CHARGING_FUNCTION_NAME("3GPP", "Secondary-Event-Charging-Function-Name")
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
  TRC_DEBUG("Building User-Authorization request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  if (!dest_host.empty())
  {
    add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  }
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
                                                 const uint32_t& vendor_id,
                                                 const int32_t& experimental_result_code,
                                                 const std::string& server_name,
                                                 const ServerCapabilities& capabs) :
                                                 Diameter::Message(dict, dict->USER_AUTHORIZATION_ANSWER, stack)
{
  TRC_DEBUG("Building User-Authorization answer");

  // This method creates a UAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->VENDOR_ID).val_u32(vendor_id));
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    add(experimental_result);
  }

  if (!server_name.empty())
  {
    add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
  }

  Diameter::AVP server_capabilities(dict->SERVER_CAPABILITIES);
  if (!capabs.server_name.empty())
  {
    server_capabilities.add(Diameter::AVP(dict->SERVER_NAME).val_str(capabs.server_name));
  }
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
  ServerCapabilities server_capabilities({}, {}, "");

  // Server capabilities are grouped into mandatory capabilities and optional
  // capabilities underneath the SERVER_CAPABILITIES AVP.
  TRC_DEBUG("Getting server capabilties from User-Authorization answer");
  Diameter::AVP::iterator server_capabilities_avp =
                         begin(((Cx::Dictionary*)dict())->SERVER_CAPABILITIES);
  if (server_capabilities_avp != end())
  {
    Diameter::AVP::iterator mandatory_capability_avp =
      server_capabilities_avp->begin(((Cx::Dictionary*)dict())->MANDATORY_CAPABILITY);
    while (mandatory_capability_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found mandatory capability %d", mandatory_capability_avp->val_i32());
      server_capabilities.mandatory_capabilities.push_back(mandatory_capability_avp->val_i32());
      mandatory_capability_avp++;
    }

    Diameter::AVP::iterator optional_capability_avp =
      server_capabilities_avp->begin(((Cx::Dictionary*)dict())->OPTIONAL_CAPABILITY);
    while (optional_capability_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found optional capability %d", optional_capability_avp->val_i32());
      server_capabilities.optional_capabilities.push_back(optional_capability_avp->val_i32());
      optional_capability_avp++;
    }
    Diameter::AVP::iterator server_name_avp =
        server_capabilities_avp->begin(((Cx::Dictionary*)dict())->SERVER_NAME);
    if (server_name_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found server name %s", server_name_avp->val_str().c_str());
      server_capabilities.server_name = server_name_avp->val_str();
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
  TRC_DEBUG("Building Location-Info request for %s", impu.c_str());
  add_new_session_id();
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  if (!dest_host.empty())
  {
    add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  }
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
                                       const uint32_t& vendor_id,
                                       const int32_t& experimental_result_code,
                                       const std::string& server_name,
                                       const ServerCapabilities& capabs) :
                                       Diameter::Message(dict, dict->USER_AUTHORIZATION_ANSWER, stack)
{
  TRC_DEBUG("Building Location-Info answer");

  // This method creates an LIA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->VENDOR_ID).val_u32(vendor_id));
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    add(experimental_result);
  }

  if (!server_name.empty())
  {
    add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
  }

  Diameter::AVP server_capabilities(dict->SERVER_CAPABILITIES);

  if (!capabs.server_name.empty())
  {
    server_capabilities.add(Diameter::AVP(dict->SERVER_NAME).val_str(capabs.server_name));
  }

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
  ServerCapabilities server_capabilities({}, {}, "");

  // Server capabilities are grouped into mandatory capabilities and optional
  // capabilities underneath the SERVER_CAPABILITIES AVP.
  Diameter::AVP::iterator server_capabilities_avp =
                         begin(((Cx::Dictionary*)dict())->SERVER_CAPABILITIES);
  if (server_capabilities_avp != end())
  {
    Diameter::AVP::iterator mandatory_capability_avp =
      server_capabilities_avp->begin(((Cx::Dictionary*)dict())->MANDATORY_CAPABILITY);
    while (mandatory_capability_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found mandatory capability %d", mandatory_capability_avp->val_i32());
      server_capabilities.mandatory_capabilities.push_back(mandatory_capability_avp->val_i32());
      mandatory_capability_avp++;
    }

    Diameter::AVP::iterator optional_capability_avp =
      server_capabilities_avp->begin(((Cx::Dictionary*)dict())->OPTIONAL_CAPABILITY);
    while (optional_capability_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found optional capability %d", optional_capability_avp->val_i32());
      server_capabilities.optional_capabilities.push_back(optional_capability_avp->val_i32());
      optional_capability_avp++;
    }
    Diameter::AVP::iterator server_name_avp =
        server_capabilities_avp->begin(((Cx::Dictionary*)dict())->SERVER_NAME);
    if (server_name_avp != server_capabilities_avp->end())
    {
      TRC_DEBUG("Found server name %s", server_name_avp->val_str().c_str());
      server_capabilities.server_name = server_name_avp->val_str();
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
  TRC_DEBUG("Building Multimedia-Auth request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  if (!dest_host.empty())
  {
    add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  }
  add_origin();
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));
  Diameter::AVP sip_auth_data_item(dict->SIP_AUTH_DATA_ITEM);
  sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTH_SCHEME).val_str(sip_auth_scheme));
  if (!sip_authorization.empty())
  {
    TRC_DEBUG("Specifying SIP-Authorization %s", sip_authorization.c_str());
    sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTHORIZATION).val_str(sip_authorization));
  }
  add(sip_auth_data_item);
  add(Diameter::AVP(dict->SIP_NUMBER_AUTH_ITEMS).val_i32(1));
  add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
}

std::string MultimediaAuthRequest::sip_auth_scheme() const
{
  std::string sip_auth_scheme;
  Diameter::AVP::iterator sip_auth_data_item_avp =
                           begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (sip_auth_data_item_avp != end())
  {
    Diameter::AVP::iterator sip_auth_scheme_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_AUTH_SCHEME);
    if (sip_auth_scheme_avp != sip_auth_data_item_avp->end())
    {
      sip_auth_scheme = sip_auth_scheme_avp->val_str();
      TRC_DEBUG("Got SIP-Auth-Scheme %s", sip_auth_scheme.c_str());
    }
  }
  return sip_auth_scheme;
}

std::string MultimediaAuthRequest::sip_authorization() const
{
  std::string sip_authorization;
  Diameter::AVP::iterator sip_auth_data_item_avp =
                           begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (sip_auth_data_item_avp != end())
  {
    Diameter::AVP::iterator sip_authorization_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_AUTHORIZATION);
    if (sip_authorization_avp != sip_auth_data_item_avp->end())
    {
      sip_authorization = sip_authorization_avp->val_str();
      TRC_DEBUG("Got SIP-Authorization %s", sip_authorization.c_str());
    }
  }
  return sip_authorization;
}

MultimediaAuthAnswer::MultimediaAuthAnswer(const Dictionary* dict,
                                           Diameter::Stack* stack,
                                           const int32_t& result_code,
                                           const uint32_t& vendor_id,
                                           const int32_t& experimental_result_code,
                                           const std::string& scheme,
                                           const DigestAuthVector& digest_av,
                                           const AKAAuthVector& aka_av) :
                                           Diameter::Message(dict, dict->MULTIMEDIA_AUTH_ANSWER, stack)
{
  TRC_DEBUG("Building Multimedia-Authorization answer");

  // This method creates an MAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    experimental_result.add(Diameter::AVP(dict->VENDOR_ID).val_u32(vendor_id));
    add(experimental_result);
  }
  Diameter::AVP sip_auth_data_item(dict->SIP_AUTH_DATA_ITEM);
  if (!scheme.empty())
  {
    sip_auth_data_item.add(Diameter::AVP(dict->SIP_AUTH_SCHEME).val_str(scheme));
  }
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
  Diameter::AVP::iterator sip_auth_data_item_avp =
                           begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (sip_auth_data_item_avp != end())
  {
    Diameter::AVP::iterator sip_auth_scheme_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_AUTH_SCHEME);
    if (sip_auth_scheme_avp != sip_auth_data_item_avp->end())
    {
      sip_auth_scheme = sip_auth_scheme_avp->val_str();
      TRC_DEBUG("Got SIP-Auth-Scheme %s", sip_auth_scheme.c_str());
    }
  }
  return sip_auth_scheme;
}

DigestAuthVector MultimediaAuthAnswer::digest_auth_vector() const
{
  TRC_DEBUG("Getting digest authentication vector from Multimedia-Auth answer");
  DigestAuthVector digest_auth_vector;
  Diameter::AVP::iterator sip_auth_data_item_avp =
                           begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (sip_auth_data_item_avp != end())
  {
    Diameter::AVP::iterator sip_digest_authenticate_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_DIGEST_AUTHENTICATE);
    if (sip_digest_authenticate_avp != sip_auth_data_item_avp->end())
    {
      // Look for the digest.
      Diameter::AVP::iterator digest_ha1_avp =
        sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->CX_DIGEST_HA1);
      if (digest_ha1_avp != sip_digest_authenticate_avp->end())
      {
        digest_auth_vector.ha1 = digest_ha1_avp->val_str();
        TRC_DEBUG("Found Digest-HA1 %s", digest_auth_vector.ha1.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-HA1.
        // Check for this too.
        digest_ha1_avp =
          sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->DIGEST_HA1);
        if (digest_ha1_avp != sip_digest_authenticate_avp->end())
        {
          digest_auth_vector.ha1 = digest_ha1_avp->val_str();
          TRC_DEBUG("Found (non-3GPP) Digest-HA1 %s",
                    digest_auth_vector.ha1.c_str());
        }
      }
      // Look for the realm.
      Diameter::AVP::iterator digest_realm_avp =
        sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->CX_DIGEST_REALM);
      if (digest_realm_avp != sip_digest_authenticate_avp->end())
      {
        digest_auth_vector.realm = digest_realm_avp->val_str();
        TRC_DEBUG("Found Digest-Realm %s", digest_auth_vector.realm.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-Realm.
        // Check for this too.
        digest_realm_avp =
          sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->DIGEST_REALM);
        if (digest_realm_avp != sip_digest_authenticate_avp->end())
        {
          digest_auth_vector.realm = digest_realm_avp->val_str();
          TRC_DEBUG("Found (non-3GPP) Digest-Realm %s",
                    digest_auth_vector.realm.c_str());
        }
      }
      // Look for the QoP.
      Diameter::AVP::iterator digest_qop_avp =
        sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->CX_DIGEST_QOP);
      if (digest_qop_avp != sip_digest_authenticate_avp->end())
      {
        digest_auth_vector.qop = digest_qop_avp->val_str();
        TRC_DEBUG("Found Digest-QoP %s", digest_auth_vector.qop.c_str());
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-QoP.
        // Check for this too.
        digest_qop_avp =
          sip_digest_authenticate_avp->begin(((Cx::Dictionary*)dict())->DIGEST_QOP);
        if (digest_qop_avp != sip_digest_authenticate_avp->end())
        {
          digest_auth_vector.qop = digest_qop_avp->val_str();
          TRC_DEBUG("Found (non-3GPP) Digest-QoP %s",
                    digest_auth_vector.qop.c_str());
        }
      }
    }
  }
  return digest_auth_vector;
}

AKAAuthVector MultimediaAuthAnswer::aka_auth_vector() const
{
  TRC_DEBUG("Getting AKA authentication vector from Multimedia-Auth answer");
  AKAAuthVector aka_auth_vector;
  Diameter::AVP::iterator sip_auth_data_item_avp =
                           begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (sip_auth_data_item_avp != end())
  {
    // Look for the challenge.
    Diameter::AVP::iterator sip_authenticate_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_AUTHENTICATE);
    if (sip_authenticate_avp != sip_auth_data_item_avp->end())
    {
      size_t len;
      const uint8_t* data = sip_authenticate_avp->val_os(len);
      aka_auth_vector.challenge = base64_encode(data, len);
      TRC_DEBUG("Found SIP-Authenticate (challenge) %s",
                aka_auth_vector.challenge.c_str());
    }

    // Look for the response.
    Diameter::AVP::iterator sip_authorization_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->SIP_AUTHORIZATION);
    if (sip_authorization_avp != sip_auth_data_item_avp->end())
    {
      size_t len;
      const uint8_t* data = sip_authorization_avp->val_os(len);
      aka_auth_vector.response = Utils::hex(data, len);
      TRC_DEBUG("Found SIP-Authorization (response) %s",
                aka_auth_vector.response.c_str());
    }

    // Look for the encryption key.
    Diameter::AVP::iterator confidentiality_key_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->CONFIDENTIALITY_KEY);
    if (confidentiality_key_avp != sip_auth_data_item_avp->end())
    {
      size_t len;
      const uint8_t* data = confidentiality_key_avp->val_os(len);
      aka_auth_vector.crypt_key = Utils::hex(data, len);
      TRC_DEBUG("Found Confidentiality-Key %s",
                aka_auth_vector.crypt_key.c_str());
    }

    // Look for the integrity key.
    Diameter::AVP::iterator integrity_key_avp =
      sip_auth_data_item_avp->begin(((Cx::Dictionary*)dict())->INTEGRITY_KEY);
    if (integrity_key_avp != sip_auth_data_item_avp->end())
    {
      size_t len;
      const uint8_t* data = integrity_key_avp->val_os(len);
      aka_auth_vector.integrity_key = Utils::hex(data, len);
      TRC_DEBUG("Found Integrity-Key %s",
                aka_auth_vector.integrity_key.c_str());
    }
  }
  return aka_auth_vector;
}

AKAAuthVector MultimediaAuthAnswer::akav2_auth_vector() const
{
  AKAAuthVector av = aka_auth_vector();
  av.version = 2;
  return av;
}

ServerAssignmentRequest::ServerAssignmentRequest(const Dictionary* dict,
                                                 Diameter::Stack* stack,
                                                 const std::string& dest_host,
                                                 const std::string& dest_realm,
                                                 const std::string& impi,
                                                 const std::string& impu,
                                                 const std::string& server_name,
                                                 const Cx::ServerAssignmentType type) :
                                                 Diameter::Message(dict, dict->SERVER_ASSIGNMENT_REQUEST, stack)
{
  TRC_DEBUG("Building Server-Assignment request for %s/%s", impi.c_str(), impu.c_str());
  add_new_session_id();
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add_origin();
  if (!dest_host.empty())
  {
    add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  }
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  if (!impi.empty())
  {
    TRC_DEBUG("Specifying User-Name %s", impi.c_str());
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
                                               const uint32_t& vendor_id,
                                               const int32_t& experimental_result_code,
                                               const std::string& ims_subscription,
                                               const ChargingAddresses& charging_addrs) :
                                               Diameter::Message(dict, dict->SERVER_ASSIGNMENT_ANSWER, stack)
{
  TRC_DEBUG("Building Server-Assignment answer");

  // This method creates an SAA which is unrealistic for various reasons, but is useful for
  // testing our handlers code, which is currently all it is used for.
  if (result_code)
  {
    add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
  }
  else
  {
    Diameter::AVP experimental_result(dict->EXPERIMENTAL_RESULT);
    experimental_result.add(Diameter::AVP(dict->EXPERIMENTAL_RESULT_CODE).val_i32(experimental_result_code));
    experimental_result.add(Diameter::AVP(dict->VENDOR_ID).val_u32(vendor_id));
    add(experimental_result);
  }

  add(Diameter::AVP(dict->USER_DATA).val_str(ims_subscription));

  if (!charging_addrs.empty())
  {
    Diameter::AVP charging_information(dict->CHARGING_INFORMATION);
    if (!charging_addrs.ccfs.empty())
    {
      TRC_DEBUG("Adding Primary-Charging-Collection-Function-Name %s", charging_addrs.ccfs[0].c_str());
      charging_information.add(Diameter::AVP(dict->PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME).
                               val_str(charging_addrs.ccfs[0]));
    }
    if (charging_addrs.ccfs.size() > 1)
    {
      TRC_DEBUG("Adding Secondary-Charging-Collection-Function-Name %s", charging_addrs.ccfs[1].c_str());
      charging_information.add(Diameter::AVP(dict->SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME).
                               val_str(charging_addrs.ccfs[1]));
    }
    if (!charging_addrs.ecfs.empty())
    {
      TRC_DEBUG("Adding Primary-Event-Charging-Function-Name %s", charging_addrs.ecfs[0].c_str());
      charging_information.add(Diameter::AVP(dict->PRIMARY_EVENT_CHARGING_FUNCTION_NAME).
                               val_str(charging_addrs.ecfs[0]));
    }
    if (charging_addrs.ccfs.size() > 1)
    {
      TRC_DEBUG("Adding Secondary-Event-Charging-Function-Name %s", charging_addrs.ecfs[1].c_str());
      charging_information.add(Diameter::AVP(dict->SECONDARY_EVENT_CHARGING_FUNCTION_NAME).
                               val_str(charging_addrs.ecfs[1]));
    }
    add(charging_information);
  }
}

void ServerAssignmentAnswer::charging_addrs(ChargingAddresses& charging_addrs) const
{
  Diameter::AVP::iterator charging_information_avp =
                         begin(((Cx::Dictionary*)dict())->CHARGING_INFORMATION);
  if (charging_information_avp != end())
  {
    Diameter::AVP::iterator primary_ccf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME);
    if (primary_ccf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ccfs.push_back(primary_ccf_name_avp->val_str());
      TRC_DEBUG("Found Primary-Charging-Collection-Function-Name %s",
                primary_ccf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator secondary_ccf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME);
    if (secondary_ccf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ccfs.push_back(secondary_ccf_name_avp->val_str());
      TRC_DEBUG("Found Secondary-Charging-Collection-Function-Name %s",
                secondary_ccf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator primary_ecf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->PRIMARY_EVENT_CHARGING_FUNCTION_NAME);
    if (primary_ecf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ecfs.push_back(primary_ecf_name_avp->val_str());
      TRC_DEBUG("Found Primary-Event-Charging-Function-Name %s",
                primary_ecf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator secondary_ecf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->SECONDARY_EVENT_CHARGING_FUNCTION_NAME);
    if (secondary_ecf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ecfs.push_back(secondary_ecf_name_avp->val_str());
      TRC_DEBUG("Found Secondary-Event-Charging-Function-Name %s",
                secondary_ecf_name_avp->val_str().c_str());
    }
  }
}

RegistrationTerminationRequest::RegistrationTerminationRequest(const Dictionary* dict,
                                                               Diameter::Stack* stack,
                                                               const int32_t& deregistration_reason,
                                                               const std::string& impi,
                                                               std::vector<std::string>& associated_identities,
                                                               std::vector<std::string>& impus,
                                                               const int32_t& auth_session_state) :
                                                               Diameter::Message(dict, dict->REGISTRATION_TERMINATION_REQUEST, stack)
{
  TRC_DEBUG("Building Registration-Termination request");
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  if (!associated_identities.empty())
  {
    Diameter::AVP associated_identities_avp(dict->ASSOCIATED_IDENTITIES);
    for (std::vector<std::string>::iterator it = associated_identities.begin();
         it != associated_identities.end();
         ++it)
    {
      TRC_DEBUG("Adding Associated-Identities/User-Name %s", it->c_str());
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
      TRC_DEBUG("Adding Public-Identity %s", it->c_str());
      add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(*it));
    }
  }
  Diameter::AVP deregistration_reason_avp(dict->DEREGISTRATION_REASON);
  deregistration_reason_avp.add(Diameter::AVP(dict->REASON_CODE).val_i32(deregistration_reason));
  add(deregistration_reason_avp);
}

std::vector<std::string> RegistrationTerminationRequest::associated_identities() const
{
  std::vector<std::string> associated_identities;

  // Associated Identities are found in USER_NAME AVPS inside the ASSOCIATED_IDENTITIES AVP.
  TRC_DEBUG("Getting Associated-Identities from Registration-Termination request");
  Diameter::AVP::iterator associated_identities_avp =
                        begin(((Cx::Dictionary*)dict())->ASSOCIATED_IDENTITIES);
  if (associated_identities_avp != end())
  {
    Diameter::AVP::iterator user_name_avp =
                            associated_identities_avp->begin(dict()->USER_NAME);
    while (user_name_avp != associated_identities_avp->end())
    {
      TRC_DEBUG("Found User-Name %s", user_name_avp->val_str().c_str());
      associated_identities.push_back(user_name_avp->val_str());
      user_name_avp++;
    }
  }
  return associated_identities;
}

std::vector<std::string> RegistrationTerminationRequest::impus() const
{
  std::vector<std::string> impus;

  // Find all the PUBLIC_IDENTITY AVPS.
  TRC_DEBUG("Getting Public-Identities from Registration-Termination request");
  Diameter::AVP::iterator public_identity_avps =
                              begin(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY);
  while (public_identity_avps != end())
  {
    TRC_DEBUG("Found Public-Identity %s",
              public_identity_avps->val_str().c_str());
    impus.push_back(public_identity_avps->val_str());
    public_identity_avps++;
  }
  return impus;
}

int32_t RegistrationTerminationRequest::deregistration_reason() const
{
  int32_t deregistration_reason = 0;
  Diameter::AVP::iterator dereg_reason_avp =
                        begin(((Cx::Dictionary*)dict())->DEREGISTRATION_REASON);
  if (dereg_reason_avp != end())
  {
    Diameter::AVP::iterator reason_code_avp =
                dereg_reason_avp->begin(((Cx::Dictionary*)dict())->REASON_CODE);
    if (reason_code_avp != dereg_reason_avp->end())
    {
      deregistration_reason = reason_code_avp->val_i32();
      TRC_DEBUG("Got Deregistration Reason-Code %d", deregistration_reason);
    }
  }
  return deregistration_reason;
}

RegistrationTerminationAnswer::RegistrationTerminationAnswer(Cx::RegistrationTerminationRequest& rtr,
                                                             Dictionary* dict,
                                                             const std::string result_code,
                                                             int32_t auth_session_state,
                                                             std::vector<std::string> associated_identities) :
                                                             Diameter::Message(rtr)
{
  TRC_DEBUG("Building Registration-Termination answer");
  build_response(rtr);
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
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
      TRC_DEBUG("Adding Associated-Identities/User-Name %s", it->c_str());
      associated_identities_avp.add(Diameter::AVP(dict->USER_NAME).val_str(*it));
    }
    add(associated_identities_avp);
  }
}

std::vector<std::string> RegistrationTerminationAnswer::associated_identities() const
{
  std::vector<std::string> associated_identities;

  // Associated Identities are found in USER_NAME AVPS inside the
  // ASSOCIATED_IDENTITIES AVP.
  TRC_DEBUG("Getting Associated-Identities from Registration-Termination answer");
  Diameter::AVP::iterator associated_identities_avp =
                        begin(((Cx::Dictionary*)dict())->ASSOCIATED_IDENTITIES);
  if (associated_identities_avp != end())
  {
    Diameter::AVP::iterator user_name_avp =
                            associated_identities_avp->begin(dict()->USER_NAME);
    while (user_name_avp != associated_identities_avp->end())
    {
      TRC_DEBUG("Found User-Name %s", user_name_avp->val_str().c_str());
      associated_identities.push_back(user_name_avp->val_str());
      user_name_avp++;
    }
  }
  return associated_identities;
}

PushProfileRequest::PushProfileRequest(const Dictionary* dict,
                                       Diameter::Stack* stack,
                                       const std::string& impi,
                                       const std::string& ims_subscription,
                                       const ChargingAddresses& charging_addrs,
                                       const int32_t& auth_session_state) :
                                       Diameter::Message(dict, dict->PUSH_PROFILE_REQUEST, stack)
{
  TRC_DEBUG("Building Push-Profile request");
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  if (!ims_subscription.empty())
  {
    add(Diameter::AVP(dict->USER_DATA).val_str(ims_subscription));
  }
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
  if (!charging_addrs.empty())
  {
    Diameter::AVP charging_information(dict->CHARGING_INFORMATION);
    if (!charging_addrs.ccfs.empty())
    {
      TRC_DEBUG("Adding Primary-Charging-Collection-Function-Name %s", charging_addrs.ccfs[0].c_str());
      charging_information.add(Diameter::AVP(dict->PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME).
                               val_str(charging_addrs.ccfs[0]));
    }
    if (charging_addrs.ccfs.size() > 1)
    {
      TRC_DEBUG("Adding Secondary-Charging-Collection-Function-Name %s", charging_addrs.ccfs[1].c_str());
      charging_information.add(Diameter::AVP(dict->SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME).
                               val_str(charging_addrs.ccfs[1]));
    }
    if (!charging_addrs.ecfs.empty())
    {
      TRC_DEBUG("Adding Primary-Event-Charging-Function-Name %s", charging_addrs.ecfs[0].c_str());
      charging_information.add(Diameter::AVP(dict->PRIMARY_EVENT_CHARGING_FUNCTION_NAME).
                               val_str(charging_addrs.ecfs[0]));
    }
    if (charging_addrs.ccfs.size() > 1)
    {
      TRC_DEBUG("Adding Secondary-Event-Charging-Function-Name %s", charging_addrs.ecfs[1].c_str());
      charging_information.add(Diameter::AVP(dict->SECONDARY_EVENT_CHARGING_FUNCTION_NAME).
                               val_str(charging_addrs.ecfs[1]));
    }
    add(charging_information);
  }
}

// Get charging addresses from a PPR. Return false if there is no Charging-Information AVP, true
// otherwise.
bool PushProfileRequest::charging_addrs(ChargingAddresses& charging_addrs) const
{
  bool found_charging_info = false;

  Diameter::AVP::iterator charging_information_avp =
                         begin(((Cx::Dictionary*)dict())->CHARGING_INFORMATION);
  if (charging_information_avp != end())
  {
    found_charging_info = true;

    Diameter::AVP::iterator primary_ccf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME);
    if (primary_ccf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ccfs.push_back(primary_ccf_name_avp->val_str());
      TRC_DEBUG("Found Primary-Charging-Collection-Function-Name %s",
                primary_ccf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator secondary_ccf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME);
    if (secondary_ccf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ccfs.push_back(secondary_ccf_name_avp->val_str());
      TRC_DEBUG("Found Secondary-Charging-Collection-Function-Name %s",
                secondary_ccf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator primary_ecf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->PRIMARY_EVENT_CHARGING_FUNCTION_NAME);
    if (primary_ecf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ecfs.push_back(primary_ecf_name_avp->val_str());
      TRC_DEBUG("Found Primary-Event-Charging-Function-Name %s",
                primary_ecf_name_avp->val_str().c_str());
    }

    Diameter::AVP::iterator secondary_ecf_name_avp =
      charging_information_avp->begin(((Cx::Dictionary*)dict())->SECONDARY_EVENT_CHARGING_FUNCTION_NAME);
    if (secondary_ecf_name_avp != charging_information_avp->end())
    {
      charging_addrs.ecfs.push_back(secondary_ecf_name_avp->val_str());
      TRC_DEBUG("Found Secondary-Event-Charging-Function-Name %s",
                secondary_ecf_name_avp->val_str().c_str());
    }
  }
  return found_charging_info;
}

PushProfileAnswer::PushProfileAnswer(Cx::PushProfileRequest& ppr,
                                     Dictionary* dict,
                                     const std::string result_code,
                                     int32_t auth_session_state) :
                                     Diameter::Message(ppr)

{
  TRC_DEBUG("Building Push-Profile answer");
  build_response(ppr);
  add_app_id(Diameter::Dictionary::Application::AUTH, dict->TGPP, dict->CX);
  set_result_code(result_code);
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(auth_session_state));
}
