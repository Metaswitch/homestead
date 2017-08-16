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

#include <climits>

#include "json_parse_utils.h"
#include "log.h"
#include "rapidjson/error/en.h"

const char* ImpuStore::Impu::_dict_v0 = "";
int ImpuStore::Impu::_dict_v0_size = 0;

thread_local LZ4_stream_t* ImpuStore::Impu::_thrd_lz4_stream;
thread_local struct preserved_hash_table_entry_t* ImpuStore::Impu::_thrd_lz4_hash;

// Associated IMPU -> Default IMPU
static const char * const JSON_DEFAULT_IMPU = "default_impu";

// IRS (Default IMPU)
static const char * const JSON_ASSOCIATED_IMPUS = "assoc_impu";
static const char * const JSON_SERVICE_PROFILE = "service_profile";
static const char * const JSON_REGISTRATION_STATE = "registration_state";
static const char * const JSON_IMPIS = "impis";

// IMPI -> Default IMPU
static const char * const JSON_DEFAULT_IMPUS = "default_impus";

// The default acceleration (1) is sufficient for us and gives best
// compression.
static const int ACCELERATION = 1;

// The maximum buffer size to use for IMPU compression
static const int MAX_BUFFER_LEN = 131072;

ImpuStore::Impu* ImpuStore::Impu::from_data(std::string const& impu,
                                            std::string& data,
                                            unsigned long& cas)
{
  if (data.size() < 2)
  {
    // Invalid data
    return nullptr;
  }

  // Version is stored in character 0.
  if (data[0] == 0)
  {
    // Version 0 - LZ4 compression
    // Data is stored as [version][length][zlib4 compressed JSON]
    size_t offset = 1;

    // Size
    uint64_t length_long = 0;

    do
    {
      length_long |= ((data[offset] & 0x75) << (7*(offset - 1)));
    } while(((data[offset] & 0x80) != 0) && length_long < INT_MAX);

    if (length_long > INT_MAX)
    {
      // Data exceeded allowed length
      return nullptr;
    }

    int length = (int) length_long;
    const char* compressed = data.c_str() + offset;
    int compressed_size = data.size() - offset;
    char* json = new char[length];

    int rc = LZ4_decompress_safe_usingDict(compressed,
                                            json,
                                            compressed_size,
                                            length,
                                            _dict_v0,
                                            _dict_v0_size);


    if (rc == 0 || rc != length)
    {
      TRC_WARNING("Failed to decompress LZ4 IMPU data - read %u/%u",
                  rc, length);

      delete[] json;

      return nullptr;
    }
    else
    {
      rapidjson::Document doc;
      doc.Parse<0>(json);

      if (doc.HasParseError())
      {
        TRC_WARNING("Failed to parse IMPU as JSON %s - Error: %s",
                    json,
                    rapidjson::GetParseError_En(doc.GetParseError()));
        delete[] json;
        return nullptr;
      }
      else if (!doc.IsObject())
      {
        TRC_WARNING("IMPU JSON didn't represent object - %s",
                    json);
        delete[] json;
        return nullptr;
      }
      else
      {
        // Successfully parsed the JSON, so we can reclaim the memory
        delete[] json;

        if (doc.HasMember(JSON_DEFAULT_IMPU))
        {
          return ImpuStore::AssociatedImpu::from_json(impu, doc, cas);
        }
        else
        {
          return ImpuStore::DefaultImpu::from_json(impu, doc, cas);
        }
      }
    }
  }
  else
  {
    TRC_WARNING("Unknown IMPU version: %u", data[0]);

    return nullptr;
  }
}

ImpuStore::Impu* ImpuStore::AssociatedImpu::from_json(std::string const& impu,
                                                      rapidjson::Value& json,
                                                      unsigned long cas)
{
  std::string default_impu;

  JSON_SAFE_GET_STRING_MEMBER(json, JSON_DEFAULT_IMPU, default_impu);

  return new AssociatedImpu(impu, default_impu, cas);
}

