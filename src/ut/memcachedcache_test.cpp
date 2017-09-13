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
static const std::string IMPI_3 = "impi3@example.com";
static const std::string IMPI_4 = "impi4@example.com";
static const std::string IMPI_5 = "impi5@example.com";

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

static const std::string SERVICE_PROFILE_3 =
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
      "<PublicIdentity>"
        "<Identity>" + ASSOC_IMPU_5 + "</Identity>"
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

// Allows us to check that progress callbacks are called
class MockProgressCallback
{
public:
  virtual ~MockProgressCallback() {};
  MOCK_METHOD0(progress_callback, void());
};

static MockProgressCallback* _mock_progress_cb;
static progress_callback _progress_callback = []() {
  _mock_progress_cb->progress_callback();
};

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
    _mock_progress_cb = new MockProgressCallback();
  }

  virtual void TearDown() override
  {
    delete _mock_progress_cb;
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

TEST_F(MemcachedCacheTest, GetIrsForImpis)
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

  std::vector<ImplicitRegistrationSet*> irss;

  Store::Status status =
    _memcached_cache->get_implicit_registration_sets_for_impis({IMPI},
                                                               0L,
                                                               irss);


  EXPECT_EQ(Store::Status::OK, status);
  EXPECT_EQ(1, irss.size());

  for (ImplicitRegistrationSet* irs : irss)
  {
    delete irs;
  }
}

TEST_F(MemcachedCacheTest, GetIrsForImpisNotFound)
{
  std::vector<ImplicitRegistrationSet*> irss;

  Store::Status status =
    _memcached_cache->get_implicit_registration_sets_for_impis({IMPI},
                                                               0L,
                                                               irss);


  // We asked for IMPIs, rather than a singular IMPI, so we expect
  // to get back OK, with a zero size, rather than NOT_FOUND.
  EXPECT_EQ(Store::Status::OK, status);
  EXPECT_EQ(0, irss.size());
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

TEST_F(MemcachedCacheTest, GetIrsForImpuLocalStoreViaAssocImpu)
{
  int expiry = time(0) + 1;

  ImpuStore::AssociatedImpu* ai =
    new ImpuStore::AssociatedImpu(ASSOC_IMPU, IMPU, 0L, expiry, _local_store);

  _local_store->set_impu(ai, 0L);

  delete ai;

  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               expiry,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(ASSOC_IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::OK, status);
  ASSERT_NE(nullptr, irs);

  delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpuLocalStoreViaAssocImpuWithoutImpu)
{
  int expiry = time(0) + 1;

  ImpuStore::AssociatedImpu* ai =
    new ImpuStore::AssociatedImpu(ASSOC_IMPU, IMPU, 0L, expiry, _local_store);

  _local_store->set_impu(ai, 0L);

  delete ai;

  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               NO_ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               expiry,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(ASSOC_IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::NOT_FOUND, status);
  ASSERT_EQ(nullptr, irs);

  delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpuLocalStoreViaAssocImpuMissingDefault)
{
  int expiry = time(0) + 1;

  ImpuStore::AssociatedImpu* ai =
    new ImpuStore::AssociatedImpu(ASSOC_IMPU, IMPU, 0L, expiry, _local_store);

  _local_store->set_impu(ai, 0L);

  delete ai;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(ASSOC_IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::NOT_FOUND, status);
  ASSERT_EQ(nullptr, irs);

  delete irs;
}

TEST_F(MemcachedCacheTest, GetIrsForImpuLocalStoreViaAssocImpuToAssocImpu)
{
  int expiry = time(0) + 1;

  ImpuStore::AssociatedImpu* ai =
    new ImpuStore::AssociatedImpu(ASSOC_IMPU,
                                  ASSOC_IMPU_2,
                                  0L,
                                  expiry,
                                  _local_store);

  _local_store->set_impu(ai, 0L);

  delete ai;

  ImpuStore::AssociatedImpu* ai_2 =
    new ImpuStore::AssociatedImpu(ASSOC_IMPU_2,
                                  IMPU,
                                  0L,
                                  expiry,
                                  _local_store);

  _local_store->set_impu(ai_2, 0L);

  delete ai_2;

  ImplicitRegistrationSet* irs = nullptr;

  Store::Status status =
    _memcached_cache->get_implicit_registration_set_for_impu(ASSOC_IMPU,
                                                             0L,
                                                             irs);

  ASSERT_EQ(Store::Status::NOT_FOUND, status);
  ASSERT_EQ(nullptr, irs);

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

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->put_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, PutIrsWithExistingUnrefreshed)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               NO_IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               expiry,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  // IMPI is added, but already has an entry, with the IMPU
  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI, { IMPU }, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  irs->add_associated_impi(IMPI);

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->put_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, PutIrsWithExistingNotRefreshedConflictAssociated)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               expiry,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  irs->set_ims_sub_xml(SERVICE_PROFILE_3);

  // Overwrite the IMPU with a conflicting Assoc IMPU. This will block our
  // request as our data might be out of date.
  {
    ImpuStore::AssociatedImpu* ai =
      new ImpuStore::AssociatedImpu(IMPU, IMPU_2, 1L, expiry, _local_store);

    _local_store->set_impu(ai, 0L);
    delete ai;
  }

  // Errors don't trigger the progress_callback
  EXPECT_EQ(Store::Status::ERROR,
            _memcached_cache->put_implicit_registration_set(irs, _progress_callback, 0L));

  delete irs;
}

