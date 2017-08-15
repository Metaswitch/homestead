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

#include "impu_store.h"

ImpuStore::Impu* ImpuStore::Impu::from_data(std::string const& impu,
                                            std::string& data,
                                            unsigned long& cas)
{
  return nullptr;
}

void ImpuStore::Impu::to_data(std::string& data)
{

}

ImpuStore::ImpiMapping* ImpuStore::ImpiMapping::from_data(std::string const& impi,
                                                          std::string& data,
                                                          unsigned long& cas)
{
  return nullptr;
}

void ImpuStore::ImpiMapping::to_data(std::string& data)
{

}

ImpuStore::Impu* ImpuStore::get_impu(const std::string& impu,
                                     SAS::TrailId trail)
{
  std::string data;
  uint64_t cas;

  Store::Status status = _store->get_data("impu", impu, data, cas, trail);

  if (status == Store::Status::OK)
  {
    return ImpuStore::Impu::from_data(impu, data, cas);
  }
  else
  {
    return nullptr;
  }
}

Store::Status ImpuStore::set_impu_without_cas(ImpuStore::Impu* impu,
                                              SAS::TrailId trail)
{
  std::string data;

  impu->to_data(data);

  Store::Status status = _store->set_data_without_cas("impu",
                                                      impu->impu,
                                                      data,
                                                      impu->expiry,
                                                      trail);

  return status;
}

Store::Status ImpuStore::set_impu(ImpuStore::Impu* impu,
                                  SAS::TrailId trail)
{
  std::string data;

  impu->to_data(data);

  Store::Status status = _store->set_data("impu",
                                          impu->impu,
                                          data,
                                          impu->cas,
                                          impu->expiry,
                                          trail);

  return status;
}

Store::Status ImpuStore::delete_impu(ImpuStore::Impu* impu,
                                     SAS::TrailId trail)
{
  return _store->delete_data("impu", impu->impu, trail);
}

ImpuStore::ImpiMapping* ImpuStore::get_impi_mapping(const std::string impi,
                                                    SAS::TrailId trail)
{
  std::string data;
  uint64_t cas;

  Store::Status status = _store->get_data("impi_mapping",
                                          impi,
                                          data,
                                          cas,
                                          trail);

  if (status == Store::Status::OK)
  {
    return ImpuStore::ImpiMapping::from_data(impi, data, cas);
  }
  else
  {
    return nullptr;
  }
}

Store::Status ImpuStore::set_impi_mapping(ImpiMapping* mapping,
                                          SAS::TrailId trail)
{
  std::string data;

  mapping->to_data(data);

  Store::Status status = _store->set_data("impi_mapping",
                                          mapping->impi,
                                          data,
                                          mapping->cas,
                                          mapping->expiry(),
                                          trail);

  return status;
}

Store::Status ImpuStore::delete_impi_mapping(ImpiMapping* mapping,
                                             SAS::TrailId trail)
{
  return _store->delete_data("impi_mapping", mapping->impi, trail);
}
