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

const std::string ImpuStore::Impu::_dict_v0 =
  "{\"registration_state\":true,\"service_profile\":\"<IMSSubscription>"
  "<PrivateID></PrivateID><ServiceProfile><PublicIdentity><Identity>"
  "<Extension></Extension></PublicIdentity><IniitialFilterCriteria><Priority>"
  "</Priority><TriggerPoint><ConditionTypeCNF></ConditionTypeCNF><SPT>"
  "<ConditionNegated></ConditionNegated><Group></Group><Method></Method>"
  "</TriggerPoint><ApplicationServer><ServerName></ServerName><DefaultHandling>"
  "</DefaultHandling></ApplicationServer></InitialFilterCriteria>"
  "</ServiceProfile></IMSSubscription><SIPHeader><Header></Header></SIPHeader>"
  "<SessionCase></SessionCase>\",\"expiry\":,\"assoc_impu\":[\"],\"impis\":[],"
  "\"ecfs\":[],\"ccfs\":[]}\"default_impu\":\"";

thread_local LZ4_stream_t* ImpuStore::Impu::_thrd_lz4_stream;
thread_local struct preserved_hash_table_entry_t* ImpuStore::Impu::_thrd_lz4_hash;

// General
static const char * const JSON_EXPIRY = "expiry";

// Associated IMPU -> Default IMPU
static const char * const JSON_DEFAULT_IMPU = "default_impu";

// IRS (Default IMPU)
static const char * const JSON_ASSOCIATED_IMPUS = "assoc_impu";
static const char * const JSON_SERVICE_PROFILE = "service_profile";
static const char * const JSON_REGISTRATION_STATE = "registration_state";
static const char * const JSON_IMPIS = "impis";
static const char * const JSON_CCFS = "ccfs";
static const char * const JSON_ECFS = "ecfs";

// IMPI -> Default IMPU
static const char * const JSON_DEFAULT_IMPUS = "default_impus";

// The default acceleration (1) is sufficient for us and gives best
// compression.
static const int ACCELERATION = 1;

// The maximum buffer size to use for IMPU compression
// 128 KB
static const int MAX_BUFFER_LEN = 131072;

void encode_varbyte(uint64_t uncomp_size, std::string& data)
{
  while (uncomp_size != 0)
  {
    // Get 7 bits into a byte
    char size_byte = uncomp_size & 0x7f;
    uncomp_size = uncomp_size >> 7;

    TRC_DEBUG("Adding byte: %d - remaining: %llu",
              size_byte, uncomp_size);

    if (uncomp_size > 0)
    {
      TRC_DEBUG("Flagging extra byte required");
      size_byte |= 0x80;
    }

    data.push_back(size_byte);
  }
}

uint64_t decode_varbyte(const std::string& data, size_t& offset)
{
  uint64_t length = 0;
  bool done;
  size_t i = 0;

  do
  {
    char bits = (data[offset] & 0x7f);
    length |= (bits << (7*i));
    TRC_DEBUG("Extracted: %d - total: %llu",
              bits, length);
    done = ((data[offset] & 0x80) == 0);
    offset++;
    i++;
  } while(!done && length < INT_MAX && offset < data.size());

  if (!done || length >= INT_MAX)
  {
    if (length >= INT_MAX)
    {
      // Data exceeded allowed length
      TRC_WARNING("Uncompressed data exceeded compressable length: %llu",
                  length);

    }
    else if (offset >= data.size())
    {
      // We were about to run off the data buffer
      TRC_WARNING("Length exceeded data buffer");
    }

    // Data is corrupt - return 0
    length = 0;
  }

  return length;
}