TEST_F(MemcachedCacheTest, PutIrsWithExistingRefreshedConflictAssociated)
{
  int expiry = time(0) + 1;

  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               ASSOC_IMPUS,
                               IMPIS,
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               expiry,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  irs->set_ttl(2);
  irs->set_ims_sub_xml(SERVICE_PROFILE_3);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->delete_associated_impi(IMPI);
  irs->delete_associated_impi(IMPI_2);
  irs->add_associated_impi(IMPI_3);

  // Overwrite the IMPU with a conflicting Assoc IMPU. We'll nuke this,
  // as our data is refreshed, and thus more likely to be right
  {
    ImpuStore::AssociatedImpu* ai =
      new ImpuStore::AssociatedImpu(IMPU, IMPU_2, 1L, expiry, _local_store);

    _local_store->set_impu(ai, 0L);
    delete ai;
  }

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->put_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, PutIrsWithExistingRefreshed)
{
  int expiry = time(0) + 1;

  for (ImpuStore* store : { _local_store, _remote_store })
  {
    ImpuStore::DefaultImpu* di =
      new ImpuStore::DefaultImpu(IMPU,
                                 {ASSOC_IMPU, ASSOC_IMPU_2, ASSOC_IMPU_5},
                                 {IMPI, IMPI_2, IMPI_4},
                                 RegistrationState::REGISTERED,
                                 CHARGING_ADDRESSES,
                                 SERVICE_PROFILE,
                                 0L,
                                 expiry,
                                 store);

    store->set_impu(di, 0L);

    delete di;
  }

  // IMPI is deleted entirely
  // IMPI 2 we remove a single mapping from
  // IMPI 3 is added, but already has an entry
  // IMPI 4 is unchanged, but missing the mapping
  // IMPI 5 is added, but already has an entry, with the IMPU

  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI, {IMPU}, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI_2, { IMPU, IMPU_2 }, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI_3, { }, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI_4, { }, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  {
    ImpuStore::ImpiMapping* mapping =
      new ImpuStore::ImpiMapping(IMPI_5, { IMPU }, 0L, expiry);

    _local_store->set_impi_mapping(mapping, 0L);

    delete mapping;
  }

  // IMPU is deleted and present
  // IMPU 2 is deleted but not present
  // IMPU 3 is added and not present
  // IMPU 4 is added and not present
  // IMPU 5 is unchanged and not present

  {
    ImpuStore::AssociatedImpu* ai =
      new ImpuStore::AssociatedImpu(ASSOC_IMPU_2, IMPU, 0L, expiry, _local_store);

    _local_store->set_impu(ai, 0L);
    delete ai;
  }

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  irs->set_ttl(2);
  irs->set_ims_sub_xml(SERVICE_PROFILE_3);
  irs->set_reg_state(RegistrationState::REGISTERED);
  irs->delete_associated_impi(IMPI);
  irs->delete_associated_impi(IMPI_2);
  irs->add_associated_impi(IMPI_3);

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->put_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, DeleteIrsNotAdded)
{
  ImplicitRegistrationSet* irs =
    _memcached_cache->create_implicit_registration_set();

  irs->set_ttl(1);
  irs->set_ims_sub_xml(SERVICE_PROFILE);
  irs->set_reg_state(RegistrationState::REGISTERED);

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->delete_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, DeleteIrsAddedRemote)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               {ASSOC_IMPU, ASSOC_IMPU_2, ASSOC_IMPU_5},
                               {IMPI, IMPI_2, IMPI_4},
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _remote_store);

  _remote_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  std::vector<ImplicitRegistrationSet*> irss = {irs};

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->delete_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

  delete irs;
}

