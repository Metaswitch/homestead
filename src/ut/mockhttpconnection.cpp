#include "mockhttpconnection.hpp"

MockHttpConnection::MockHttpConnection(HttpResolver* resolver) :
  HttpConnection("", false, resolver, SASEvent::HttpLogLevel::PROTOCOL) {};
MockHttpConnection::~MockHttpConnection() {};
