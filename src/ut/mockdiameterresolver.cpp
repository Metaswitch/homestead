#include "mockdiameterresolver.hpp"

MockDiameterResolver::MockDiameterResolver() :
  DiameterResolver(NULL, AF_INET) {}
MockDiameterResolver::~MockDiameterResolver() {}
