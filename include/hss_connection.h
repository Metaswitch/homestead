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

// This enum represents the various responses we can get from the HSS.
// The DiameterHssConnection will map from combinations of Diameter result code
// and experimental result code to one of these.
// Not complete
enum HssResultCode
{
    SUCCESS = 0,
    SERVER_UNAVAILABLE = 1,
    NOT_FOUND = 2
};

// Base class that represents a response from the HSS.
// We always have an HssResultCode
class HssResponse
{
public:
  virtual ~HssResponse();

  HssResponse(HssResultCode rc) : _result_code(rc) {};

  HssResultCode get_result()
  {
    return _result_code;
  }

private:
  HssResultCode _result_code;
};

// Subclasses of the HssResponse.
// We will have 4 - one for each of the 4 requests we make to the HSS.
// Each one holds the data that the handlers need to process the response.
class MultimediaAuthResponse : public HssResponse
{
public:
  ~MultimediaAuthResponse()
  {
    if (_auth_vector)
    {
      delete _auth_vector; _auth_vector = NULL;
    }
  }

  MultimediaAuthResponse(int32_t rc,
                         AuthVector* av,
                         std::string scheme) : HssResponse(rc),
    _auth_vector(av),
    _scheme(scheme)
  {
  }

  AuthVector* get_av()
  {
    return _auth_vector;
  }

  std::string get_scheme()
  {
    return _scheme;
  }

private:
  AuthVector* _auth_vector;
  std::string _sip_auth_scheme;
};

// Abstract base class that represents connection to the HSS.
// Has 4 methods, to make the 4 different requests to the HSS.
class HssConnection
{
public:
  virtual ~HssConnection();

  // ---------------------------------------------------------------------------
  // Methods representing communicating with the HSS.
  // Each method takes a callback, which will be called on a different thread
  // (in general) once the request is complete.
  // Ownership of the response objects passed to the callbacks is delegated to
  // the callback target. It's the responsibility of the callback to delete the
  // object when it is done with it.
  // ---------------------------------------------------------------------------

  // Send a multimedia auth request to the HSS
  virtual void send_multimedia_auth_request(std::function<void(MultimediaAuthResponse*)> callback,
                                            std::string impi,
                                            std::string impu,
                                            std::string provided_server_name,
                                            std::string scheme,
                                            std::string authorization);

  // Send a user authorization request to the HSS
 
  virtual void send_user_auth_request(std::function<void(UserAuthResponse*)> callback,
                                      std::string impi,
                                      std::string impu,
                                      std::string visited_network,
                                      std::string authorization_type,
                                      bool emergency);

  // Send a location info request to the HSS
  virtual void send_location_info_request(std::function<void(LocationInfoResponse*)> callback,
                                          std::string impu,
                                          std::string originating,
                                          std::string authorization_type);

  // Send a server assignment request to the HSS
  virtual void send_server_assignment_request(std::function<void(ServerAssignmentResponse*)> callback,
                                              std::string impi,
                                              std::string impu,
                                              std::string provided_server_name,
                                              Cx::ServerAssignmentType type,
                                              bool support_shared_ifcs,
                                              std::string wildcard_impu);
};

#endif