ImpuStore::Impu* ImpuStore::Impu::from_data(const std::string& impu,
                                            std::string& data,
                                            unsigned long cas,
                                            ImpuStore* store)
{
  if (data.size() == 0)
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

    // Size of uncompressed data
    uint64_t length_long = decode_varbyte(data, offset);

    if (length_long == 0)
    {
      // Data is corrupt
      return nullptr;
    }

    int length = (int) length_long;
    const char* compressed = data.c_str() + offset;
    int compressed_size = data.size() - offset;
    char* json = new char[length + 1];
    json[length] = '\0';

    TRC_DEBUG("Decompressing %llu bytes of data into %llu bytes",
              compressed_size,
              length);

    int rc = LZ4_decompress_safe_usingDict(compressed,
                                           json,
                                           compressed_size,
                                           length,
                                           _dict_v0.c_str(),
                                           _dict_v0.size());


    if (rc == 0 || rc != length)
    {
      TRC_WARNING("Failed to decompress LZ4 IMPU data - read %d/%d",
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
          return ImpuStore::AssociatedImpu::from_json(impu, doc, cas, store);
        }
        else
        {
          return ImpuStore::DefaultImpu::from_json(impu, doc, cas, store);
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
                                                      unsigned long cas,
                                                      ImpuStore* store)
{
  std::string default_impu;
  int64_t expiry = 0L;

  JSON_SAFE_GET_STRING_MEMBER(json, JSON_DEFAULT_IMPU, default_impu);
  JSON_SAFE_GET_INT_64_MEMBER(json, JSON_EXPIRY, expiry);

  return new AssociatedImpu(impu, default_impu, cas, expiry, store);
}

ImpuStore::Impu* ImpuStore::DefaultImpu::from_json(std::string const& impu,
                                                   rapidjson::Value& json,
                                                   unsigned long cas,
                                                   ImpuStore* store)
{
  std::vector<std::string> assoc_impus;
  std::vector<std::string> impis;
  std::deque<std::string> ccfs;
  std::deque<std::string> ecfs;
  std::string service_profile;
  int64_t expiry = 0L;

  bool state;

  JSON_GET_BOOL_MEMBER(json, JSON_REGISTRATION_STATE, state);

  RegistrationState reg_state = state ?
    RegistrationState::REGISTERED :
    RegistrationState::UNREGISTERED;

  JSON_SAFE_GET_INT_64_MEMBER(json, JSON_EXPIRY, expiry);
  JSON_SAFE_GET_STRING_MEMBER(json, JSON_SERVICE_PROFILE, service_profile);

  extract_json_string_array(json, JSON_ASSOCIATED_IMPUS, assoc_impus);
  extract_json_string_array(json, JSON_IMPIS, impis);
  extract_json_string_array(json, JSON_CCFS, ccfs);
  extract_json_string_array(json, JSON_ECFS, ecfs);

  ChargingAddresses charging_addresses = ChargingAddresses(ccfs, ecfs);

  return new DefaultImpu(impu,
                         assoc_impus,
                         impis,
                         reg_state,
                         charging_addresses,
                         service_profile,
                         cas,
                         expiry,
                         store);
}

void ImpuStore::Impu::compress_data_v0(const std::string& data,
                                       char*& buffer,
                                       int& comp_size)
{
  unsigned int uncomp_size = data.size();

  // Check we have a LZ4 stream with the V0 dict pre-prepared.
  if (_thrd_lz4_stream == NULL)
  {
    _thrd_lz4_stream = LZ4_createStream();
    LZ4_loadDict(_thrd_lz4_stream, _dict_v0.c_str(), _dict_v0.size());
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
                                           data.c_str(),
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

      // LCOV_EXCL_START
      if (new_buffer_length > MAX_BUFFER_LEN)
      {
        TRC_WARNING("Failed to attempt to compress %lu bytes of data - won't "
                    "fit into %lu bytes, proposed new buffer of %lu bytes "
                    "exceeds maximum of %lu bytes",
                    uncomp_size, buffer_length,
                    new_buffer_length, MAX_BUFFER_LEN);
        break;
      }
      // LCOV_EXCL_STOP

      buffer_length = new_buffer_length;

      buffer = (char*) realloc((void*)buffer, buffer_length);
      LZ4_resetStream(stream);
    }
  } while(comp_size == 0);

  LZ4_freeStream(stream);
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
    TRC_DEBUG("Determining JSON for %s", impu.c_str());

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

    TRC_DEBUG("Wrote IMPU %s to JSON: %lu bytes", impu.c_str(), uncomp_size);

    compress_data_v0(json, buffer, comp_size);
  }

  // LCOV_EXCL_START
  // This only happens when we fail to compress some data,
  // which isn't hittable in the UTs
  if (comp_size == 0)
  {
    free(buffer);

    return Store::Status::ERROR;
  }
  // LCOV_EXCL_STOP

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

  data.reserve(1 + uncomp_size_len + comp_size);

  // Version
  data.push_back((char) 0);

  // Length of the uncompressed data
  encode_varbyte(uncomp_size, data);

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
  writer.String(JSON_EXPIRY);
  writer.Int64(expiry);

  write_json_string_array(writer, JSON_ASSOCIATED_IMPUS, associated_impus);
  write_json_string_array(writer, JSON_IMPIS, impis);
  write_json_string_array(writer, JSON_ECFS, charging_addresses.ecfs);
  write_json_string_array(writer, JSON_CCFS, charging_addresses.ccfs);
}

