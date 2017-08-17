/**
 * @file impu_store_test.cpp UT for IMPU Store
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "impu_store.h"
#include "localstore.h"
#include "test_interposer.hpp"
#include "test_utils.hpp"

static const std::string IMPU = "sip:impu@example.com";
static const std::vector<std::string> NO_ASSOCIATED_IMPUS;
static const std::vector<std::string> IMPI = { "impi@example.com" };

// Not valid - just dummy data for testing.
static const std::string SERVICE_PROFILE = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><ServiceProfile></ServiceProfile>";

class ImpuStoreTest : public testing::Test
{
  static void SetUpTestCase()
  {
    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();
  }
};

TEST_F(ImpuStoreTest, Constructor)
{
  LocalStore* local_store = new LocalStore();
  ImpuStore* impu_store = new ImpuStore(local_store);
  delete impu_store;
  delete local_store;
}

TEST_F(ImpuStoreTest, EncodeDecodeSmallVarByte)
{
  std::string data;
  encode_varbyte(2, data);

  // 1 byte should take up 1 byte
  EXPECT_EQ(1, data.size());

  size_t offset = 0;

  EXPECT_EQ(2, decode_varbyte(data, offset));
}

TEST_F(ImpuStoreTest, SetDefaultImpu)
{
  LocalStore* local_store = new LocalStore();
  ImpuStore* impu_store = new ImpuStore(local_store);

  int now = time(0);

  ImpuStore::DefaultImpu* default_impu =
    new ImpuStore::DefaultImpu(IMPU,
                               NO_ASSOCIATED_IMPUS,
                               IMPI,
                               RegistrationState::REGISTERED,
                               SERVICE_PROFILE,
                               0L,
                               now);

  EXPECT_EQ(Store::Status::OK,
            impu_store->set_impu(default_impu, 0));

  delete default_impu;
  delete impu_store;
  delete local_store;
}

TEST_F(ImpuStoreTest, GetDefaultImpu)
{
  LocalStore* local_store = new LocalStore();
  ImpuStore* impu_store = new ImpuStore(local_store);

  int now = time(0);

  ImpuStore::DefaultImpu* default_impu =
    new ImpuStore::DefaultImpu(IMPU,
                               NO_ASSOCIATED_IMPUS,
                               IMPI,
                               RegistrationState::REGISTERED,
                               SERVICE_PROFILE,
                               0L,
                               now);
  impu_store->set_impu(default_impu, 0);

  delete default_impu;

  ImpuStore::Impu* impu = impu_store->get_impu(IMPU.c_str(), 0L);

  ASSERT_NE(nullptr, impu);

  ASSERT_TRUE(impu->is_default_impu());
  ASSERT_EQ(now, impu->expiry);

  ImpuStore::DefaultImpu* got_default_impu =
    dynamic_cast<ImpuStore::DefaultImpu*>(impu);

  ASSERT_NE(nullptr, got_default_impu);

  ASSERT_EQ(IMPI, got_default_impu->impis);
  ASSERT_EQ(NO_ASSOCIATED_IMPUS, got_default_impu->associated_impus);
  ASSERT_EQ(SERVICE_PROFILE, got_default_impu->service_profile);

  delete got_default_impu;
  delete impu_store;
  delete local_store;
}
