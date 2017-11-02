/**
 * Memcached based store for storing IMPUs
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef IMPU_STORE_H_
#define IMPU_STORE_H_

#include "charging_addresses.h"
#include "reg_state.h"
#include "store.h"

#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <lz4.h>

/**
 * Encode a number in a variable number of bytes and append
 *
 * This minimizes the length that it takes to store the length
 * uncompressed data.
 *
 * The length is stored with seven bits in each byte (0x7f), and then
 * the top bit (0x80) set if there are more bits to go.
 */
void encode_varbyte(uint64_t uncomp_size_len, std::string& data);

/**
 * Decode a variable length from the supplied buffer.
 *
 * An offset into the buffer can be provided.
 *
 * Returns a length, or zero on failure.
 */
uint64_t decode_varbyte(const std::string& data, size_t& offset);

class ImpuStore
{
public:
  class Impu
  {
  private:
    static thread_local LZ4_stream_t* _thrd_lz4_stream;
    static thread_local struct preserved_hash_table_entry_t* _thrd_lz4_hash;

    const static std::string _dict_v0;

  protected:
    Impu(const std::string impu,
         uint64_t cas,
         int64_t expiry,
         const ImpuStore* store) :
      impu(impu),
      cas(cas),
      expiry(expiry),
      store(store)
    {
    }

  public:
    virtual ~Impu(){
    }

    virtual bool is_default_impu() = 0;

    virtual Store::Status to_data(std::string& data);

    static void compress_data_v0(const std::string& data,
                                 char*& buffer,
                                 int& comp_size);

    static Impu* from_data(const std::string& impu,
                           std::string& data,
                           uint64_t cas,
                           ImpuStore* store);

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer) = 0;

    const std::string impu;
    const uint64_t cas;
    const int64_t expiry;
    const ImpuStore * const store;
  };

  class DefaultImpu : public Impu
  {
  public:
    DefaultImpu(const std::string& impu,
                const std::vector<std::string>& associated_impus,
                const std::vector<std::string>& impis,
                RegistrationState registration_state,
                const ChargingAddresses& charging_addresses,
                const std::string& service_profile,
                uint64_t cas,
                int64_t expiry,
                const ImpuStore* store) :
      Impu(impu, cas, expiry, store),
      registration_state(registration_state),
      charging_addresses(charging_addresses),
      associated_impus(associated_impus),
      impis(impis),
      service_profile(service_profile)
    {
    }

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer);

    virtual ~DefaultImpu(){}

    static Impu* from_json(const std::string& impu,
                           rapidjson::Value& json,
                           uint64_t cas,
                           ImpuStore* store);

    bool has_associated_impu(const std::string& impu)
    {
      return std::find(associated_impus.begin(),
                       associated_impus.end(),
                       impu) != associated_impus.end();
    }

    virtual bool is_default_impu(){ return true; }

    RegistrationState registration_state;
    ChargingAddresses charging_addresses;
    std::vector<std::string> associated_impus;
    std::vector<std::string> impis;
    std::string service_profile;
  };

  class AssociatedImpu : public Impu
  {
  public:
    AssociatedImpu(std::string impu,
                   std::string default_impu,
                   uint64_t cas,
                   int64_t expiry,
                   const ImpuStore* store) :
      Impu(impu, cas, expiry, store),
      default_impu(default_impu)
    {
    }

    virtual ~AssociatedImpu(){}

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer);

    virtual bool is_default_impu(){ return false; }

    const std::string default_impu;

    static Impu* from_json(const std::string& impu,
                           rapidjson::Value& json,
                           uint64_t cas,
                           ImpuStore* store);
  };

  class ImpiMapping
  {
  public:
    ImpiMapping(std::string impi,
                std::vector<std::string> default_impus,
                uint64_t cas,
                int64_t expiry) :
      impi(impi),
      cas(cas),
      _expiry(expiry),
      _default_impus(default_impus)
    {
    }

    ImpiMapping(std::string impi, std::string impu, int64_t expiry) :
      impi(impi),
      cas(0L),
      _expiry(expiry),
      _default_impus({ impu })
    {
    }

    static ImpiMapping* from_data(const std::string& impu,
                                  const std::string&,
                                  uint64_t cas);

    static ImpiMapping* from_json(const std::string& impi,
                                  rapidjson::Value& json,
                                  uint64_t cas);

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer);

    virtual Store::Status to_data(std::string& data);

    virtual ~ImpiMapping(){
    }

    void add_default_impu(const std::string& impu)
    {
      _default_impus.push_back(impu);
    }

    bool has_default_impu(const std::string& impu)
    {
      return std::find(_default_impus.begin(),
                       _default_impus.end(),
                       impu) != _default_impus.end();
    }

    void remove_default_impu(const std::string& impu)
    {
      _default_impus.erase(std::remove(_default_impus.begin(),
                                       _default_impus.end(),
                                       impu),
                           _default_impus.end());
    }

    bool is_empty()
    {
      return _default_impus.size() == 0;
    }

    int64_t get_expiry()
    {
      return _expiry;
    }

    void set_expiry(int64_t expiry)
    {
      _expiry = expiry;
    }

    const std::vector<std::string>& get_default_impus()
    {
      return _default_impus;
    }

    const std::string impi;
    const uint64_t cas;

  private:
    int64_t _expiry;
    std::vector<std::string> _default_impus;
  };

  ImpuStore(Store* store) : _store(store)
  {

  }

  // Sets the IMPU in the store without checking the CAS value, overwriting any
  // data already present.
  Store::Status set_impu_without_cas(Impu* impu, SAS::TrailId trail);

  // Attempts to add an IMPU that doesn't already exist in the store.
  // Fails with DATA_CONTENTION if an IMPU is already present.
  Store::Status add_impu(Impu* impu, SAS::TrailId trail);

  // Sets the IMPU using CAS
  Store::Status set_impu(Impu* impu, SAS::TrailId trail);

  Impu* get_impu(const std::string& impu, SAS::TrailId trail);

  Store::Status delete_impu(Impu* impu, SAS::TrailId trail);

  Store::Status set_impi_mapping(ImpiMapping* mapping, SAS::TrailId trail);

  // Get the ImpiMapping for this impi.
  // If successful, set the pointer out_mapping to be the retrieved ImpiMapping
  // If not, does not alter mapping
  Store::Status get_impi_mapping(const std::string impi, ImpiMapping*& out_mapping, SAS::TrailId trail);

  Store::Status delete_impi_mapping(ImpiMapping* mapping, SAS::TrailId trail);

private:
  Store* _store;
};

#endif