ImpuStore::Impu* ImpuStore::DefaultImpu::from_json(std::string const& impu,
                                                   rapidjson::Value& json,
                                                   unsigned long cas)
{
  std::vector<std::string> assoc_impus;
  std::vector<std::string> impis;
  std::string service_profile;

  bool state;

  JSON_GET_BOOL_MEMBER(json, JSON_REGISTRATION_STATE, state);

  RegistrationState reg_state = state ?
    RegistrationState::REGISTERED :
    RegistrationState::UNREGISTERED;

  JSON_SAFE_GET_STRING_MEMBER(json, JSON_SERVICE_PROFILE, service_profile);

  JSON_ASSERT_ARRAY(json[JSON_ASSOCIATED_IMPUS]);

  const rapidjson::Value& assoc_impus_arr = json[JSON_ASSOCIATED_IMPUS];

  for (rapidjson::Value::ConstValueIterator assoc_impus_it = assoc_impus_arr.Begin();
       assoc_impus_it != assoc_impus_arr.End();
       ++assoc_impus_it)
  {
    JSON_ASSERT_STRING(*assoc_impus_it);
    assoc_impus.push_back(assoc_impus_it->GetString());
  }

  JSON_ASSERT_ARRAY(json[JSON_IMPIS]);

  const rapidjson::Value& impis_arr = json[JSON_IMPIS];

  for (rapidjson::Value::ConstValueIterator impis_it = impis_arr.Begin();
       impis_it != impis_arr.End();
       ++impis_it)
  {
    JSON_ASSERT_STRING(*impis_it);
    impis.push_back(impis_it->GetString());
  }

  return new DefaultImpu(impu,
                         assoc_impus,
                         impis,
                         reg_state,
                         service_profile,
                         cas);
}

Store::Status ImpuStore::Impu::to_data(std::string& data)
{
  // We get the JSON representing the IMPU, compress it using
  // lz4, and then build a buffer to return.
  // The buffer contains a version (0), the uncompressed size
  // (an array of 7 bits, with bit 0x80 set if there is more
  // to come), and the compressed data.

  unsigned int uncomp_size;
  int comp_size;
  char* buffer; // Buffer for compressed data

  // Scope the JSON string so we don't have to keep it in
  // memory too long
  {
    std::string json;

    {
      // Gather the JSON
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      writer.StartObject();
      write_json(writer);
      writer.EndObject();
      json = buffer.GetString();
    }

    json.push_back('\0'); // Add a terminating null byte for safety

    uncomp_size = json.size();

    // Check we have a LZ4 stream with the V0 dict pre-prepared.
    if (_thrd_lz4_stream == NULL)
    {
      _thrd_lz4_stream = LZ4_createStream();
      LZ4_loadDict(_thrd_lz4_stream, _dict_v0, _dict_v0_size);
      LZ4_stream_preserve(_thrd_lz4_stream, &_thrd_lz4_hash);
    }

    // Compress the data using LZ4
    LZ4_stream_t* stream = LZ4_createStream();

    size_t buffer_length = 2048; // 2KB
    buffer = (char*) malloc(buffer_length);

    do
    {
      LZ4_stream_restore_preserved(stream, _thrd_lz4_stream, _thrd_lz4_hash);
      comp_size = LZ4_compress_fast_continue(stream,
                                             json.c_str(),
                                             buffer,
                                             uncomp_size,
                                             buffer_length,
                                             ACCELERATION);

      if (comp_size <= 0)
      {
        // Compression failed - retry with a bigger buffer.
        // Buffer size is the only reason it can fail. We stop
        // the buffer growing beyond reason.
        int new_buffer_length = buffer_length * 2;

        if (new_buffer_length > MAX_BUFFER_LEN)
        {
          TRC_WARNING("Failed to attempt to compress %lu bytes of data - won't "
                      "fit into %lu bytes, proposed new buffer of %lu bytes "
                      "exceeds maximum of %lu bytes",
                      uncomp_size, buffer_length,
                      new_buffer_length, MAX_BUFFER_LEN);
          break;
        }

        buffer_length = new_buffer_length;

        buffer = (char*) realloc((void*)buffer, buffer_length);
        LZ4_resetStream(stream);
      }
    } while(comp_size == 0);

    LZ4_freeStream(stream);
  }

  if (comp_size == 0)
  {
    free(buffer);

    return Store::Status::ERROR;
  }

  // Start creating the buffer to return

  // Work out the number of bits set in json
  unsigned int uncomp_size_bits = 0;
  unsigned int v = uncomp_size;

  while (v >>= 1) {
        uncomp_size_bits++;
  }

  // Work out the number of bytes we need. 7 bits per byte.
  // We add 6 to fix rounding errors as (7 + 6) / 7 => 1 and
  // (8 + 6) / 7 => 2
  int uncomp_size_len = (uncomp_size_bits + 6) / 7;

  data.reserve(1 + uncomp_size_len + uncomp_size);

  // Version
  data.push_back((char) 0);

  // Length of the uncompressed data
  while (uncomp_size_len != 0)
  {
    // Get 7 bits into a byte
    char size_byte = (uncomp_size >> (uncomp_size_len * 7)) & 0x7f;

    if (uncomp_size_len > 1)
    {
      size_byte |= 0x80;
    }

    data.push_back(size_byte);
    uncomp_size_len--;
  }

  // Add the compressed data to the buffer
  data.append(buffer, comp_size);

  // Finally, we can free the compressed data
  free(buffer);

  return Store::Status::OK;
}

