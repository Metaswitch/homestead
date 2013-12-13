/**
 * @file cx.h class definition wrapping Cx
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

using namespace Cx;

Dictionary::Dictionary() :
  TGPP("3GPP"),
  CX("Cx"),
  MULTIMEDIA_AUTH_REQUEST("3GPP/Multimedia-Auth-Request"),
  MULTIMEDIA_AUTH_ANSWER("3GPP/Multimedia-Auth-Answer"),
  SUPPORTED_FEATURES("3GPP", "Supported-Features"),
  PUBLIC_IDENTITY("3GPP", "Public-Identity"),
  SIP_AUTH_DATA_ITEM("3GPP", "SIP-Auth-Data-Item"),
  SIP_AUTH_SCHEME("3GPP", "SIP-Authentication-Scheme"),
  SIP_NUMBER_AUTH_ITEMS("3GPP", "SIP-Number-Auth-Items"),
  SERVER_NAME("3GPP", "Server-Name"),
  SIP_DIGEST_AUTHENTICATE("3GPP", "SIP-Digest-Authenticate"),
  CX_DIGEST_HA1("3GPP", "Digest-HA1"),
  CX_DIGEST_REALM("3GPP", "Digest-Realm")
{
}

MultimediaAuthRequest::MultimediaAuthRequest(const Dictionary* dict,
                                             const std::string& dest_realm,
                                             const std::string& dest_host,
                                             const std::string& impi,
                                             const std::string& impu,
                                             const std::string& server_name,
                                             const std::string& sip_auth_scheme) :
                                             Diameter::Message(dict, dict->MULTIMEDIA_AUTH_REQUEST)
{
  add_new_session_id();
  add(Diameter::AVP(dict->AUTH_SESSION_STATE).val_i32(1));
  add(Diameter::AVP(dict->DESTINATION_REALM).val_str(dest_realm));
  add(Diameter::AVP(dict->DESTINATION_HOST).val_str(dest_host));
  add_origin();
  add(Diameter::AVP(dict->USER_NAME).val_str(impi));
  add(Diameter::AVP(dict->PUBLIC_IDENTITY).val_str(impu));
  add(Diameter::AVP(dict->SIP_AUTH_DATA_ITEM).add(
        Diameter::AVP(dict->SIP_AUTH_SCHEME).val_str(sip_auth_scheme)));
  add(Diameter::AVP(dict->SIP_NUMBER_AUTH_ITEMS).val_i32(1));
  add(Diameter::AVP(dict->SERVER_NAME).val_str(server_name));
}

std::string MultimediaAuthRequest::impu() const
{
  std::string impu;
  Diameter::AVP::iterator avps = begin(dict()->USER_NAME);
  if (avps != end())
  {
    impu = avps->val_str();
  }
  return impu;
}

MultimediaAuthAnswer::MultimediaAuthAnswer(const Dictionary* dict,
                                           int result_code) :
                                           Diameter::Message(dict, dict->MULTIMEDIA_AUTH_ANSWER)
{
  add(Diameter::AVP(dict->RESULT_CODE).val_i32(result_code));
}

int MultimediaAuthAnswer::result_code() const
{
  int result_code = 0;
  Diameter::AVP::iterator avps = begin(dict()->RESULT_CODE);
  if (avps != end())
  {
    result_code = avps->val_i32();
  }
  return result_code;
}

std::string MultimediaAuthAnswer::sip_auth_scheme() const
{
  std::string sip_auth_scheme;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_AUTH_SCHEME);
    if (avps != end())
    {
      sip_auth_scheme = avps->val_str();
    }
  }
  return sip_auth_scheme;
}

std::string MultimediaAuthAnswer::digest_ha1() const
{
  std::string digest_ha1;
  Diameter::AVP::iterator avps = begin(((Cx::Dictionary*)dict())->SIP_AUTH_DATA_ITEM);
  if (avps != end())
  {
    avps = avps->begin(((Cx::Dictionary*)dict())->SIP_DIGEST_AUTHENTICATE);
    if (avps != end())
    {
      Diameter::AVP::iterator avps2 = avps->begin(((Cx::Dictionary*)dict())->CX_DIGEST_HA1);
      if (avps2 != end())
      {
        digest_ha1 = avps2->val_str();
      }
      else
      {
        // Some HSSs (in particular OpenIMSCore), use non-3GPP Digest-HA1.  Check for this too.
        avps2 = avps->begin(((Cx::Dictionary*)dict())->DIGEST_HA1);
        if (avps2 != end())
        {
          digest_ha1 = avps2->val_str();
        }
      }
    }
  }
  return digest_ha1;
}
