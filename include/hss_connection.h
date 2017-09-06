/**
 * @file hss_connection.h Abstract base class representing connection to an HSS.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSS_CONNECTION_H__
#define HSS_CONNECTION_H__

#include <string>

#include "authvector.h"
#include "cx.h"
#include "servercapabilities.h"
#include "charging_addresses.h"
#include "statisticsmanager.h"

namespace HssConnection {

// This enum represents the various responses we can get from the HSS.
// The DiameterHssConnection will map from combinations of Diameter result code
// and experimental result code to one of these.
// Not complete
enum ResultCode
{
    SUCCESS,
    SERVER_UNAVAILABLE,
    NOT_FOUND,
    FORBIDDEN,
    TIMEOUT,
    NEW_WILDCARD,
    ASSIGNMENT_TYPE,
    UNKNOWN_AUTH_SCHEME,
    UNKNOWN
};

// Structs to represent the requests we make to the HSS
struct MultimediaAuthRequest
{
  std::string impi;
  std::string impu;
  std::string server_name;
  std::string scheme;
  std::string authorization;
};

struct UserAuthRequest
{
  std::string impi;
  std::string impu;
  std::string visited_network;
  std::string authorization_type;
  bool emergency;
};

struct LocationInfoRequest
{
  std::string impu;
  std::string originating;
  std::string authorization_type;
};

struct ServerAssignmentRequest
{
  std::string impi;
  std::string impu;
  std::string server_name;
  Cx::ServerAssignmentType type;
  bool support_shared_ifcs;
  std::string wildcard_impu;
};

// Base class that represents a response from the HSS.
// We always have an ResultCode
class HssResponse
{
public:
  virtual ~HssResponse() {};

  HssResponse(ResultCode rc) : _result_code(rc) {}

  ResultCode get_result() const
  {
    return _result_code;
  }

private:
  ResultCode _result_code;
};

// Subclasses of the HssResponse.
// We will have 4 - one for each of the 4 requests we make to the HSS.
// Each one holds the data that the handlers need to process the response.
class MultimediaAuthAnswer : public HssResponse
{
public:
  ~MultimediaAuthAnswer()
  {
    if (_auth_vector)
    {
      delete _auth_vector; _auth_vector = NULL;
    }
  }

  // Takes ownership of the AuthVector* passed in, and will delete it in its
  // destructor
  MultimediaAuthAnswer(ResultCode rc) : HssResponse(rc),
    _auth_vector(NULL),
    _sip_auth_scheme("")
  {
  }

  // Takes ownership of the AuthVector* passed in, and will delete it in its
  // destructor
  MultimediaAuthAnswer(ResultCode rc,
                       AuthVector* av,
                       std::string scheme) : HssResponse(rc),
    _auth_vector(av),
    _sip_auth_scheme(scheme)
  {
  }

  // The pointer is only valid for the life of the MultimediaAuthAnswer
  AuthVector* get_av() const
  {
    return _auth_vector;
  }

  std::string get_scheme() const
  {
    return _sip_auth_scheme;
  }

private:
  AuthVector* _auth_vector;
  std::string _sip_auth_scheme;
};

class UserAuthAnswer : public HssResponse
{
public:
  ~UserAuthAnswer(){}

  UserAuthAnswer(ResultCode rc) : HssResponse(rc),
    _json_result(0),
    _server_name(""),
    _server_capabilities()
  {
  }

  UserAuthAnswer(ResultCode rc,
                 int32_t json_result,
                 std::string server_name,
                 ServerCapabilities capabilities) : HssResponse(rc),
    _json_result(json_result),
    _server_name(server_name),
    _server_capabilities(capabilities)
  {
  }

  int32_t get_json_result() const
  {
    return _json_result;
  }

  std::string get_server() const
  {
    return _server_name;
  }

  ServerCapabilities get_server_capabilities() const
  {
    return _server_capabilities;
  }

private:
  // This is the result that we'll send on the JSON response
  int32_t _json_result;
  std::string _server_name;
  ServerCapabilities _server_capabilities;
};

class LocationInfoAnswer : public HssResponse
{
public:
  ~LocationInfoAnswer(){}

  LocationInfoAnswer(ResultCode rc) : HssResponse(rc),
    _json_result(0),
    _server_name(""),
    _server_capabilities(),
    _wildcard_impu("")
  {
  }

  LocationInfoAnswer(ResultCode rc,
                 int32_t json_result,
                 std::string server_name,
                 ServerCapabilities capabilities,
                 std::string wildcard_impu) : HssResponse(rc),
    _json_result(json_result),
    _server_name(server_name),
    _server_capabilities(capabilities),
    _wildcard_impu(wildcard_impu)
  {
  }

  int32_t get_json_result() const
  {
    return _json_result;
  }

  std::string get_server() const
  {
    return _server_name;
  }

  ServerCapabilities get_server_capabilities() const
  {
    return _server_capabilities;
  }

  std::string get_wildcard_impu() const
  {
    return _wildcard_impu;
  }

private:
  // This is the result that we'll send on the JSON response
  int32_t _json_result;
  std::string _server_name;
  ServerCapabilities _server_capabilities;
  std::string _wildcard_impu;
};

class ServerAssignmentAnswer : public HssResponse
{
public:
  ~ServerAssignmentAnswer(){}

  ServerAssignmentAnswer(ResultCode rc) : HssResponse(rc),
    _charging_addrs(),
    _service_profile("")
  {
  }

  ServerAssignmentAnswer(ResultCode rc,
                 ChargingAddresses charging_addrs,
                 std::string service_profile,
                 std::string wildcard_impu) : HssResponse(rc),
    _charging_addrs(charging_addrs),
    _service_profile(service_profile),
    _wildcard_impu(wildcard_impu)
  {
  }

  // The pointer is only valid for the life of the UserAuthAnswer
  ChargingAddresses get_charging_addresses() const
  {
    return _charging_addrs;
  }

  std::string get_service_profile() const
  {
    return _service_profile;
  }

  std::string get_wildcard_impu() const
  {
    return _wildcard_impu;
  }

private:
  ChargingAddresses _charging_addrs;
  std::string _service_profile;
  std::string _wildcard_impu;
};

// Callback typedefs
typedef std::function<void(const MultimediaAuthAnswer&)> maa_cb;
typedef std::function<void(const UserAuthAnswer&)> uaa_cb;
typedef std::function<void(const LocationInfoAnswer&)> lia_cb;
typedef std::function<void(const ServerAssignmentAnswer&)> saa_cb;

// Abstract base class that represents connection to the HSS.
// Has 4 methods, to make the 4 different requests to the HSS.
class HssConnection
{
public:
  virtual ~HssConnection() {};

  HssConnection(StatisticsManager* stats_manager) :
    _stats_manager(stats_manager)
    {
    };

  // ---------------------------------------------------------------------------
  // Methods representing communicating with the HSS.
  // Each method takes a callback, which will be called on a different thread
  // (in general) once the request is complete.
  // Ownership of the response objects passed to the callbacks is delegated to
  // the callback target. It's the responsibility of the callback to delete the
  // object when it is done with it.
  // ---------------------------------------------------------------------------

  // Send a multimedia auth request to the HSS
  virtual void send_multimedia_auth_request(maa_cb callback,
                                            MultimediaAuthRequest request,
                                            SAS::TrailId trail) = 0;

  // Send a user auth request to the HSS
  virtual void send_user_auth_request(uaa_cb callback,
                                      UserAuthRequest request,
                                      SAS::TrailId trail) = 0;

  // Send a location info request to the HSS
  virtual void send_location_info_request(lia_cb callback,
                                          LocationInfoRequest request,
                                          SAS::TrailId trail) = 0;

  // Send a server assignment request to the HSS
  virtual void send_server_assignment_request(saa_cb callback,
                                              ServerAssignmentRequest request,
                                              SAS::TrailId trail) = 0;

  static void configure_auth_schemes(const std::string& scheme_digest,
                                     const std::string& scheme_akav1,
                                     const std::string& scheme_akav2);

protected:
  StatisticsManager* _stats_manager;

  static std::string _scheme_digest;
  static std::string _scheme_akav1;
  static std::string _scheme_akav2;
};

}; // namespace HssConnection
#endif