TEST_F(MemcachedCacheTest, DeleteIrsAddedLocalStoreFail)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               {ASSOC_IMPU, ASSOC_IMPU_2, ASSOC_IMPU_5},
                               {IMPI, IMPI_2, IMPI_4},
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _local_store);

  _local_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  std::vector<ImplicitRegistrationSet*> irss = {irs};

  _lls->force_delete_error();

  // The progress_callback is not called on error
  EXPECT_EQ(Store::Status::ERROR,
            _memcached_cache->delete_implicit_registration_sets(irss, _progress_callback, 0L));

  delete irs;
}

TEST_F(MemcachedCacheTest, DeleteIrss)
{
  ImpuStore::DefaultImpu* di =
    new ImpuStore::DefaultImpu(IMPU,
                               {ASSOC_IMPU, ASSOC_IMPU_2, ASSOC_IMPU_5},
                               {IMPI, IMPI_2, IMPI_4},
                               RegistrationState::REGISTERED,
                               CHARGING_ADDRESSES,
                               SERVICE_PROFILE,
                               0L,
                               time(0) + 1,
                               _remote_store);

  _remote_store->set_impu(di, 0L);

  delete di;

  ImplicitRegistrationSet* irs;

  _memcached_cache->get_implicit_registration_set_for_impu(IMPU, 0L, irs);

  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status = _memcached_cache->delete_implicit_registration_set(irs, _progress_callback, 0L);
  EXPECT_EQ(Store::Status::OK, status);

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

TEST_F(MemcachedCacheTest, GetImsSubscriptionNotFound)
{
  ImsSubscription* subscription = nullptr;

  Store::Status status =
    _memcached_cache->get_ims_subscription(IMPI,
                                           0L,
                                           subscription);

  EXPECT_EQ(Store::Status::NOT_FOUND, status);
  EXPECT_EQ(nullptr, subscription);
}

TEST_F(MemcachedCacheTest, GetImsSubscriptionLocal)
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

TEST_F(MemcachedCacheTest, GetImsSubscriptionRemote)
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

  ImpuStore::ImpiMapping* mapping =
    new ImpuStore::ImpiMapping(IMPI, {IMPU}, time(0) + 1);

  _remote_store->set_impi_mapping(mapping, 0L);

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


  EXPECT_CALL(*_mock_progress_cb, progress_callback());
  Store::Status status =
    _memcached_cache->put_ims_subscription(subscription,
                                           _progress_callback,
                                           0L);

  EXPECT_EQ(Store::Status::OK, status);

  delete subscription;
}
