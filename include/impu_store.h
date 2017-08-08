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

#include "store.h"

#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

class ImpuStore
{
public:
  class Impu
  {
  protected:
    Impu(std::string impu, uint64_t cas) :
      impu(impu),
      cas(cas)
    {
    }
 
  public:
    virtual ~Impu(){
    }

    virtual bool is_default_impu() = 0;

    static Impu* from_json(rapidjson::Value* json, uint64_t cas);

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>* writer) = 0;
 
    const ImpuStore* store;
    const std::string impu;
    const uint64_t cas;
  };

  class DefaultImpu : public Impu
  {
  public:
    DefaultImpu(std::string impu,
                std::vector<std::string> associated_impus,
                std::vector<std::string> impis,
                bool is_registered,
                uint64_t cas) :
      Impu(impu, cas),
      associated_impus(associated_impus),
      impis(impis)
    {
    }

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>* writer){}

    virtual ~DefaultImpu(){};

    static Impu* from_json(rapidjson::Value* json, uint64_t cas);

    bool has_associated_impu(const std::string& impu)
    {
      return std::find(associated_impus.begin(),
                       associated_impus.end(),
                       impu) != associated_impus.end();
    }

    virtual bool is_default_impu(){ return true; }

    std::vector<std::string> associated_impus;
    std::vector<std::string> impis;
    bool is_registered;
  };

  class AssociatedImpu : public Impu
  {
  public:
    AssociatedImpu(std::string impu, std::string default_impu, uint64_t cas) :
      Impu(impu, cas),
      default_impu(default_impu)
    {
    }

    virtual ~AssociatedImpu(){}

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>* writer){}
    virtual bool is_default_impu(){ return false; }

    const std::string default_impu;

    static Impu* from_json(rapidjson::Value* json, uint64_t cas);
  };

  class ImpiMapping
  {
  public:
    ImpiMapping(std::string impi, std::vector<std::string> default_impus, uint64_t cas) :
      impi(impi),
      default_impus(default_impus),
      cas(cas)
    {
    }

    ImpiMapping(std::string impi, std::string impu) : impi(impi), default_impus({ impu }), cas(0L)
    {
    }

    static Impu* from_json(rapidjson::Value* json, uint64_t cas);

    virtual void write_json(rapidjson::Writer<rapidjson::StringBuffer>* writer){}

    virtual ~ImpiMapping(){
    }

    void add_default_impu(const std::string& impu)
    {
      default_impus.push_back(impu);
    }

    bool has_default_impu(const std::string& impu)
    {
      return std::find(default_impus.begin(),
                       default_impus.end(),
                       impu) != default_impus.end();
    }

    void remove_default_impu(const std::string& impu)
    {
      default_impus.erase(std::remove(default_impus.begin(),
                                      default_impus.end(),
                                      impu));
    }

    bool is_empty()
    {
      return default_impus.size() == 0;
    }

    const std::string impi;

    const std::vector<std::string>& get_default_impus()
    {
      return default_impus;
    }

  private:
    std::vector<std::string> default_impus;
    const uint64_t cas;
  };

  ImpuStore(Store* store) : _store(store)
  {

  }

  Store::Status set_impu_without_cas(Impu* impu, SAS::TrailId trail);
  Store::Status set_impu(Impu* impu, SAS::TrailId trail);

  Impu* get_impu(const std::string& impu, SAS::TrailId trail);

  Store::Status delete_impu(Impu* impu, SAS::TrailId trail);

  Store::Status set_impi_mapping(ImpiMapping* mapping, SAS::TrailId trail);

  ImpiMapping* get_impi_mapping(const std::string impi, SAS::TrailId trail);

  Store::Status delete_impi_mapping(ImpiMapping* mapping, SAS::TrailId trail);

private:
  Store* _store;
};

#endif
