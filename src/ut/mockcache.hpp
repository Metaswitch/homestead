/**
 * @file mockcache.h Mock HTTP stack.
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

#ifndef MOCKCACHE_H__
#define MOCKCACHE_H__

#include "cache.h"
#include "mock_cassandra_store.h"

#include "gmock/gmock.h"

static DigestAuthVector mock_digest_av;

class MockCache : public Cache
{
public:
  MockCache() {};
  virtual ~MockCache() {};

  MOCK_METHOD2(do_async, void(CassandraStore::Operation*& op,
                              CassandraStore::Transaction*& trx));

  //
  // Methods that create cache request objects.
  //
  MOCK_METHOD3(create_PutRegData,
               PutRegData*(const std::string& public_id,
                           const int64_t timestamp,
                           const int32_t ttl));
  MOCK_METHOD3(create_PutRegData,
               PutRegData*(const std::vector<std::string>& public_ids,
                           const int64_t timestamp,
                           const int32_t ttl));
  MOCK_METHOD4(create_PutAssociatedPrivateID,
               PutAssociatedPrivateID*(const std::vector<std::string>& impus,
                                       const std::string& impi,
                                       const int64_t timestamp,
                                       const int32_t ttl));
  MOCK_METHOD4(create_PutAssociatedPublicID,
               PutAssociatedPublicID*(const std::string& private_id,
                                      const std::string& assoc_public_id,
                                      const int64_t timestamp,
                                      const int32_t ttl));
  MOCK_METHOD4(create_PutAuthVector,
               PutAuthVector* (const std::string& private_id,
                               const DigestAuthVector& auth_vector,
                               const int64_t timestamp,
                               const int32_t ttl));
  MOCK_METHOD1(create_GetRegData,
               GetRegData*(const std::string& public_id));
  MOCK_METHOD1(create_GetAssociatedPublicIDs,
               GetAssociatedPublicIDs*(const std::string& private_id));
  MOCK_METHOD1(create_GetAssociatedPublicIDs,
               GetAssociatedPublicIDs*(const std::vector<std::string>& private_ids));
  MOCK_METHOD1(create_GetAssociatedPrimaryPublicIDs,
               GetAssociatedPrimaryPublicIDs*(const std::string& private_id));
  MOCK_METHOD1(create_GetAssociatedPrimaryPublicIDs,
               GetAssociatedPrimaryPublicIDs*(const std::vector<std::string>& private_ids));
  MOCK_METHOD1(create_GetAuthVector,
               GetAuthVector*(const std::string& private_id));
  MOCK_METHOD2(create_GetAuthVector,
               GetAuthVector*(const std::string& private_id,
                              const std::string& public_id));
  MOCK_METHOD3(create_DeletePublicIDs,
               DeletePublicIDs*(const std::string& public_id,
                                const std::vector<std::string>& impis,
                                int64_t timestamp));
  MOCK_METHOD3(create_DeletePublicIDs,
               DeletePublicIDs*(const std::vector<std::string>& public_ids,
                                const std::vector<std::string>& impis,
                                int64_t timestamp));
  MOCK_METHOD2(create_DeletePrivateIDs,
               DeletePrivateIDs*(const std::string& private_id,
                                 int64_t timestamp));
  MOCK_METHOD2(create_DeletePrivateIDs,
               DeletePrivateIDs*(const std::vector<std::string>& private_ids,
                                 int64_t timestamp));
  MOCK_METHOD2(create_DeleteIMPIMapping,
               DeleteIMPIMapping*(const std::vector<std::string>& private_ids,
                                  int64_t timestamp));
  MOCK_METHOD3(create_DissociateImplicitRegistrationSetFromImpi,
               DissociateImplicitRegistrationSetFromImpi*(const std::vector<std::string>& impus,
                                                          const std::string& impi,
                                                          int64_t timestamp));
  MOCK_METHOD3(create_DissociateImplicitRegistrationSetFromImpi,
               DissociateImplicitRegistrationSetFromImpi*(const std::vector<std::string>& impus,
                                                          const std::vector<std::string>& impis,
                                                          int64_t timestamp));
  MOCK_METHOD0(create_ListImpus, ListImpus*());

  // Mock request objects.
  //
  // EXAMPLE USAGE
  // =============
  //
  // To handle receiving a request:
  //
  // -  The Test creates a MockGetRegData object.
  //
  // -  The test sets up MockCache to expect create_GetRegData().
  //    This checks the parameters and returns the mock object.
  //
  // -  The test sets up MockCache to expect send() with the mock request passed
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
  class MockPutRegData : public PutRegData, public MockOperationMixin
  {
    MockPutRegData() : PutRegData("", 0, 0) {}
    virtual ~MockPutRegData() {}

    MOCK_METHOD1(with_xml, PutRegData&(const std::string& xml));
    MOCK_METHOD1(with_reg_state, PutRegData&(const RegistrationState reg_state));
    MOCK_METHOD1(with_associated_impis, PutRegData&(const std::vector<std::string>& impis));
    MOCK_METHOD1(with_charging_addrs, PutRegData&(const ChargingAddresses& charging_addrs));
  };

  class MockPutAssociatedPrivateID : public PutAssociatedPrivateID, public MockOperationMixin
  {
    MockPutAssociatedPrivateID() : PutAssociatedPrivateID({}, "", 0, 0) {}
    virtual ~MockPutAssociatedPrivateID() {}
  };

  class MockPutAssociatedPublicID : public PutAssociatedPublicID, public MockOperationMixin
  {
    MockPutAssociatedPublicID() : PutAssociatedPublicID("", "", 0, 0) {}
    virtual ~MockPutAssociatedPublicID() {}
  };

  class MockPutAuthVector : public PutAuthVector, public MockOperationMixin
  {
    MockPutAuthVector() : PutAuthVector("", mock_digest_av, 0, 0) {}
    virtual ~MockPutAuthVector() {}
  };

  class MockGetRegData : public GetRegData, public MockOperationMixin
  {
    MockGetRegData() : GetRegData("") {}
    virtual ~MockGetRegData() {}

    MOCK_METHOD2(get_xml, void(std::string& xml, int& ttl));
    MOCK_METHOD2(get_registration_state, void(RegistrationState& state, int& ttl));
    MOCK_METHOD1(get_associated_impis, void(std::vector<std::string>& associated_impis));
    MOCK_METHOD1(get_charging_addrs, void(ChargingAddresses& charging_addrs));
  };

  class MockGetAssociatedPublicIDs : public GetAssociatedPublicIDs, public MockOperationMixin
  {
    MockGetAssociatedPublicIDs() : GetAssociatedPublicIDs("") {}
    virtual ~MockGetAssociatedPublicIDs() {}

    MOCK_METHOD1(get_result, void(std::vector<std::string>& public_ids));
  };

  class MockGetAssociatedPrimaryPublicIDs : public GetAssociatedPrimaryPublicIDs, public MockOperationMixin
  {
    MockGetAssociatedPrimaryPublicIDs() : GetAssociatedPrimaryPublicIDs("") {}
    virtual ~MockGetAssociatedPrimaryPublicIDs() {}

    MOCK_METHOD1(get_result, void(std::vector<std::string>& public_ids));
  };

  class MockGetAuthVector : public GetAuthVector, public MockOperationMixin
  {
    MockGetAuthVector() : GetAuthVector("") {}
    virtual ~MockGetAuthVector() {}

    MOCK_METHOD1(get_result, void(DigestAuthVector& av));
  };

  class MockDeletePublicIDs : public DeletePublicIDs, public MockOperationMixin
  {
    MockDeletePublicIDs() : DeletePublicIDs("", {}, 0) {}
    virtual ~MockDeletePublicIDs() {}
  };

  class MockDeletePrivateIDs : public DeletePrivateIDs, public MockOperationMixin
  {
    MockDeletePrivateIDs() : DeletePrivateIDs("", 0) {}
    virtual ~MockDeletePrivateIDs() {}
  };

  class MockDeleteIMPIMapping : public DeleteIMPIMapping, public MockOperationMixin
  {
    MockDeleteIMPIMapping() : DeleteIMPIMapping({}, 0) {}
    virtual ~MockDeleteIMPIMapping() {}
  };

  class MockDissociateImplicitRegistrationSetFromImpi : public DissociateImplicitRegistrationSetFromImpi, public MockOperationMixin
  {
    MockDissociateImplicitRegistrationSetFromImpi() : DissociateImplicitRegistrationSetFromImpi({}, "", 0) {}
    virtual ~MockDissociateImplicitRegistrationSetFromImpi() {}
  };

  class MockListImpus : public ListImpus, public MockOperationMixin
  {
    MockListImpus() : ListImpus() {}
    virtual ~MockListImpus() {}
    MOCK_METHOD0(get_impus_ref, std::vector<std::string>&());
  };
};

#endif
