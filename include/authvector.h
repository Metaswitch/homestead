/**
 * @file authvector.h definitions of different authorization vectors.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef AUTHVECTOR_H__
#define AUTHVECTOR_H__

struct AuthVector
{
  virtual ~AuthVector() {};
};

struct DigestAuthVector : AuthVector
{
  virtual ~DigestAuthVector() {};

  std::string ha1;
  std::string realm;
  std::string qop;
};

struct AKAAuthVector : AuthVector
{
  virtual ~AKAAuthVector() {};

  std::string challenge;
  std::string response;
  std::string crypt_key;
  std::string integrity_key;
  int version = 1;
};

#endif
