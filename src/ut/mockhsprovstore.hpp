/**
 * @file mockcache.h Mock HTTP stack.
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKHSPROVSTORE_H__
#define MOCKHSPROVSTORE_H__

#include "hsprov_store.h"
#include "mock_cassandra_store.h"

#include "gmock/gmock.h"

static DigestAuthVector mock_digest_av;

class MockHsProvStore : public HsProvStore
{
public:
  MockHsProvStore() {};
  virtual ~MockHsProvStore() {};

  MOCK_METHOD2(do_async, void(CassandraStore::Operation*& op,
                              CassandraStore::Transaction*& trx));

  //
  // Methods that create cache request objects.
  //
  MOCK_METHOD1(create_GetRegData,
               GetRegData*(const std::string& public_id));
  MOCK_METHOD1(create_GetAuthVector,
               GetAuthVector*(const std::string& private_id));
  MOCK_METHOD2(create_GetAuthVector,
               GetAuthVector*(const std::string& private_id,
                              const std::string& public_id));

  // Mock request objects.
  //
  // EXAMPLE USAGE
  // =============
  //
  // To handle receiving a request:
  //
  // -  The Test creates a MockGetRegData object.
  //
  // -  The test sets up MockHsProvStore to expect create_GetRegData().
  //    This checks the parameters and returns the mock object.
  //
  // -  The test sets up MockHsProvStore to expect send() with the mock request passed
  //    in.  This stores the transaction on the mock request.
  //
  // To generate a response.
  //
  // -  For a successful get request, the test expects get_result() to be called
  //    and return the required result.
  //
  // -  The test gets the transaction from the mock request (using get_trx) and
  //    calls on_success or on_failure as appropriate.
  //
  // These classes just implement a default constructor.  This make the mocking
  // code and the UTs easier to write.
  //
  class MockGetRegData : public GetRegData, public MockOperationMixin
  {
    MockGetRegData() : GetRegData("") {}
    virtual ~MockGetRegData() {}

    MOCK_METHOD1(get_xml, void(std::string& xml));
    MOCK_METHOD1(get_charging_addrs, void(ChargingAddresses& charging_addrs));
  };

  class MockGetAuthVector : public GetAuthVector, public MockOperationMixin
  {
    MockGetAuthVector() : GetAuthVector("") {}
    virtual ~MockGetAuthVector() {}

    MOCK_METHOD1(get_result, void(DigestAuthVector& av));
  };
};

#endif
