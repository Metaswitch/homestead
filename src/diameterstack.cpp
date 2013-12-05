#include "diameterstack.h"

#include <freeDiameter/freeDiameter-host.h>
#include <freeDiameter/libfdcore.h>

DiameterStack* DiameterStack::INSTANCE = &DEFAULT_INSTANCE;
DiameterStack DiameterStack::DEFAULT_INSTANCE;

DiameterStack::DiameterStack() : _initialized(false)
{
}

void DiameterStack::initialize()
{
  // Initialize if we haven't already done so.  We don't do this in the
  // constructor because we can't throw exceptions on failure.
  if (!_initialized)
  {
    int rc = fd_core_initialize();
    if (rc != 0)
    {
      throw Exception("fd_core_initialize", rc);
    }
    _initialized = true;
  }
}

void DiameterStack::configure(std::string filename)
{
  initialize();
  int rc = fd_core_parseconf(filename.c_str());
  if (rc != 0)
  {
    throw Exception("fd_core_parseconf", rc);
  }
}

void DiameterStack::start()
{
  initialize();
  int rc = fd_core_start();
  if (rc != 0)
  {
    throw Exception("fd_core_start", rc);
  }
}

void DiameterStack::stop()
{
  if (_initialized)
  {
    int rc = fd_core_shutdown();
    if (rc != 0)
    {
      throw Exception("fd_core_shutdown", rc);
    }
  }
}

void DiameterStack::wait_stopped()
{
  if (_initialized)
  {
    int rc = fd_core_wait_shutdown_complete();
    if (rc != 0)
    {
      throw Exception("fd_core_wait_shutdown_complete", rc);
    }
  }
}
