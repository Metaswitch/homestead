/**
 * @file test_utils.hpp Unit test utility functions header file
 *
 * Copyright (C) Metaswitch Networks 2013
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */


#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <string>
#include "authvector.h"

/// Expect that std::list L contains value X.
#define EXPECT_CONTAINED(X, L) \
  EXPECT_TRUE(find((L).begin(), (L).end(), (X)) != (L).end())

/// The directory that contains the unit tests.
extern const std::string UT_DIR;

// Check an AuthVector* matches the given DigestAuthVector properties
MATCHER_P3(IsDigestAndMatches, ha1, realm, qop, std::string(negation ? "isn't" : "is") +
                                                " equal to given DigestAuthVector")
{
  if (arg != NULL)
  {
    DigestAuthVector* digest = dynamic_cast<DigestAuthVector*>(arg);
    if (digest != NULL)
    {
      return ((digest->ha1 == ha1) &&
              (digest->realm == realm) &&
              (digest->qop == qop));
    }
  }
  return false;
}

// Check an AuthVector* matches the given AKAAuthVector properties
MATCHER_P5(IsAKAAndMatches, version, challenge, response, crypt_key, integrity_key,
           std::string(negation ? "isn't" : "is") + " equal to given AKAAuthVector")
{
  if (arg != NULL)
  {
    AKAAuthVector* aka = dynamic_cast<AKAAuthVector*>(arg);
    if (aka != NULL)
    {
      return ((aka->version == version) &&
              (aka->challenge == challenge) &&
              (aka->response == response) &&
              (aka->crypt_key == crypt_key) &&
              (aka->integrity_key == integrity_key));
    }
    else
    {
      *result_listener << "which isn't an AKAAuthVector*";
    }
  }
  else
  {
    *result_listener << "which is NULL";
  }
  return false;
}