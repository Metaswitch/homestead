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
#include "servercapabilities.h"
#include "charging_addresses.h"

namespace HssConnection {



// This enum represents the various responses we can get from the HSS.
// The DiameterHssConnection will map from combinations of Diameter result code
// and experimental result code to one of these.
// Not complete
enum ResultCode
{
    SUCCESS = 0,
    SERVER_UNAVAILABLE = 1,
    NOT_FOUND = 2,
    FORBIDDEN = 3,
    TIMEOUT = 4,
    NEW_WILDCARD = 5,
    ASSIGNMENT_TYPE = 6,
    UNKNOWN = 7
};

// Structs to represent the requests we make to the HSS
struct MultimediaAuthRequest
{
  std::string impi;
  std::string impu;
  std::string provided_server_name;
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
  std::string provided_server_name;
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

  ResultCode get_result()
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
  MultimediaAuthAnswer(ResultCode rc,
                         AuthVector* av,
                         std::string scheme) : HssResponse(rc),
    _auth_vector(av),
    _sip_auth_scheme(scheme)
  {
  }

  // The pointer is only valid for the life of the MultimediaAuthAnswer
  AuthVector* get_av()
  {
    return _auth_vector;
  }

  std::string get_scheme()
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
  ~UserAuthAnswer()
  {
    if (_server_capabilities)
    {
      delete _server_capabilities; _server_capabilities = NULL;
    }
  }

  // Takes ownership of the ServerCapabilities* passed in, and will delete it in its
  // destructor
  UserAuthAnswer(ResultCode rc,
                 int32_t json_result,
                 std::string server_name,
                 ServerCapabilities* capabilities) : HssResponse(rc),
    _json_result(json_result),
    _server_name(server_name),
    _server_capabilities(capabilities)
  {
  }

  int32_t get_json_result()
  {
    return _json_result;
  }

  std::string get_server()
  {
    return _server_name;
  }
  // The pointer is only valid for the life of the UserAuthAnswer
  ServerCapabilities* get_server_capabilities()
  {
    return _server_capabilities;
  }

private:
  // This is the result that we'll send on the JSON response
  int32_t _json_result;
  std::string _server_name;
  ServerCapabilities* _server_capabilities;
};

class LocationInfoAnswer : public HssResponse
{
public:
  ~LocationInfoAnswer()
  {
    if (_server_capabilities)
    {
      delete _server_capabilities; _server_capabilities = NULL;
    }
  }

  // Takes ownership of the ServerCapabilities* passed in, and will delete it in its
  // destructor
  LocationInfoAnswer(ResultCode rc,
                 int32_t json_result,
                 std::string server_name,
                 ServerCapabilities* capabilities,
                 std::string wildcard_impu) : HssResponse(rc),
    _json_result(json_result),
    _server_name(server_name),
    _server_capabilities(capabilities),
    _wildcard_impu(wildcard_impu)
  {
  }

  int32_t get_json_result()
  {
    return _json_result;
  }

  std::string get_server()
  {
    return _server_name;
  }

  // The pointer is only valid for the life of the UserAuthAnswer
  ServerCapabilities* get_server_capabilities()
  {
    return _server_capabilities;
  }

  std::string get_wildcard_impu()
  {
    return _wildcard_impu;
  }

private:
  // This is the result that we'll send on the JSON response
  int32_t _json_result;
  std::string _server_name;
  ServerCapabilities* _server_capabilities;
  std::string _wildcard_impu;
};

class ServerAssignmentAnswer : public HssResponse
{
public:
  ~ServerAssignmentAnswer(){}

  // Takes ownership of the ServerCapabilities* passed in, and will delete it in its
  // destructor
  ServerAssignmentAnswer(ResultCode rc,
                 ChargingAddresses charging_addrs,
                 std::string service_profile) : HssResponse(rc),
    _charging_addrs(charging_addrs),
    _service_profile(service_profile)
  {
  }

  // The pointer is only valid for the life of the UserAuthAnswer
  ChargingAddresses get_charging_addresses()
  {
    return _charging_addrs;
  }

  std::string get_service_profile()
  {
    return _service_profile;
  }

private:
  ChargingAddresses _charging_addrs;
  std::string _service_profile;
};

// Callback typedefs
typedef std::function<void(MultimediaAuthAnswer*)> maa_cb;
typedef std::function<void(UserAuthAnswer*)> uaa_cb;
typedef std::function<void(LocationInfoAnswer*)> lia_cb;
typedef std::function<void(ServerAssignmentAnswer*)> saa_cb;

// Abstract base class that represents connection to the HSS.
// Has 4 methods, to make the 4 different requests to the HSS.
class HssConnection
{
public:
  virtual ~HssConnection() {};

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
                                            MultimediaAuthRequest request) = 0;

  // Send a user auth request to the HSS
  virtual void send_user_auth_request(uaa_cb callback,
                                      UserAuthRequest request) = 0;

  // Send a location info request to the HSS
  virtual void send_location_info_request(lia_cb callback,
                                          LocationInfoRequest request) = 0;

  // Send a server assignment request to the HSS
  virtual void send_server_assignment_request(saa_cb callback,
                                              ServerAssignmentRequest request) = 0;
};

}; // namespace HssConnection
#endif