void ImpuStore::DefaultImpu::write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
  writer.String(JSON_REGISTRATION_STATE);

  bool state;

  if (registration_state == RegistrationState::REGISTERED)
  {
    state = true;
  }
  else if (registration_state == RegistrationState::UNREGISTERED)
  {
    state = false;
  }
  else
  {
    TRC_WARNING("Unexpected registration state: %u",
                registration_state);
    state = false;
  }

  writer.Bool(state);
  writer.String(JSON_SERVICE_PROFILE);
  writer.String(service_profile.c_str());
  writer.String(JSON_ASSOCIATED_IMPUS);
  writer.StartArray();

  for (const std::string& assoc_impu : associated_impus)
  {
    writer.String(assoc_impu.c_str());
  }

  writer.EndArray();

  writer.String(JSON_IMPIS);
  writer.StartArray();

  for (const std::string& impi : impis)
  {
    writer.String(impi.c_str());
  }

  writer.EndArray();
}

void ImpuStore::AssociatedImpu::write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
  writer.String(JSON_DEFAULT_IMPU);
  writer.String(default_impu.c_str());
}

ImpuStore::ImpiMapping* ImpuStore::ImpiMapping::from_data(std::string const& impi,
                                                          std::string& data,
                                                          unsigned long& cas)
{
  rapidjson::Document doc;
  doc.Parse<0>(data.c_str());

  if (doc.HasParseError())
  {
    TRC_WARNING("Failed to parse IMPU as JSON %s - Error: %s",
                data.c_str(),
                rapidjson::GetParseError_En(doc.GetParseError()));
    return nullptr;
  }
  else if (!doc.IsObject())
  {
    TRC_WARNING("IMPU JSON didn't represent object - %s",
                data.c_str());
    return nullptr;
  }
  else
  {
    return ImpuStore::ImpiMapping::from_json(impi, doc, cas);
  }
}

Store::Status ImpuStore::ImpiMapping::to_data(std::string& data)
{
  // Gather the JSON
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  write_json(writer);
  writer.EndObject();
  data = buffer.GetString();
  data.push_back('\0'); // Add a terminating null byte for safety

  return Store::Status::OK;
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

  Store::Status status = impu->to_data(data);

  if (status == Store::Status::OK)
  {
    status = _store->set_data_without_cas("impu",
                                          impu->impu,
                                          data,
                                          impu->expiry,
                                          trail);
  }

  return status;
}

Store::Status ImpuStore::set_impu(ImpuStore::Impu* impu,
                                  SAS::TrailId trail)
{
  std::string data;

  Store::Status status = impu->to_data(data);

  if (status == Store::Status::OK)
  {
    _store->set_data("impu",
                     impu->impu,
                     data,
                     impu->cas,
                     impu->expiry,
                     trail);
  }

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

  Store::Status status = mapping->to_data(data);

  if (status == Store::Status::OK)
  {
    status = _store->set_data("impi_mapping",
                              mapping->impi,
                              data,
                              mapping->cas,
                              mapping->expiry(),
                              trail);
  }

  return status;
}

Store::Status ImpuStore::delete_impi_mapping(ImpiMapping* mapping,
                                             SAS::TrailId trail)
{
  return _store->delete_data("impi_mapping", mapping->impi, trail);
}


ImpuStore::ImpiMapping* ImpuStore::ImpiMapping::from_json(std::string const& impi,
                                                         rapidjson::Value& json,
                                                         unsigned long cas)
{
  std::vector<std::string> impus;

  JSON_ASSERT_ARRAY(json[JSON_DEFAULT_IMPUS]);

  const rapidjson::Value& default_impus_arr = json[JSON_DEFAULT_IMPUS];

  for (rapidjson::Value::ConstValueIterator default_impus_it = default_impus_arr.Begin();
       default_impus_it != default_impus_arr.End();
       ++default_impus_it)
  {
    JSON_ASSERT_STRING(*default_impus_it);
    impus.push_back(default_impus_it->GetString());
  }

  return new ImpiMapping(impi,
                         impus,
                         cas);
}

void ImpuStore::ImpiMapping::write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
  writer.String(JSON_DEFAULT_IMPUS);
  writer.StartArray();

  for (const std::string& default_impu : _default_impus)
  {
    writer.String(default_impu.c_str());
  }

  writer.EndArray();
}
