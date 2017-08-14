/**
 * @file hsprov_store.h class definition of a cassandra-backed store.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSPROV_STORE_H__
#define HSPROV_STORE_H__

#include "cassandra_store.h"
#include "reg_state.h"
#include "charging_addresses.h"
#include "authvector.h"

class HsProvStore : public CassandraStore::Store
{
public:
  virtual ~HsProvStore();

  /// @return the singleton cache instance.
  static inline HsProvStore* get_instance() { return INSTANCE; }

private:
  // Singleton variables.
  static HsProvStore* INSTANCE;
  static HsProvStore DEFAULT_INSTANCE;

protected:
  // The constructors and assignment operation are protected to prevent multiple
  // instances of the class from being created.
  HsProvStore();
  HsProvStore(HsProvStore const&);
  void operator=(HsProvStore const&);

public:
  //
  // Operations
  //

  class GetRegData : public CassandraStore::HAOperation
  {
  public:
    /// Get the IMS subscription XML for a public identity.
    ///
    /// @param public_id the public identity.
    GetRegData(const std::string& public_id);
    virtual ~GetRegData();

    /// Access the result of the request.
    ///
    /// @param xml the IMS subscription XML document.
    virtual void get_xml(std::string& xml);

    /// Access the result of the request.
    ///
    /// @param charging_addrs the charging addresses for this public identity.
    virtual void get_charging_addrs(ChargingAddresses& charging_addrs);

    struct Result
    {
      std::string xml;
      ChargingAddresses charging_addrs;
    };
    virtual void get_result(Result& result);

  protected:
    // Request parameters.
    std::string _public_id;

    // Result.
    std::string _xml;
    ChargingAddresses _charging_addrs;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual GetRegData* create_GetRegData(const std::string& public_id)
  {
    return new GetRegData(public_id);
  }

  class GetAuthVector : public CassandraStore::HAOperation
  {
  public:
    /// Get the auth vector of a private ID.
    ///
    /// @param private_id the private ID.
    GetAuthVector(const std::string& private_id);

    /// Get the auth vector of a private ID that has an associated public ID.
    ///
    /// If the private ID exists but the public ID is not associated with it,
    /// the on_failure will be called with a NOT_FOUND result.
    ///
    /// @param private_id the private ID.
    GetAuthVector(const std::string& private_id,
                  const std::string& public_id);
    virtual ~GetAuthVector();

    /// Access the result of the request.
    ///
    /// @param auth_vector the digest auth vector for the private ID.
    virtual void get_result(DigestAuthVector& auth_vector);

  protected:
    // Request parameters.
    std::string _private_id;
    std::string _public_id;

    // Result.
    DigestAuthVector _auth_vector;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual GetAuthVector* create_GetAuthVector(const std::string& private_id)
  {
    return new GetAuthVector(private_id);
  }

  virtual GetAuthVector* create_GetAuthVector(const std::string& private_id,
                                              const std::string& public_id)
  {
    return new GetAuthVector(private_id, public_id);
  }
};

#endif
