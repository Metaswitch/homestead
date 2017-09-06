/**
 * @file hss_connection.cpp Abstract base class representing connection to an HSS.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "hss_connection.h"

namespace HssConnection {

std::string HssConnection::_scheme_digest;
std::string HssConnection::_scheme_akav1;
std::string HssConnection::_scheme_akav2;

void HssConnection::configure_auth_schemes(const std::string& scheme_digest,
                                   const std::string& scheme_akav1,
                                   const std::string& scheme_akav2)
{
    _scheme_digest = scheme_digest;
    _scheme_akav1 = scheme_akav1;
    _scheme_akav2 = scheme_akav2;
}

}; // namespace HssConnection