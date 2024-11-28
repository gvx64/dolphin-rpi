// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DiscIO/VolumeWii.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <future> //gvx64 rollforward to 5.0-12188 - implement .rvz support
#include <map>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mbedtls/aes.h> //gvx64 rollforward to 5.0-12188 - implement .rvz support
#include <mbedtls/sha1.h> //gvx64 rollforward to 5.0-12188 - implement .rvz support

#include "Common/Align.h" //gvx64 rollforward to 5.0-12188 - implement .rvz support
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Swap.h"

#include "DiscIO/Blob.h"
#include "DiscIO/Enums.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"

namespace DiscIO
{
constexpr u64 PARTITION_DATA_OFFSET = 0x20000;

VolumeWii::VolumeWii(std::unique_ptr<BlobReader> reader)
    : m_pReader(std::move(reader)), m_game_partition(PARTITION_NONE),
      m_last_decrypted_block(UINT64_MAX)
{
  _assert_(m_pReader);


  m_encrypted = m_pReader->ReadSwapped<u32>(0x60) == u32(0); //gvx64 rollforward to 5.0-12188 - implement .rvz support
//gvx64  if (m_pReader->ReadSwapped<u32>(0x60) != u32(0))
  if ( !m_encrypted ) //gvx64 rollforward to 5.0-12188 - implement .rvz support
  {
    // No partitions - just read unencrypted data like with a GC disc
    return;
  }

  // Get tickets, TMDs, and decryption keys for all partitions
  for (u32 partition_group = 0; partition_group < 4; ++partition_group)
  {
    const std::optional<u32> number_of_partitions =
        m_pReader->ReadSwapped<u32>(0x40000 + (partition_group * 8));
    if (!number_of_partitions)
      continue;

    std::optional<u32> read_buffer =
        m_pReader->ReadSwapped<u32>(0x40000 + (partition_group * 8) + 4);
    if (!read_buffer)
      continue;
    const u64 partition_table_offset = static_cast<u64>(*read_buffer) << 2;

    for (u32 i = 0; i < number_of_partitions; i++)
    {
      // Read the partition offset
      read_buffer = m_pReader->ReadSwapped<u32>(partition_table_offset + (i * 8));
      if (!read_buffer)
        continue;
      const u64 partition_offset = static_cast<u64>(*read_buffer) << 2;

      // Check if this is the game partition
      const bool is_game_partition =
          m_game_partition == PARTITION_NONE &&
          m_pReader->ReadSwapped<u32>(partition_table_offset + (i * 8) + 4) == u32(0);

      // Read ticket
      std::vector<u8> ticket_buffer(sizeof(IOS::ES::Ticket));
      if (!m_pReader->Read(partition_offset, ticket_buffer.size(), ticket_buffer.data()))
        continue;
      IOS::ES::TicketReader ticket{std::move(ticket_buffer)};
      if (!ticket.IsValid())
        continue;

      // Read TMD
      const std::optional<u32> tmd_size = m_pReader->ReadSwapped<u32>(partition_offset + 0x2a4);
      std::optional<u32> tmd_address = m_pReader->ReadSwapped<u32>(partition_offset + 0x2a8);
      if (!tmd_size || !tmd_address)
        continue;
      *tmd_address <<= 2;
      if (!IOS::ES::IsValidTMDSize(*tmd_size))
      {
        // This check is normally done by ES in ES_DiVerify, but that would happen too late
        // (after allocating the buffer), so we do the check here.
        PanicAlert("Invalid TMD size");
        continue;
      }
      std::vector<u8> tmd_buffer(*tmd_size);
      if (!m_pReader->Read(partition_offset + *tmd_address, *tmd_size, tmd_buffer.data()))
        continue;
      IOS::ES::TMDReader tmd{std::move(tmd_buffer)};

      // Get the decryption key
      const std::array<u8, 16> key = ticket.GetTitleKey();
      std::unique_ptr<mbedtls_aes_context> aes_context = std::make_unique<mbedtls_aes_context>();
      mbedtls_aes_setkey_dec(aes_context.get(), key.data(), 128);

      // We've read everything. Time to store it! (The reason we don't store anything
      // earlier is because we want to be able to skip adding the partition if an error occurs.)
      const Partition partition(partition_offset);
      m_partition_keys[partition] = std::move(aes_context);
      m_partition_tickets[partition] = std::move(ticket);
      m_partition_tmds[partition] = std::move(tmd);
      if (is_game_partition)
        m_game_partition = partition;
    }
  }
}

VolumeWii::~VolumeWii()
{
}

bool VolumeWii::Read(u64 _ReadOffset, u64 _Length, u8* _pBuffer, const Partition& partition) const
{
  if (partition == PARTITION_NONE)
    return m_pReader->Read(_ReadOffset, _Length, _pBuffer);

  // Get the decryption key for the partition
  auto it = m_partition_keys.find(partition);
  if (it == m_partition_keys.end())
    return false;
  mbedtls_aes_context* aes_context = it->second.get();

  std::vector<u8> read_buffer(BLOCK_TOTAL_SIZE);

  const u64 partition_data_offset = partition.offset + PARTITION_DATA_OFFSET; //gvx64 correct Wii rvz support
  if (m_pReader->SupportsReadWiiDecrypted()) //gvx64 correct Wii rvz support
    return m_pReader->ReadWiiDecrypted(_ReadOffset, _Length, _pBuffer, partition_data_offset); //gvx64 correct Wii rvz support

  while (_Length > 0)
  {
    // Calculate offsets
    u64 block_offset_on_disc =
        partition.offset + PARTITION_DATA_OFFSET + _ReadOffset / BLOCK_DATA_SIZE * BLOCK_TOTAL_SIZE;
    u64 data_offset_in_block = _ReadOffset % BLOCK_DATA_SIZE;

    if (m_last_decrypted_block != block_offset_on_disc)
    {
      // Read the current block
      if (!m_pReader->Read(block_offset_on_disc, BLOCK_TOTAL_SIZE, read_buffer.data()))
        return false;

      // Decrypt the block's data.
      // 0x3D0 - 0x3DF in read_buffer will be overwritten,
      // but that won't affect anything, because we won't
      // use the content of read_buffer anymore after this
      mbedtls_aes_crypt_cbc(aes_context, MBEDTLS_AES_DECRYPT, BLOCK_DATA_SIZE, &read_buffer[0x3D0],
                            &read_buffer[BLOCK_HEADER_SIZE], m_last_decrypted_block_data);
      m_last_decrypted_block = block_offset_on_disc;

      // The only thing we currently use from the 0x000 - 0x3FF part
      // of the block is the IV (at 0x3D0), but it also contains SHA-1
      // hashes that IOS uses to check that discs aren't tampered with.
      // http://wiibrew.org/wiki/Wii_Disc#Encrypted
    }

    // Copy the decrypted data
    u64 copy_size = std::min(_Length, BLOCK_DATA_SIZE - data_offset_in_block);
    memcpy(_pBuffer, &m_last_decrypted_block_data[data_offset_in_block],
           static_cast<size_t>(copy_size));

    // Update offsets
    _Length -= copy_size;
    _pBuffer += copy_size;
    _ReadOffset += copy_size;
  }

  return true;
}

bool VolumeWii::IsEncryptedAndHashed() const //gvx64 rollforward to 5.0-12188 - implement .rvz support
{
  return m_encrypted;
}


std::vector<Partition> VolumeWii::GetPartitions() const
{
  std::vector<Partition> partitions;
  for (const auto& pair : m_partition_keys)
    partitions.push_back(pair.first);
  return partitions;
}

Partition VolumeWii::GetGamePartition() const
{
  return m_game_partition;
}

std::optional<u64> VolumeWii::GetTitleID(const Partition& partition) const
{
  const IOS::ES::TicketReader& ticket = GetTicket(partition);
  if (!ticket.IsValid())
    return {};
  return ticket.GetTitleId();
}

const IOS::ES::TicketReader& VolumeWii::GetTicket(const Partition& partition) const
{
  auto it = m_partition_tickets.find(partition);
  return it != m_partition_tickets.end() ? it->second : INVALID_TICKET;
}

const IOS::ES::TMDReader& VolumeWii::GetTMD(const Partition& partition) const
{
  auto it = m_partition_tmds.find(partition);
  return it != m_partition_tmds.end() ? it->second : INVALID_TMD;
}

//gvx64 u64 VolumeWii::PartitionOffsetToRawOffset(u64 offset, const Partition& partition)
u64 VolumeWii::PartitionOffsetToRawOffset(u64 offset, const Partition& partition) const //gvx64 rollforward to 5.0-12188 - implement .rvz support
{
  if (partition == PARTITION_NONE)
    return offset;

  return partition.offset + PARTITION_DATA_OFFSET + (offset / BLOCK_DATA_SIZE * BLOCK_TOTAL_SIZE) +
         (offset % BLOCK_DATA_SIZE);
}

std::string VolumeWii::GetGameID(const Partition& partition) const
{
  char ID[6];

  if (!Read(0, 6, (u8*)ID, partition))
    return std::string();

  return DecodeString(ID);
}

Region VolumeWii::GetRegion() const
{
  const std::optional<u32> region_code = m_pReader->ReadSwapped<u32>(0x4E000);
  return region_code ? static_cast<Region>(*region_code) : Region::UNKNOWN_REGION;
}

Country VolumeWii::GetCountry(const Partition& partition) const
{
  // The 0 that we use as a default value is mapped to COUNTRY_UNKNOWN and UNKNOWN_REGION
  u8 country_byte = ReadSwapped<u8>(3, partition).value_or(0);
  const Region region = GetRegion();

  if (RegionSwitchWii(country_byte) != region)
    return TypicalCountryForRegion(region);

  return CountrySwitch(country_byte);
}

std::string VolumeWii::GetMakerID(const Partition& partition) const
{
  char makerID[2];

  if (!Read(0x4, 0x2, (u8*)&makerID, partition))
    return std::string();

  return DecodeString(makerID);
}

std::optional<u16> VolumeWii::GetRevision(const Partition& partition) const
{
  std::optional<u8> revision = ReadSwapped<u8>(7, partition);
  return revision ? *revision : std::optional<u16>();
}

std::string VolumeWii::GetInternalName(const Partition& partition) const
{
  char name_buffer[0x60];
  if (Read(0x20, 0x60, (u8*)&name_buffer, partition))
    return DecodeString(name_buffer);

  return "";
}

std::map<Language, std::string> VolumeWii::GetLongNames() const
{
  std::unique_ptr<FileSystem> file_system(CreateFileSystem(this, GetGamePartition()));
  if (!file_system)
    return {};

  std::vector<u8> opening_bnr(NAMES_TOTAL_BYTES);
  std::unique_ptr<FileInfo> file_info = file_system->FindFileInfo("opening.bnr");
  opening_bnr.resize(
      file_system->ReadFile(file_info.get(), opening_bnr.data(), opening_bnr.size(), 0x5C));
  return ReadWiiNames(opening_bnr);
}

std::vector<u32> VolumeWii::GetBanner(int* width, int* height) const
{
  *width = 0;
  *height = 0;

  const std::optional<u64> title_id = GetTitleID(GetGamePartition());
  if (!title_id)
    return std::vector<u32>();

  return GetWiiBanner(width, height, *title_id);
}

std::string VolumeWii::GetApploaderDate(const Partition& partition) const
{
  char date[16];

  if (!Read(0x2440, 0x10, (u8*)&date, partition))
    return std::string();

  return DecodeString(date);
}

Platform VolumeWii::GetVolumeType() const
{
  return Platform::WII_DISC;
}

std::optional<u8> VolumeWii::GetDiscNumber(const Partition& partition) const
{
  return ReadSwapped<u8>(6, partition);
}

BlobType VolumeWii::GetBlobType() const
{
  return m_pReader->GetBlobType();
}

u64 VolumeWii::GetSize() const
{
  return m_pReader->GetDataSize();
}

u64 VolumeWii::GetRawSize() const
{
  return m_pReader->GetRawSize();
}

bool VolumeWii::CheckIntegrity(const Partition& partition) const
{
  // Get the decryption key for the partition
  auto it = m_partition_keys.find(partition);
  if (it == m_partition_keys.end())
    return false;
  mbedtls_aes_context* aes_context = it->second.get();

  // Get partition data size
  u32 partSizeDiv4;
  m_pReader->Read(partition.offset + 0x2BC, 4, (u8*)&partSizeDiv4);
  u64 partDataSize = (u64)Common::swap32(partSizeDiv4) * 4;

  u32 nClusters = (u32)(partDataSize / 0x8000);
  for (u32 clusterID = 0; clusterID < nClusters; ++clusterID)
  {
    u64 clusterOff = partition.offset + PARTITION_DATA_OFFSET + (u64)clusterID * 0x8000;

    // Read and decrypt the cluster metadata
    u8 clusterMDCrypted[0x400];
    u8 clusterMD[0x400];
    u8 IV[16] = {0};
    if (!m_pReader->Read(clusterOff, 0x400, clusterMDCrypted))
    {
      WARN_LOG(DISCIO, "Integrity Check: fail at cluster %d: could not read metadata", clusterID);
      return false;
    }
    mbedtls_aes_crypt_cbc(aes_context, MBEDTLS_AES_DECRYPT, 0x400, IV, clusterMDCrypted, clusterMD);

    // Some clusters have invalid data and metadata because they aren't
    // meant to be read by the game (for example, holes between files). To
    // try to avoid reporting errors because of these clusters, we check
    // the 0x00 paddings in the metadata.
    //
    // This may cause some false negatives though: some bad clusters may be
    // skipped because they are *too* bad and are not even recognized as
    // valid clusters. To be improved.
    bool meaningless = false;
    for (u32 idx = 0x26C; idx < 0x280; ++idx)
      if (clusterMD[idx] != 0)
        meaningless = true;

    if (meaningless)
      continue;

    u8 clusterData[0x7C00];
    if (!Read((u64)clusterID * 0x7C00, 0x7C00, clusterData, partition))
    {
      WARN_LOG(DISCIO, "Integrity Check: fail at cluster %d: could not read data", clusterID);
      return false;
    }

    for (u32 hashID = 0; hashID < 31; ++hashID)
    {
      u8 hash[20];

      mbedtls_sha1(clusterData + hashID * 0x400, 0x400, hash);

      // Note that we do not use strncmp here
      if (memcmp(hash, clusterMD + hashID * 20, 20))
      {
        WARN_LOG(DISCIO, "Integrity Check: fail at cluster %d: hash %d is invalid", clusterID,
                 hashID);
        return false;
      }
    }
  }

  return true;
}

//gvx64 rollforward to 5.0-12188 - implement .rvz support (new content through to end of file)
bool VolumeWii::HashGroup(const std::array<u8, BLOCK_DATA_SIZE> in[BLOCKS_PER_GROUP],
                          HashBlock out[BLOCKS_PER_GROUP],
                          const std::function<bool(size_t block)>& read_function)
{
  std::array<std::future<void>, BLOCKS_PER_GROUP> hash_futures;
  bool success = true;

  for (size_t i = 0; i < BLOCKS_PER_GROUP; ++i)
  {
    if (read_function && success)
      success = read_function(i);

    hash_futures[i] = std::async(std::launch::async, [&in, &out, &hash_futures, success, i]() {
      const size_t h1_base = Common::AlignDown(i, 8);

      if (success)
      {
        // H0 hashes
        for (size_t j = 0; j < 31; ++j)
          mbedtls_sha1_ret(in[i].data() + j * 0x400, 0x400, out[i].h0[j]);

        // H0 padding
        std::memset(out[i].padding_0, 0, sizeof(HashBlock::padding_0));

        // H1 hash
        mbedtls_sha1_ret(reinterpret_cast<u8*>(out[i].h0), sizeof(HashBlock::h0),
                         out[h1_base].h1[i - h1_base]);
      }

      if (i % 8 == 7)
      {
        for (size_t j = 0; j < 7; ++j)
          hash_futures[h1_base + j].get();

        if (success)
        {
          // H1 padding
          std::memset(out[h1_base].padding_1, 0, sizeof(HashBlock::padding_1));

          // H1 copies
          for (size_t j = 1; j < 8; ++j)
            std::memcpy(out[h1_base + j].h1, out[h1_base].h1, sizeof(HashBlock::h1));

          // H2 hash
          mbedtls_sha1_ret(reinterpret_cast<u8*>(out[i].h1), sizeof(HashBlock::h1),
                           out[0].h2[h1_base / 8]);
        }

        if (i == BLOCKS_PER_GROUP - 1)
        {
          for (size_t j = 0; j < 7; ++j)
            hash_futures[j * 8 + 7].get();

          if (success)
          {
            // H2 padding
            std::memset(out[0].padding_2, 0, sizeof(HashBlock::padding_2));

            // H2 copies
            for (size_t j = 1; j < BLOCKS_PER_GROUP; ++j)
              std::memcpy(out[j].h2, out[0].h2, sizeof(HashBlock::h2));
          }
        }
      }
    });
  }

  // Wait for all the async tasks to finish
  hash_futures.back().get();

  return success;
}

bool VolumeWii::EncryptGroup(
    u64 offset, u64 partition_data_offset, u64 partition_data_decrypted_size,
    const std::array<u8, AES_KEY_SIZE>& key, BlobReader* blob,
    std::array<u8, GROUP_TOTAL_SIZE>* out,
    const std::function<void(HashBlock hash_blocks[BLOCKS_PER_GROUP])>& hash_exception_callback)
{
  std::vector<std::array<u8, BLOCK_DATA_SIZE>> unencrypted_data(BLOCKS_PER_GROUP);
  std::vector<HashBlock> unencrypted_hashes(BLOCKS_PER_GROUP);

  const bool success =
      HashGroup(unencrypted_data.data(), unencrypted_hashes.data(), [&](size_t block) {
        if (offset + (block + 1) * BLOCK_DATA_SIZE <= partition_data_decrypted_size)
        {
          if (!blob->ReadWiiDecrypted(offset + block * BLOCK_DATA_SIZE, BLOCK_DATA_SIZE,
                                      unencrypted_data[block].data(), partition_data_offset))
          {
            return false;
          }
        }
        else
        {
          unencrypted_data[block].fill(0);
        }
        return true;
      });

  if (!success)
    return false;

  if (hash_exception_callback)
    hash_exception_callback(unencrypted_hashes.data());

  const unsigned int threads =
      std::min(BLOCKS_PER_GROUP, std::max<unsigned int>(1, std::thread::hardware_concurrency()));

  std::vector<std::future<void>> encryption_futures(threads);

  mbedtls_aes_context aes_context;
  mbedtls_aes_setkey_enc(&aes_context, key.data(), 128);

  for (size_t i = 0; i < threads; ++i)
  {
    encryption_futures[i] = std::async(
        std::launch::async,
        [&unencrypted_data, &unencrypted_hashes, &aes_context, &out](size_t start, size_t end) {
          for (size_t i = start; i < end; ++i)
          {
            u8* out_ptr = out->data() + i * BLOCK_TOTAL_SIZE;

            u8 iv[16] = {};
            mbedtls_aes_crypt_cbc(&aes_context, MBEDTLS_AES_ENCRYPT, BLOCK_HEADER_SIZE, iv,
                                  reinterpret_cast<u8*>(&unencrypted_hashes[i]), out_ptr);

            std::memcpy(iv, out_ptr + 0x3D0, sizeof(iv));
            mbedtls_aes_crypt_cbc(&aes_context, MBEDTLS_AES_ENCRYPT, BLOCK_DATA_SIZE, iv,
                                  unencrypted_data[i].data(), out_ptr + BLOCK_HEADER_SIZE);
          }
        },
        i * BLOCKS_PER_GROUP / threads, (i + 1) * BLOCKS_PER_GROUP / threads);
  }

  for (std::future<void>& future : encryption_futures)
    future.get();

  return true;
}

void VolumeWii::DecryptBlockHashes(const u8* in, HashBlock* out, mbedtls_aes_context* aes_context)
{
  std::array<u8, 16> iv;
  iv.fill(0);
  mbedtls_aes_crypt_cbc(aes_context, MBEDTLS_AES_DECRYPT, sizeof(HashBlock), iv.data(), in,
                        reinterpret_cast<u8*>(out));
}

void VolumeWii::DecryptBlockData(const u8* in, u8* out, mbedtls_aes_context* aes_context)
{
  std::array<u8, 16> iv;
  std::copy(&in[0x3d0], &in[0x3e0], iv.data());
  mbedtls_aes_crypt_cbc(aes_context, MBEDTLS_AES_DECRYPT, BLOCK_DATA_SIZE, iv.data(),
                        &in[BLOCK_HEADER_SIZE], out);
}

}  // namespace
