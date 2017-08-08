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

Store::Status ImpuStore::set_impu_without_cas(ImpuStore::Impu* impu,
                                              SAS::TrailId trail)
{
  return Store::Status::ERROR;
}

Store::Status ImpuStore::set_impu(ImpuStore::Impu* impu,
                                  SAS::TrailId trail)
{
  return Store::Status::ERROR;
}

Store::Status ImpuStore::delete_impu(ImpuStore::Impu* impu,
                                     SAS::TrailId trail)
{
  return Store::Status::ERROR;
}

Store::Status ImpuStore::set_impi_mapping(ImpiMapping* mapping,
                                          SAS::TrailId trail)
{
  return Store::Status::ERROR;
}

Store::Status ImpuStore::delete_impi_mapping(ImpiMapping* mapping,
                                             SAS::TrailId trail)
{
  return Store::Status::ERROR;
}

ImpuStore::ImpiMapping* ImpuStore::get_impi_mapping(const std::string impi,
                                                    SAS::TrailId trail)
{
  return nullptr;
}

ImpuStore::Impu* ImpuStore::get_impu(const std::string& impu,
                                     SAS::TrailId trail)
{
  return nullptr;
}
