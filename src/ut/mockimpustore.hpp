/**
 * @file mockimpustore.hpp Mock IMPU store.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKIMPUSTORE_HPP__
#define MOCKIMPUSTORE_HPP__

#include "gmock/gmock.h"
#include "impu_store.h"

class MockImpuStore : public ImpuStore
{
public:
  MockImpuStore() : ImpuStore(nullptr) {};
  virtual ~MockImpuStore() {};

  MOCK_METHOD2(set_impu_without_cas, Store::Status(Impu* impu, SAS::TrailId trail));
  MOCK_METHOD2(add_impu, Store::Status(Impu* impu, SAS::TrailId trail));
  MOCK_METHOD2(set_impu, Store::Status(Impu* impu, SAS::TrailId trail));
  MOCK_METHOD2(get_impu, Impu*(const std::string& impu, SAS::TrailId trail));
  MOCK_METHOD2(delete_impu, Store::Status(Impu* impu, SAS::TrailId trail));
  MOCK_METHOD2(set_impi_mapping, Store::Status(ImpiMapping* mapping, SAS::TrailId trail));
  MOCK_METHOD3(get_impi_mapping, Store::Status(const std::string impi, ImpiMapping*& out_mapping, SAS::TrailId trail));
  MOCK_METHOD2(delete_impi_mapping, Store::Status(ImpiMapping* mapping, SAS::TrailId trail));
};

#endif
