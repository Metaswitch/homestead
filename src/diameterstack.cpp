/**
 * @file diameterstack.cpp class implementation wrapping freeDiameter
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
