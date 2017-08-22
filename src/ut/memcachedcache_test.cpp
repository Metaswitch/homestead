/**
 * @file memcachedcache_test.cpp UT for Memcached Cache
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "memcached_cache.h"
#include "fake_implicit_reg_set.h"
#include "test_interposer.hpp"
#include "test_utils.hpp"
#include "localstore.h"

static LocalStore LOCAL_STORE;
static LocalStore LOCAL_STORE_2;

static const ImpuStore IMPU_STORE(&LOCAL_STORE);
static const ImpuStore IMPU_STORE_2(&LOCAL_STORE_2);

static const std::string IMPU = "sip:default_impu@example.com";
static const std::string IMPU_2 = "sip:default_impu_2@example.com";
static const std::string ASSOC_IMPU = "sip:assoc_impu@example.com";
static const std::string ASSOC_IMPU_2 = "tel:+1234567890";
static const std::string ASSOC_IMPU_3 = "sip:assoc_impu_3@example.com";
static const std::string ASSOC_IMPU_4 = "tel:+1234567894";
static const std::string ASSOC_IMPU_5 = "sip:assoc_impu_5@example.com";
static const std::string ASSOC_IMPU_6 = "sip:assoc_impu_6@example.com";


static const std::string IMPI = "impi@example.com";
static const std::string IMPI_2 = "impi2@example.com";

static const std::vector<std::string> NO_ASSOC_IMPUS = {};
static const std::vector<std::string> ASSOC_IMPUS = { ASSOC_IMPU, ASSOC_IMPU_2 };
static const std::vector<std::string> ASSOC_IMPUS_2 = { ASSOC_IMPU_3, ASSOC_IMPU_4 };
static const std::vector<std::string> ASSOC_IMPUS_3 = { ASSOC_IMPU_4, ASSOC_IMPU_5 };

static const std::vector<std::string> NO_IMPIS = {};
static const std::vector<std::string> IMPIS = { IMPI };
static const std::vector<std::string> IMPIS_2 = { IMPI_2 };

static const std::deque<std::string> CCFS = { "ccf" };
static const std::deque<std::string> ECFS = { "ecf" };

static const std::deque<std::string> CCFS_2 = { "ccf2" };
static const std::deque<std::string> ECFS_2 = { "ecf2" };

static const ChargingAddresses NO_CHARGING_ADDRESSES = { {}, {} };
static const ChargingAddresses CHARGING_ADDRESSES = { CCFS, ECFS };
static const ChargingAddresses CHARGING_ADDRESSES_2 = { CCFS_2, ECFS_2 };

static const std::string EMPTY_SERVICE_PROFILE =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<IMSSubscription>"
    "<PrivateID>" + IMPI + "</PrivateID>"
    "<ServiceProfile>"
      "<PublicIdentity>"
        "<Identity>" + IMPU + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "</ServiceProfile>"
    "</IMSSubscription>";


static const std::string SERVICE_PROFILE = 
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<IMSSubscription>"
    "<PrivateID>" + IMPI + "</PrivateID>"
    "<ServiceProfile>"
      "<PublicIdentity>"
        "<Identity>" + IMPU + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<PublicIdentity>"
        "<Identity>" + ASSOC_IMPU + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<PublicIdentity>"
        "<Identity>" + ASSOC_IMPU_2 + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<InitialFilterCriteria>"
        "<Priority>0</Priority>"
        "<TriggerPoint>"
          "<ConditionTypeCNF>0</ConditionTypeCNF>"
          "<SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SessionCase>2</SessionCase><Extension></Extension></SPT>"
          "</TriggerPoint>"
        "<ApplicationServer>"
         "<ServerName>sip:127.0.0.1:5065</ServerName>"
          "<DefaultHandling>0</DefaultHandling>"
          "</ApplicationServer>"
        "</InitialFilterCriteria>"
      "</ServiceProfile>"
    "</IMSSubscription>";

static const std::string SERVICE_PROFILE_2 = 
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<IMSSubscription>"
    "<PrivateID>" + IMPI + "</PrivateID>"
    "<ServiceProfile>"
      "<PublicIdentity>"
        "<Identity>" + IMPU + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<PublicIdentity>"
        "<Identity>" + ASSOC_IMPU_3 + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<PublicIdentity>"
        "<Identity>" + ASSOC_IMPU_4 + "</Identity>"
        "<Extension><IdentityType>0</IdentityType></Extension>"
        "</PublicIdentity>"
      "<InitialFilterCriteria>"
        "<Priority>0</Priority>"
        "<TriggerPoint>"
          "<ConditionTypeCNF>0</ConditionTypeCNF>"
          "<SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SessionCase>2</SessionCase><Extension></Extension></SPT>"
          "</TriggerPoint>"
        "<ApplicationServer>"
         "<ServerName>sip:127.0.0.1:5065</ServerName>"
          "<DefaultHandling>0</DefaultHandling>"
          "</ApplicationServer>"
        "</InitialFilterCriteria>"
      "</ServiceProfile>"
    "</IMSSubscription>";

static const uint64_t CAS = 1L;
static const uint64_t CAS_2 = 2L;


class ControlTimeTest : public testing::Test
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

class MemcachedImsSubscriptionTest : public testing::Test
{
};

TEST_F(MemcachedImsSubscriptionTest, BasicIrsHandling)
{
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  std::vector<ImplicitRegistrationSet*> irss = { irs };

  MemcachedImsSubscription* mis = new MemcachedImsSubscription(irss);

  EXPECT_NE(nullptr, mis->get_irs_for_default_impu(IMPU));
  EXPECT_EQ(1, mis->get_irs().size());

  delete mis;
}

TEST_F(MemcachedImsSubscriptionTest, SetChargingAddresses)
{
  FakeImplicitRegistrationSet* irs = new FakeImplicitRegistrationSet(IMPU);
  std::vector<ImplicitRegistrationSet*> irss = { irs };

  MemcachedImsSubscription* mis = new MemcachedImsSubscription(irss);

  mis->set_charging_addrs(CHARGING_ADDRESSES);

  EXPECT_EQ(CHARGING_ADDRESSES, irs->get_charging_addresses());

  delete mis;
}

class MemcachedImplicitRegistrationSetTest : public ControlTimeTest
{
};

TEST_F(MemcachedImplicitRegistrationSetTest, CreateFromStore)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      nullptr);

  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet(&default_impu);

  EXPECT_EQ(IMPU, mirs->get_default_impu());
  EXPECT_FALSE(mirs->is_refreshed());
  EXPECT_TRUE(mirs->is_existing());
  EXPECT_FALSE(mirs->has_changed());
  EXPECT_EQ(ASSOC_IMPUS, mirs->get_associated_impus());
  EXPECT_EQ(IMPIS, mirs->get_associated_impis());
  EXPECT_EQ(REGISTERED, mirs->get_reg_state());
  EXPECT_EQ(SERVICE_PROFILE, mirs->get_ims_sub_xml());
  EXPECT_EQ(CHARGING_ADDRESSES, mirs->get_charging_addresses());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, CreateNew)
{
  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet();

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetServiceProfileNew)
{
  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet();

  mirs->set_ims_sub_xml(SERVICE_PROFILE);
  EXPECT_EQ(ASSOC_IMPUS, mirs->get_associated_impus());
  EXPECT_EQ(SERVICE_PROFILE, mirs->get_ims_sub_xml());
  ASSERT_TRUE(mirs->has_changed_impus());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetServiceProfileSame)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS_2,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      nullptr);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  mirs.set_ims_sub_xml(SERVICE_PROFILE);
  EXPECT_EQ(ASSOC_IMPUS, mirs.get_associated_impus());
  EXPECT_EQ(ASSOC_IMPUS_2,
            mirs.impus(MemcachedImplicitRegistrationSet::State::DELETED));
  EXPECT_EQ(SERVICE_PROFILE, mirs.get_ims_sub_xml());
  ASSERT_TRUE(mirs.has_changed_impus());

  // Set it back, so we can test undeletion of elements
  mirs.set_ims_sub_xml(SERVICE_PROFILE_2);

  EXPECT_EQ(ASSOC_IMPUS_2, mirs.get_associated_impus());
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetServiceProfileDifferent)
{
  int expiry = time(0) + 1;

  // This case is banned by TS 29.228 Section 6.5.2.1 as the Default IMPU
  // has changed, but we check that the code will handle it as best it can
  ImpuStore::DefaultImpu default_impu(IMPU_2,
                                      NO_ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      nullptr);

  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet(&default_impu);

  mirs->set_ims_sub_xml(SERVICE_PROFILE);
  EXPECT_EQ(ASSOC_IMPUS, mirs->get_associated_impus());
  EXPECT_EQ(SERVICE_PROFILE, mirs->get_ims_sub_xml());
  ASSERT_TRUE(mirs->has_changed_impus());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetRegistrationState)
{
  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet();

  mirs->set_reg_state(RegistrationState::UNREGISTERED);

  ASSERT_EQ(RegistrationState::UNREGISTERED,
            mirs->get_reg_state());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, AddAssociatedImpi)
{
  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet();

  mirs->add_associated_impi(IMPI);

  ASSERT_EQ(IMPIS, mirs->get_associated_impis());
  ASSERT_TRUE(mirs->has_changed_impis());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, DeleteAssociatedImpi)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      nullptr);

  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet(&default_impu);

  mirs->delete_associated_impi(IMPI);

  ASSERT_EQ(0, mirs->get_associated_impis().size());
  ASSERT_TRUE(mirs->has_changed_impis());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetChargingAddresses)
{
  MemcachedImplicitRegistrationSet* mirs =
    new MemcachedImplicitRegistrationSet();

  mirs->set_charging_addresses(CHARGING_ADDRESSES);

  ASSERT_EQ(CHARGING_ADDRESSES,
            mirs->get_charging_addresses());

  delete mirs;
}

TEST_F(MemcachedImplicitRegistrationSetTest, SetTtl)
{
  MemcachedImplicitRegistrationSet mirs;

  mirs.set_ttl(1);

  ASSERT_TRUE(mirs.is_refreshed());
}

TEST_F(MemcachedImplicitRegistrationSetTest, GetImpu)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  ImpuStore::DefaultImpu* got_impu = mirs.get_impu();

  ASSERT_NE(nullptr, got_impu);
  EXPECT_EQ(got_impu->impu, IMPU);
  EXPECT_EQ(got_impu->associated_impus, ASSOC_IMPUS);
  EXPECT_EQ(got_impu->registration_state, RegistrationState::REGISTERED);
  EXPECT_EQ(got_impu->charging_addresses, CHARGING_ADDRESSES);
  EXPECT_EQ(got_impu->impis, IMPIS);
  EXPECT_EQ(got_impu->service_profile, SERVICE_PROFILE);
  EXPECT_EQ(got_impu->cas, 0L);
  EXPECT_EQ(got_impu->expiry, expiry);
  EXPECT_EQ(got_impu->store, nullptr);

  delete got_impu;
}

TEST_F(MemcachedImplicitRegistrationSetTest, GetImpuFromImpu)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  ImpuStore::DefaultImpu default_impu_2(IMPU_2,
                                        NO_ASSOC_IMPUS,
                                        NO_IMPIS,
                                        RegistrationState::UNREGISTERED,
                                        NO_CHARGING_ADDRESSES,
                                        EMPTY_SERVICE_PROFILE,
                                        CAS_2,
                                        expiry + 1,
                                        &IMPU_STORE_2);

  ImpuStore::DefaultImpu* got_impu = mirs.get_impu_from_impu(&default_impu_2);

  ASSERT_NE(nullptr, got_impu);
  EXPECT_EQ(got_impu->impu, IMPU);
  EXPECT_EQ(got_impu->associated_impus, ASSOC_IMPUS);
  EXPECT_EQ(got_impu->registration_state, RegistrationState::REGISTERED);
  EXPECT_EQ(got_impu->charging_addresses, CHARGING_ADDRESSES);
  EXPECT_EQ(got_impu->impis, IMPIS);
  EXPECT_EQ(got_impu->service_profile, SERVICE_PROFILE);
  EXPECT_EQ(got_impu->cas, CAS_2);
  EXPECT_EQ(got_impu->store, &IMPU_STORE_2);
  EXPECT_EQ(got_impu->expiry, expiry);

  delete got_impu;
}

TEST_F(MemcachedImplicitRegistrationSetTest, GetImpuForCorrectStore)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  ImpuStore::DefaultImpu* got_impu = mirs.get_impu_for_store(&IMPU_STORE);

  ASSERT_NE(nullptr, got_impu);
  EXPECT_EQ(got_impu->impu, IMPU);
  EXPECT_EQ(got_impu->associated_impus, ASSOC_IMPUS);
  EXPECT_EQ(got_impu->registration_state, RegistrationState::REGISTERED);
  EXPECT_EQ(got_impu->charging_addresses, CHARGING_ADDRESSES);
  EXPECT_EQ(got_impu->impis, IMPIS);
  EXPECT_EQ(got_impu->service_profile, SERVICE_PROFILE);
  EXPECT_EQ(got_impu->cas, CAS);
  EXPECT_EQ(got_impu->expiry, expiry);
  EXPECT_EQ(got_impu->store, &IMPU_STORE);

  delete got_impu;
}

TEST_F(MemcachedImplicitRegistrationSetTest, GetImpuForDifferentStore)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  ImpuStore::DefaultImpu* got_impu = mirs.get_impu_for_store(&IMPU_STORE_2);

  ASSERT_EQ(nullptr, got_impu);
}

TEST_F(MemcachedImplicitRegistrationSetTest, UpdateFromStoreUnchanged)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  mirs.update_from_impu_from_store(&default_impu);

  EXPECT_EQ(IMPU, mirs.get_default_impu());
  EXPECT_EQ(ASSOC_IMPUS, mirs.get_associated_impus());
  EXPECT_EQ(RegistrationState::REGISTERED, mirs.get_reg_state());
  EXPECT_EQ(CHARGING_ADDRESSES, mirs.get_charging_addresses());
  EXPECT_EQ(IMPIS, mirs.get_associated_impis());
  EXPECT_EQ(SERVICE_PROFILE, mirs.get_ims_sub_xml());
  EXPECT_EQ(1, mirs.get_ttl());
}

TEST_F(MemcachedImplicitRegistrationSetTest, UpdateFromStoreChangedInStore)
{
  int now = time(0);

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      now + 1,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  ImpuStore::DefaultImpu default_impu_2(IMPU,
                                        ASSOC_IMPUS_2,
                                        IMPIS_2,
                                        RegistrationState::UNREGISTERED,
                                        NO_CHARGING_ADDRESSES,
                                        EMPTY_SERVICE_PROFILE,
                                        CAS_2,
                                        now + 2,
                                        &IMPU_STORE_2);

  mirs.update_from_impu_from_store(&default_impu_2);

  EXPECT_EQ(ASSOC_IMPUS_2, mirs.get_associated_impus());
  EXPECT_EQ(RegistrationState::UNREGISTERED, mirs.get_reg_state());
  EXPECT_EQ(NO_CHARGING_ADDRESSES, mirs.get_charging_addresses());
  EXPECT_EQ(IMPIS_2, mirs.get_associated_impis());
  EXPECT_EQ(EMPTY_SERVICE_PROFILE, mirs.get_ims_sub_xml());
  EXPECT_EQ(2, mirs.get_ttl());
}

TEST_F(MemcachedImplicitRegistrationSetTest, UpdateFromStoreChangedInStoreAndByUser)
{
  int now = time(0);

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      now + 1,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  mirs.set_charging_addresses(CHARGING_ADDRESSES_2);
  mirs.set_reg_state(RegistrationState::REGISTERED);
  mirs.set_ims_sub_xml(SERVICE_PROFILE_2);
  mirs.delete_associated_impi(IMPI);
  mirs.add_associated_impi(IMPI_2);
  mirs.set_ttl(3);

  ImpuStore::DefaultImpu default_impu_2(IMPU,
                                        ASSOC_IMPUS_3,
                                        NO_IMPIS,
                                        RegistrationState::UNREGISTERED,
                                        NO_CHARGING_ADDRESSES,
                                        EMPTY_SERVICE_PROFILE,
                                        CAS_2,
                                        now + 2,
                                        &IMPU_STORE_2);

  mirs.update_from_impu_from_store(&default_impu_2);

  EXPECT_EQ(ASSOC_IMPUS_2, mirs.get_associated_impus());
  EXPECT_EQ(RegistrationState::REGISTERED, mirs.get_reg_state());
  EXPECT_EQ(CHARGING_ADDRESSES_2, mirs.get_charging_addresses());
  EXPECT_EQ(IMPIS_2, mirs.get_associated_impis());
  EXPECT_EQ(SERVICE_PROFILE_2, mirs.get_ims_sub_xml());
  EXPECT_EQ(3, mirs.get_ttl());
}

TEST_F(MemcachedImplicitRegistrationSetTest, DeleteImpus)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  mirs.delete_assoc_impus();

  ASSERT_EQ(NO_ASSOC_IMPUS, mirs.get_associated_impus());
  ASSERT_EQ(NO_ASSOC_IMPUS,
            mirs.impus(MemcachedImplicitRegistrationSet::State::UNCHANGED));
  ASSERT_EQ(ASSOC_IMPUS,
            mirs.impus(MemcachedImplicitRegistrationSet::State::DELETED));
}

TEST_F(MemcachedImplicitRegistrationSetTest, DeleteImpis)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu default_impu(IMPU,
                                      ASSOC_IMPUS,
                                      IMPIS,
                                      RegistrationState::REGISTERED,
                                      CHARGING_ADDRESSES,
                                      SERVICE_PROFILE,
                                      CAS,
                                      expiry,
                                      &IMPU_STORE);

  MemcachedImplicitRegistrationSet mirs(&default_impu);

  mirs.delete_impis();

  ASSERT_EQ(NO_IMPIS, mirs.get_associated_impis());
  ASSERT_EQ(NO_IMPIS,
            mirs.impis(MemcachedImplicitRegistrationSet::State::UNCHANGED));
  ASSERT_EQ(IMPIS,
            mirs.impis(MemcachedImplicitRegistrationSet::State::DELETED));
}


class MemcachedCacheTest : public ControlTimeTest
{
public:
  virtual void SetUp() override
  {
    _lls = new LocalStore();
    _local_store = new ImpuStore(_lls);
    _rls = new LocalStore();
    _remote_store = new ImpuStore(_rls);
    _remote_stores = { _remote_store };
    _memcached_cache = new MemcachedCache(_local_store,
                                          _remote_stores);
  }

  virtual void TearDown() override
  {
    delete _memcached_cache;
    delete _remote_store;
    delete _rls;
    delete _local_store;
    delete _lls;
  }

private:
  LocalStore* _lls;
  ImpuStore* _local_store;
  LocalStore* _rls;
  ImpuStore* _remote_store;
  std::vector<ImpuStore*> _remote_stores;

  MemcachedCache* _memcached_cache;
};

TEST_F(MemcachedCacheTest, Constructor)
{
  EXPECT_NE(nullptr, _memcached_cache);
}

TEST_F(MemcachedCacheTest, CreateIrs)
{
 ImplicitRegistrationSet* irs = _memcached_cache->create_implicit_registration_set();
 EXPECT_NE(nullptr, irs);
 delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpuNotFound)
{
  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::NOT_FOUND, status);
  ASSERT_EQ(nullptr, irs);
}

TEST_F(MemcachedCacheTest, GetIrsForImpuLocalStore)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::OK, status);
  ASSERT_NE(nullptr, irs);

  delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpuRemoteStore)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _remote_store);

  _remote_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::OK, status);
  ASSERT_NE(nullptr, irs);

  delete irs;
}

TEST_F(MemcachedCacheTest, PutIrs)
{
  ImplicitRegistrationSet* irs =
    _memcached_cache->create_implicit_registration_set();

  irs->set_ttl(1);
  irs->set_ims_sub_xml(SERVICE_PROFILE);
  irs->set_reg_state(RegistrationState::REGISTERED);

  EXPECT_EQ(Store::Status::OK,
            _memcached_cache->put_implicit_registration_set(irs, 0L));

  delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpus)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _remote_store);

  _remote_store->set_impu(di, 0L);

  delete di;

  std::vector<ImplicitRegistrationSet*> irss;

  Store::Status status =
    _memcached_cache->get_implicit_registration_sets_for_impus({IMPU},
                                                               0L,
                                                               irss);

  EXPECT_EQ(Store::Status::OK, status);
  EXPECT_EQ(1, irss.size());

  for (ImplicitRegistrationSet* irs : irss)
  {
    delete irs;
  }
}

TEST_F(MemcachedCacheTest, GetImsSubscription)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImpuStore::ImpiMapping* mapping =
    new ImpuStore::ImpiMapping(IMPI, {IMPU}, time(0) + 1);

  _local_store->set_impi_mapping(mapping, 0L);
  
  delete mapping;

  ImsSubscription* subscription;

  Store::Status status =
    _memcached_cache->get_ims_subscription(IMPI,
                                           0L,
                                           subscription);

  EXPECT_EQ(Store::Status::OK, status);
  EXPECT_NE(nullptr, subscription);

  delete subscription;
}

TEST_F(MemcachedCacheTest, PutImsSubscription)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImpuStore::ImpiMapping* mapping =
    new ImpuStore::ImpiMapping(IMPI, {IMPU}, time(0) + 1);

  _local_store->set_impi_mapping(mapping, 0L);
  
  delete mapping;

  ImsSubscription* subscription;

  _memcached_cache->get_ims_subscription(IMPI,
                                         0L,
                                         subscription);

  subscription->set_charging_addrs(CHARGING_ADDRESSES_2);

  Store::Status status =
    _memcached_cache->put_ims_subscription(subscription,
                                           0L);

  EXPECT_EQ(Store::Status::OK, status);

  delete subscription;
}
