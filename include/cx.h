/**
 * @file cx.h class definition wrapping Cx
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef CX_H__
#define CX_H__

#include "diameterstack.h"
#include "authvector.h"
#include "servercapabilities.h"
#include "charging_addresses.h"

namespace Cx
{
class Dictionary : public Diameter::Dictionary
{
public:
  Dictionary();
  const Diameter::Dictionary::Vendor TGPP;
  const Diameter::Dictionary::Vendor TGPP2;
  const Diameter::Dictionary::Application CX;
  const Diameter::Dictionary::Message USER_AUTHORIZATION_REQUEST;
  const Diameter::Dictionary::Message USER_AUTHORIZATION_ANSWER;
  const Diameter::Dictionary::Message LOCATION_INFO_REQUEST;
  const Diameter::Dictionary::Message LOCATION_INFO_ANSWER;
  const Diameter::Dictionary::Message MULTIMEDIA_AUTH_REQUEST;
  const Diameter::Dictionary::Message MULTIMEDIA_AUTH_ANSWER;
  const Diameter::Dictionary::Message SERVER_ASSIGNMENT_REQUEST;
  const Diameter::Dictionary::Message SERVER_ASSIGNMENT_ANSWER;
  const Diameter::Dictionary::Message REGISTRATION_TERMINATION_REQUEST;
  const Diameter::Dictionary::Message REGISTRATION_TERMINATION_ANSWER;
  const Diameter::Dictionary::Message PUSH_PROFILE_REQUEST;
  const Diameter::Dictionary::Message PUSH_PROFILE_ANSWER;
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
  const Diameter::Dictionary::AVP ASSOCIATED_IDENTITIES;
  const Diameter::Dictionary::AVP DEREGISTRATION_REASON;
  const Diameter::Dictionary::AVP REASON_CODE;
  const Diameter::Dictionary::AVP IDENTITY_WITH_EMERGENCY_REGISTRATION;
  const Diameter::Dictionary::AVP CHARGING_INFORMATION;
  const Diameter::Dictionary::AVP PRIMARY_CHARGING_COLLECTION_FUNCTION_NAME;
  const Diameter::Dictionary::AVP SECONDARY_CHARGING_COLLECTION_FUNCTION_NAME;
  const Diameter::Dictionary::AVP PRIMARY_EVENT_CHARGING_FUNCTION_NAME;
  const Diameter::Dictionary::AVP SECONDARY_EVENT_CHARGING_FUNCTION_NAME;
  const Diameter::Dictionary::AVP SUPPORTED_FEATURES;
  const Diameter::Dictionary::AVP VENDOR_ID;
  const Diameter::Dictionary::AVP FEATURE_LIST_ID;
  const Diameter::Dictionary::AVP FEATURE_LIST;
  const Diameter::Dictionary::AVP WILDCARDED_PUBLIC_IDENTITY;
  const Diameter::Dictionary::AVP UAR_FLAGS;
};

class UserAuthorizationRequest : public Diameter::Message
{
public:
  UserAuthorizationRequest(const Dictionary* dict,
                           Diameter::Stack* stack,
                           const std::string& dest_host,
                           const std::string& dest_realm,
                           const std::string& impi,
                           const std::string& impu,
                           const std::string& visited_network_identifier,
                           const std::string& authorization_type,
                           const bool& emergency);
  inline UserAuthorizationRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline std::string impu() const
  {
    std::string str;
    get_str_from_avp(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY, str);
    return str;
  }
  inline bool visited_network(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->VISITED_NETWORK_IDENTIFIER, str);
  }
  inline bool auth_type(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->USER_AUTHORIZATION_TYPE, i32);
  }
  inline bool uar_flags(uint32_t& u32) const
  {
    return get_u32_from_avp(((Cx::Dictionary*)dict())->UAR_FLAGS, u32);
  }
};

class UserAuthorizationAnswer : public Diameter::Message
{
public:
  UserAuthorizationAnswer(const Dictionary* dict,
                          Diameter::Stack* stack,
                          const int32_t& result_code,
                          const uint32_t& vendor_id,
                          const int32_t& experimental_result_code,
                          const std::string& scscf,
                          const ServerCapabilities& capabs);
  inline UserAuthorizationAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline bool server_name(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->SERVER_NAME, str);
  }
  ServerCapabilities server_capabilities() const;
};


class LocationInfoRequest : public Diameter::Message
{
public:
  LocationInfoRequest(const Dictionary* dict,
                      Diameter::Stack* stack,
                      const std::string& dest_host,
                      const std::string& dest_realm,
                      const std::string& originating_request,
                      const std::string& impu,
                      const std::string& authorization_type);
  inline LocationInfoRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline bool originating(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->ORIGINATING_REQUEST, i32);
  }
  inline std::string impu() const
  {
    std::string str;
    get_str_from_avp(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY, str);
    return str;
  }
  inline bool auth_type(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->USER_AUTHORIZATION_TYPE, i32);
  }
};

class LocationInfoAnswer : public Diameter::Message
{
public:
  LocationInfoAnswer(const Dictionary* dict,
                     Diameter::Stack* stack,
                     const int32_t& result_code,
                     const uint32_t& vendor_id,
                     const int32_t& experimental_result_code,
                     const std::string& scscf,
                     const ServerCapabilities& capabs,
                     const std::string& wildcarded_public_identity = "");
  inline LocationInfoAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline bool server_name(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->SERVER_NAME, str);
  }

  inline bool wildcarded_public_identity(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->
                            WILDCARDED_PUBLIC_IDENTITY, str);
  }

  ServerCapabilities server_capabilities() const;
};

class MultimediaAuthRequest : public Diameter::Message
{
public:
  MultimediaAuthRequest(const Dictionary* dict,
                        Diameter::Stack* stack,
                        const std::string& dest_realm,
                        const std::string& dest_host,
                        const std::string& impi,
                        const std::string& impu,
                        const std::string& server_name,
                        const std::string& sip_auth_scheme,
                        const std::string& sip_authorization = "");
  inline MultimediaAuthRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline std::string impu() const
  {
    std::string str;
    get_str_from_avp(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY, str);
    return str;
  }
  inline bool server_name(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->SERVER_NAME, str);
  }
  std::string sip_auth_scheme() const;
  std::string sip_authorization() const;
  inline bool sip_number_auth_items(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->SIP_NUMBER_AUTH_ITEMS, i32);
  }
};

class MultimediaAuthAnswer : public Diameter::Message
{
public:
  MultimediaAuthAnswer(const Dictionary* dict,
                       Diameter::Stack* stack,
                       const int32_t& result_code,
                       const uint32_t& vendor_id,
                       const int32_t& experimental_result_code,
                       const std::string& scheme,
                       const DigestAuthVector& digest_av,
                       const AKAAuthVector& aka_av);
  inline MultimediaAuthAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  std::string sip_auth_scheme() const;
  DigestAuthVector digest_auth_vector() const;
  AKAAuthVector aka_auth_vector() const;
  AKAAuthVector akav2_auth_vector() const;
};

enum ServerAssignmentType
{
    NO_ASSIGNMENT = 0,
    REGISTRATION = 1,
    RE_REGISTRATION = 2,
    UNREGISTERED_USER = 3,
    TIMEOUT_DEREGISTRATION = 4,
    USER_DEREGISTRATION = 5,
    TIMEOUT_DEREGISTRATION_STORE_SERVER_NAME = 6, // Currently not used
    USER_DEREGISTRATION_STORE_SERVER_NAME = 7, // Currently not used
    ADMINISTRATIVE_DEREGISTRATION = 8,
    AUTHENTICATION_FAILURE = 9,
    AUTHENTICATION_TIMEOUT = 10,
    DEREGISTRATION_TOO_MUCH_DATA = 11 // Currently not used
};

class ServerAssignmentRequest : public Diameter::Message
{
public:
  ServerAssignmentRequest(const Dictionary* dict,
                          Diameter::Stack* stack,
                          const std::string& dest_host,
                          const std::string& dest_realm,
                          const std::string& impi,
                          const std::string& impu,
                          const std::string& server_name,
                          const Cx::ServerAssignmentType type,
                          const bool support_shared_ifcs,
                          const std::string& wildcard = "");
  inline ServerAssignmentRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline std::string impu() const
  {
    std::string str;
    get_str_from_avp(((Cx::Dictionary*)dict())->PUBLIC_IDENTITY, str);
    return str;
  }
  inline bool server_name(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->SERVER_NAME, str);
  }
  inline bool server_assignment_type(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->SERVER_ASSIGNMENT_TYPE, i32);
  }
  inline bool user_data_already_available(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->USER_DATA_ALREADY_AVAILABLE, i32);
  }

  bool include_wildcard_on_sar(Cx::ServerAssignmentType type);

};

class ServerAssignmentAnswer : public Diameter::Message
{
public:
  ServerAssignmentAnswer(const Dictionary* dict,
                         Diameter::Stack* stack,
                         const int32_t& result_code,
                         const uint32_t& vendor_id,
                         const int32_t& experimental_result_code,
                         const std::string& ims_subscription,
                         const ChargingAddresses& charging_addrs,
                         const std::string& wildcard = "");
  inline ServerAssignmentAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline bool user_data(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->USER_DATA, str);
  }
  inline bool wildcarded_public_identity(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->WILDCARDED_PUBLIC_IDENTITY, str);
  }
  inline bool server_assignment_type(int32_t& i32) const
  {
    return get_i32_from_avp(((Cx::Dictionary*)dict())->SERVER_ASSIGNMENT_TYPE, i32);
  }

  void charging_addrs(ChargingAddresses& charging_addrs) const;
};

class RegistrationTerminationRequest : public Diameter::Message
{
public:
  RegistrationTerminationRequest(const Dictionary* dict,
                                 Diameter::Stack* stack,
                                 const int32_t& deregistration_reason,
                                 const std::string& impi,
                                 std::vector<std::string>& associated_identities,
                                 std::vector<std::string>& impus,
                                 const int32_t& auth_session_state);
  inline RegistrationTerminationRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  std::vector<std::string> associated_identities() const;
  std::vector<std::string> impus() const;
  int32_t deregistration_reason() const;
};

class RegistrationTerminationAnswer : public Diameter::Message
{
public:
  RegistrationTerminationAnswer(Cx::RegistrationTerminationRequest& rtr,
                                Dictionary* dict,
                                const std::string result_code,
                                int32_t auth_session_state,
                                std::vector<std::string> impis);
  inline RegistrationTerminationAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};

  std::vector<std::string> associated_identities() const;
};

class PushProfileRequest : public Diameter::Message
{
public:
  PushProfileRequest(const Dictionary* dict,
                     Diameter::Stack* stack,
                     const std::string& impi,
                     const std::string& ims_subscription,
                     const ChargingAddresses& charging_addrs,
                     const int32_t& auth_session_state);
  inline PushProfileRequest(Diameter::Message& msg) : Diameter::Message(msg) {};

  inline bool user_data(std::string& str) const
  {
    return get_str_from_avp(((Cx::Dictionary*)dict())->USER_DATA, str);
  }

  bool charging_addrs(ChargingAddresses& charging_addrs) const;
};

class PushProfileAnswer : public Diameter::Message
{
public:
  PushProfileAnswer(Cx::PushProfileRequest& ppr,
                    Dictionary* dict,
                    const std::string result_code,
                    int32_t auth_session_state);
  inline PushProfileAnswer(Diameter::Message& msg) : Diameter::Message(msg) {};
};

};

#endif
