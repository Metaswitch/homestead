/**
 * @file cassandracache.h class definition of a cassandra-backed cache.
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
#ifndef CACHE_H_
#define CACHE_H_

#include "cassandra_store.h"
#include "reg_state.h"
#include "charging_addresses.h"
#include "authvector.h"

class Cache : public CassandraStore::Store
{
public:
  virtual ~Cache();

  /// @return the singleton cache instance.
  static inline Cache* get_instance() { return INSTANCE; }

private:
  // Singleton variables.
  static Cache* INSTANCE;
  static Cache DEFAULT_INSTANCE;

protected:
  // The constructors and assignment operation are protected to prevent multiple
  // instances of the class from being created.
  Cache();
  Cache(Cache const&);
  void operator=(Cache const&);

public:
  //
  // Operations
  //

  /// @class PutRegData write the registration data for some number of public IDs.
  class PutRegData : public CassandraStore::Operation
  {
  public:
    /// Constructors. Stores off the public IDs that we're changing, the
    /// timestamp, and the TTL. Then creates a blank PutRegData object.
    PutRegData(const std::string& public_id,
               const int64_t timestamp,
               const int32_t ttl = 0);
    PutRegData(const std::vector<std::string>& public_ids,
               const int64_t timestamp,
               const int32_t ttl = 0);

    /// Methods for adding various bits of registration information to store for
    /// the specified public IDs. These APIs conform to the fluent interface
    /// pattern.

    /// @param xml - The subscription XML.
    /// @returns - A reference to this PutRegData object.
    virtual PutRegData& with_xml(const std::string& xml);

    /// @param reg_state - The new registration state.
    /// @returns - A reference to this PutRegData object.
    virtual PutRegData& with_reg_state(const RegistrationState reg_state);

    /// @param impis - The associated IMPIs.
    /// @returns - A reference to this PutRegData object.
    virtual PutRegData& with_associated_impis(const std::vector<std::string>& impis);

    /// @param charging_addrs - The charging addresses.
    /// @returns - A reference to this PutRegData object.
    virtual PutRegData& with_charging_addrs(const ChargingAddresses& charging_addrs);

    virtual ~PutRegData();

  protected:
    std::vector<std::string> _public_ids;
    int64_t _timestamp;
    int32_t _ttl;

    std::map<std::string, std::string> _columns;
    std::vector<CassandraStore::RowColumns> _to_put;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual PutRegData* create_PutRegData(const std::string& public_id,
                                        const int64_t timestamp,
                                        const int32_t ttl = 0)
  {
    return new PutRegData(public_id,
                          timestamp,
                          ttl);
  }

  virtual PutRegData* create_PutRegData(const std::vector<std::string>& public_ids,
                                        const int64_t timestamp,
                                        const int32_t ttl = 0)
  {
    return new PutRegData(public_ids,
                          timestamp,
                          ttl);
  }

  class PutAssociatedPrivateID : public CassandraStore::Operation
  {
  public:
    /// Give a set of public IDs (representing an implicit registration set) an associated private ID.
    ///
    /// @param impus The public IDs.
    /// @param impi the private ID to associate with them.
    PutAssociatedPrivateID(const std::vector<std::string>& impus,
                           const std::string& impi,
                           const int64_t timestamp,
                           const int32_t ttl = 0);
    virtual ~PutAssociatedPrivateID();

  protected:
    std::vector<std::string> _impus;
    std::string _impi;
    int64_t _timestamp;
    int32_t _ttl;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual PutAssociatedPrivateID* create_PutAssociatedPrivateID(
    const std::vector<std::string>& impus,
    const std::string& impi,
    const int64_t timestamp,
    const int32_t ttl = 0)
  {
    return new PutAssociatedPrivateID(impus, impi, timestamp, ttl);
  }

  class PutAssociatedPublicID : public CassandraStore::Operation
  {
  public:
    /// Give a private_id an associated public ID.
    ///
    /// @param private_id the private ID in question.
    /// @param assoc_public_id the public ID to associate with it.
    PutAssociatedPublicID(const std::string& private_id,
                          const std::string& assoc_public_id,
                          const int64_t timestamp,
                          const int32_t ttl = 0);
    virtual ~PutAssociatedPublicID();

  protected:
    std::string _private_id;
    std::string _assoc_public_id;
    int64_t _timestamp;
    int32_t _ttl;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual PutAssociatedPublicID* create_PutAssociatedPublicID(const std::string& private_id,
                                                              const std::string& assoc_public_id,
                                                              const int64_t timestamp,
                                                              const int32_t ttl = 0)
  {
    return new PutAssociatedPublicID(private_id, assoc_public_id, timestamp, ttl);
  }

  class PutAuthVector : public CassandraStore::Operation
  {
  public:
    /// Set the authorization vector used for a private ID.
    ///
    /// This request does not support AKAAuthVectors (it is not possible to
    /// cache these due to the sequence numbers they contain).
    ///
    /// @param private_id the private ID in question.
    /// @param auth_vector the auth vector to store.
    PutAuthVector(const std::string& private_id,
                  const DigestAuthVector& auth_vector,
                  const int64_t timestamp,
                  const int32_t ttl = 0);
    virtual ~PutAuthVector();

  protected:
    std::vector<std::string> _private_ids;
    DigestAuthVector _auth_vector;
    int64_t _timestamp;
    int32_t _ttl;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual PutAuthVector* create_PutAuthVector(const std::string& private_id,
                                              const DigestAuthVector& auth_vector,
                                              const int64_t timestamp,
                                              const int32_t ttl = 0)
  {
    return new PutAuthVector(private_id, auth_vector, timestamp, ttl);
  }

  class GetRegData : public CassandraStore::Operation
  {
  public:
    /// Get the IMS subscription XML for a public identity.
    ///
    /// @param public_id the public identity.
    GetRegData(const std::string& public_id);
    virtual ~GetRegData();
    virtual void get_result(std::pair<RegistrationState, std::string>& result);

    /// Access the result of the request.
    ///
    /// @param xml the IMS subscription XML document.
    /// @param ttl the column's time-to-live.
    virtual void get_xml(std::string& xml, int32_t& ttl);

    /// Access the result of the request.
    ///
    /// @param reg_state the registration state value.
    /// @param ttl the column's time-to-live.
    virtual void get_registration_state(RegistrationState& reg_state, int32_t& ttl);

    /// Access the result of the request.
    ///
    /// @param impis The IMPIs associated with this IMS Subscription
    virtual void get_associated_impis(std::vector<std::string>& impis);

    /// Access the result of the request.
    ///
    /// @param charging_addrs the charging addresses for this public identity.
    virtual void get_charging_addrs(ChargingAddresses& charging_addrs);

    struct Result
    {
      std::string xml;
      RegistrationState state;
      std::vector<std::string> impis;
      ChargingAddresses charging_addrs;
    };
    virtual void get_result(Result& result);

  protected:
    // Request parameters.
    std::string _public_id;

    // Result.
    std::string _xml;
    RegistrationState _reg_state;
    int32_t _xml_ttl;
    int32_t _reg_state_ttl;
    std::vector<std::string> _impis;
    ChargingAddresses _charging_addrs;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual GetRegData* create_GetRegData(const std::string& public_id)
  {
    return new GetRegData(public_id);
  }

  /// Get all the public IDs that are associated with one or more
  /// private IDs.

  // Only used when subscribers are locally provisioned - for the
  // database operation that stores associations between IMPIs and
  // primary public IDs for use in handling RTRs, see GetAssociatedPrimaryPublicIDs.

  class GetAssociatedPublicIDs : public CassandraStore::Operation
  {
  public:
    /// Get the public Ids that are associated with a single private ID.
    ///
    /// @param private_id the private ID.
    GetAssociatedPublicIDs(const std::string& private_id);

    /// Get the public Ids that are associated with multiple private IDs.
    ///
    /// @param private_ids a vector of private IDs.
    GetAssociatedPublicIDs(const std::vector<std::string>& private_ids);
    virtual ~GetAssociatedPublicIDs();

    /// Access the result of the request.
    ///
    /// @param public_ids A vector of public IDs associated with the private ID.
    virtual void get_result(std::vector<std::string>& public_ids);

  protected:
    // Request parameters.
    std::vector<std::string> _private_ids;

    // Result.
    std::vector<std::string> _public_ids;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual GetAssociatedPublicIDs* create_GetAssociatedPublicIDs(const std::string& private_id)
  {
    return new GetAssociatedPublicIDs(private_id);
  }

  virtual GetAssociatedPublicIDs* create_GetAssociatedPublicIDs(
    const std::vector<std::string>& private_ids)
  {
    return new GetAssociatedPublicIDs(private_ids);
  }

  /// Retrieves the primary public IDs which a particular IMPI has been
  /// used to authenticate, by querying the "impi_mapping" table.

  /// Note that this operates on the "impi_mapping" table (storing data
  /// needed to handle Registration-Termination-Requests, and only used
  /// when we have a HSS) not the "impi" table (storing the SIP digest
  /// HA1 and all the public IDs associated with this IMPI, and only
  /// used when subscribers are locally provisioned).
  class GetAssociatedPrimaryPublicIDs : public CassandraStore::Operation
  {
  public:
    /// Get the primary public Ids that are associated with a single private ID.
    ///
    /// @param private_id the private ID.
    GetAssociatedPrimaryPublicIDs(const std::string& private_id);
    GetAssociatedPrimaryPublicIDs(const std::vector<std::string>& private_ids);
    virtual ~GetAssociatedPrimaryPublicIDs() {};

    /// Access the result of the request.
    ///
    /// @param public_ids A vector of public IDs associated with the private ID.
    virtual void get_result(std::vector<std::string>& public_ids);

  protected:
    // Request parameters.
    std::vector<std::string> _private_ids;

    // Result.
    std::vector<std::string> _public_ids;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual GetAssociatedPrimaryPublicIDs* create_GetAssociatedPrimaryPublicIDs(
    const std::string& private_id)
  {
    return new GetAssociatedPrimaryPublicIDs(private_id);
  }

  virtual GetAssociatedPrimaryPublicIDs* create_GetAssociatedPrimaryPublicIDs(
    const std::vector<std::string>& private_ids)
  {
    return new GetAssociatedPrimaryPublicIDs(private_ids);
  }

  class GetAuthVector : public CassandraStore::Operation
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

  class DeletePublicIDs : public CassandraStore::Operation
  {
  public:
    /// Delete several public IDs from the cache, and also dissociate
    /// the primary public ID (the first one in the vector) from the
    /// given IMPIs.
    ///
    /// @param public_ids the public IDs to delete.
    /// @param impis the IMPIs to dissociate from this implicit
    ///              registration set.
    DeletePublicIDs(const std::vector<std::string>& public_ids,
                    const std::vector<std::string>& impis,
                    int64_t timestamp);

    /// Delete a public ID from the cache, and also dissociate
    /// it from the given IMPIs.
    ///
    /// @param public_id the public ID to delete.
    /// @param impis the IMPIs to dissociate from this implicit
    ///              registration set.
    DeletePublicIDs(const std::string& public_id,
                    const std::vector<std::string>& impis,
                    int64_t timestamp);

    virtual ~DeletePublicIDs();

  protected:
    std::vector<std::string> _public_ids;
    std::vector<std::string> _impis;
    int64_t _timestamp;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual DeletePublicIDs* create_DeletePublicIDs(
    const std::vector<std::string>& public_ids,
    const std::vector<std::string>& impis,
    int64_t timestamp)
  {
    return new DeletePublicIDs(public_ids, impis, timestamp);
  }

  virtual DeletePublicIDs* create_DeletePublicIDs(
    const std::string& public_id,
    const std::vector<std::string>& impis,
    int64_t timestamp)
  {
    return new DeletePublicIDs(public_id, impis, timestamp);
  }

  class DeletePrivateIDs : public CassandraStore::Operation
  {
  public:
    /// Delete a single private ID from the cache.
    ///
    /// @param private_id the private ID to delete.
    DeletePrivateIDs(const std::string& private_id, int64_t timestamp);

    /// Delete an single private ID from the cache.
    ///
    /// @param private_ids the private IDs to delete.
    DeletePrivateIDs(const std::vector<std::string>& private_ids,
                     int64_t timestamp);
    virtual ~DeletePrivateIDs();

  protected:
    std::vector<std::string> _private_ids;
    int64_t _timestamp;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual DeletePrivateIDs* create_DeletePrivateIDs(const std::string& private_id,
                                                    int64_t timestamp)
  {
    return new DeletePrivateIDs(private_id, timestamp);
  }

  virtual DeletePrivateIDs* create_DeletePrivateIDs(const std::vector<std::string>& private_ids,
                                                    int64_t timestamp)
  {
    return new DeletePrivateIDs(private_ids, timestamp);
  }

  /// DeleteIMPIMapping operates on the "impi_mapping" Cassandra table,
  /// and deletes whole rows - effectively causing the cache to
  /// "forget" that a particular IMPI has been used to authenticate any
  /// IMPUs.

  /// The main use-case is for Registration-Termination-Requests, which
  /// may specify a private ID and require the S-CSCF to clear all data
  /// and bindings associated with it.

  class DeleteIMPIMapping : public CassandraStore::Operation
  {
  public:
    /// Delete a mapping from private IDs to the IMPUs they have authenticated.
    ///
    DeleteIMPIMapping(const std::vector<std::string>& private_ids,
                      int64_t timestamp);
    virtual ~DeleteIMPIMapping() {};

  protected:
    std::vector<std::string> _private_ids;
    int64_t _timestamp;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual DeleteIMPIMapping*
    create_DeleteIMPIMapping(const std::vector<std::string>& private_ids,
                             int64_t timestamp)
  {
    return new DeleteIMPIMapping(private_ids, timestamp);
  }

  /// DeleteIMPIMapping operates on the "impi_mapping" and "impu"
  /// Cassandra tables. It takes a vector of public IDs (representing
  /// the IDs in an implicit registration set) and a private ID, and
  /// removes the association between them:
  ///   * Each public ID's row in the IMPU table is updated to remove
  ///     this private ID. If this is the last private ID, the row is deleted.
  ///   * The private ID's row in the IMPI mapping table is updated to
  ///     remove the primary public ID. If this was the last prmary
  ///     public ID, the row is left empty for Cassandra to eventually
  ///     delete.

  /// The main use-case is for Registration-Termination-Requests.

  class DissociateImplicitRegistrationSetFromImpi : public CassandraStore::Operation
  {
  public:
    /// Delete a mapping from private IDs to the IMPUs they have authenticated.
    ///
    DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                              const std::string& impi,
                                              int64_t timestamp);
    DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                              const std::vector<std::string>& impis,
                                              int64_t timestamp);
    virtual ~DissociateImplicitRegistrationSetFromImpi() {};

  protected:
    std::vector<std::string> _impus;
    std::vector<std::string> _impis;
    int64_t _timestamp;

    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
  };

  virtual DissociateImplicitRegistrationSetFromImpi*
    create_DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                                     const std::string& impi,
                                                     int64_t timestamp)
  {
    return new DissociateImplicitRegistrationSetFromImpi(impus, impi, timestamp);
  }

  virtual DissociateImplicitRegistrationSetFromImpi*
    create_DissociateImplicitRegistrationSetFromImpi(const std::vector<std::string>& impus,
                                                     const std::vector<std::string>& impis,
                                                     int64_t timestamp)
  {
    return new DissociateImplicitRegistrationSetFromImpi(impus, impis, timestamp);
  }

  /// Get all the IMPUs for which Homestead has data in its cache.
  ///
  /// - When an HSS is in use, this lists all subscribers for which homestead is
  /// storing data learned from the HSS (which in practise is all subscribers
  /// that are assigned to this S-CSCF in the HSS).
  /// - When subscribers are locally provisioned this returns all provisioned
  /// subscribers.
  class ListImpus : public CassandraStore::Operation
  {
  public:
    ListImpus() {}
    virtual ~ListImpus() {}

    virtual std::vector<std::string>& get_impus_ref() { return _impus; }

  protected:
    bool perform(CassandraStore::Client* client, SAS::TrailId trail);
    std::vector<std::string> _impus;
  };

  virtual ListImpus* create_ListImpus()
  {
    return new ListImpus();
  }
};

#endif