void ImpuStore::AssociatedImpu::write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
  writer.String(JSON_DEFAULT_IMPU);
  writer.String(default_impu.c_str());
  writer.String(JSON_EXPIRY);
  writer.Int64(expiry);
}

ImpuStore::ImpiMapping* ImpuStore::ImpiMapping::from_data(const std::string& impi,
                                                          const std::string& data,
                                                          unsigned long cas)
{
  // Unlike IMPUs, we don't compress IMPI mappings, we just store a JSON
  // dictionary, as the overhead of compression is likely to be worse than
  // the size of the data

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
  // Gather the JSON - as per from_data we don't compress IMPI Mappings
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

  Store::Status status = _store->get_data("impu",
                                          impu,
                                          data,
                                          cas,
                                          trail,
                                          false);

  if (status == Store::Status::OK)
  {
    return ImpuStore::Impu::from_data(impu, data, cas, this);
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
    int now = time(0);

    status = _store->set_data_without_cas("impu",
                                          impu->impu,
                                          data,
                                          impu->expiry - now,
                                          trail,
                                          false);
  }

  return status;
}

Store::Status ImpuStore::add_impu(ImpuStore::Impu* impu,
                                  SAS::TrailId trail)
{
  std::string data;

  Store::Status status = impu->to_data(data);

  if (status == Store::Status::OK)
  {
    int now = time(0);

    // Set the data with a CAS of 0, which will fail if there's data already
    // present
    status = _store->set_data("impu",
                              impu->impu,
                              data,
                              0,
                              impu->expiry - now,
                              trail,
                              false);
  }

  return status;
}

Store::Status ImpuStore::set_impu(ImpuStore::Impu* impu,
                                  SAS::TrailId trail)
{
  TRC_DEBUG("Writing %s to store (SAS Trail: %lu)",
            impu->impu.c_str(), trail);

  std::string data;

  Store::Status status = impu->to_data(data);

  if (status == Store::Status::OK)
  {
    int now = time(0);

    status = _store->set_data("impu",
                              impu->impu,
                              data,
                              impu->cas,
                              impu->expiry - now,
                              trail,
                              false);
  }

  TRC_DEBUG("Wrote %s to store (SAS Trail: %lu) with result: %u",
            impu->impu.c_str(), trail, status);

  return status;
}

Store::Status ImpuStore::delete_impu(ImpuStore::Impu* impu,
                                     SAS::TrailId trail)
{
  return _store->delete_data("impu", impu->impu, trail);
}

Store::Status ImpuStore::get_impi_mapping(const std::string impi,
                                          ImpuStore::ImpiMapping*& out_mapping,
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
    // Use a temporary variable to hold the ImpiMapping* so that we don't change
    // out_mapping if we fail to decode the mapping
    ImpuStore::ImpiMapping* mapping = ImpuStore::ImpiMapping::from_data(impi,
                                                                        data,
                                                                        cas);
    // LCOV_EXCL_START
    if (mapping == nullptr)
    {
      // We failed to decode the mapping from the retrieved data, so just return
      // an ERROR
      status = Store::Status::ERROR;
    }
    // LCOV_EXCL_STOP
    else
    {
      out_mapping = mapping;
    }
  }

  return status;
}

Store::Status ImpuStore::set_impi_mapping(ImpiMapping* mapping,
                                          SAS::TrailId trail)
{
  std::string data;

  Store::Status status = mapping->to_data(data);

  if (status == Store::Status::OK)
  {
    int now = time(0);

    status = _store->set_data("impi_mapping",
                              mapping->impi,
                              data,
                              mapping->cas,
                              mapping->get_expiry() - now,
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
  int64_t expiry = 0L;

  extract_json_string_array(json, JSON_DEFAULT_IMPUS, impus);

  JSON_SAFE_GET_INT_64_MEMBER(json, JSON_EXPIRY, expiry);

  return new ImpiMapping(impi,
                         impus,
                         cas,
                         expiry);
}

void ImpuStore::ImpiMapping::write_json(rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
  write_json_string_array(writer, JSON_DEFAULT_IMPUS, _default_impus);
  writer.String(JSON_EXPIRY);
  writer.Int64(_expiry);
}
