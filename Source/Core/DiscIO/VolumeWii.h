// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <mbedtls/aes.h>
#include <functional> //gvx64 rollforward to 5.0-12188 - implement .rvz support
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/IOS/ES/Formats.h"
#include "DiscIO/Volume.h"

// --- this volume type is used for encrypted Wii images ---

namespace DiscIO
{
class BlobReader;
enum class BlobType;
enum class Country;
enum class Language;
enum class Region;
enum class Platform;

//gvx64 class VolumeWii : public VolumeDisc
class VolumeWii : public VolumeDisc //gvx64 rollforward to 5.0-12188 - implement .rvz support
{
public:
  static constexpr size_t AES_KEY_SIZE = 16; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr size_t SHA1_SIZE = 20; //gvx64 rollforward to 5.0-12188 - implement .rvz support

  static constexpr u32 H3_TABLE_SIZE = 0x18000; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr u32 BLOCKS_PER_GROUP = 0x40; //gvx64 rollforward to 5.0-12188 - implement .rvz support

  static constexpr u64 BLOCK_HEADER_SIZE = 0x0400; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr u64 BLOCK_DATA_SIZE = 0x7C00; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr u64 BLOCK_TOTAL_SIZE = BLOCK_HEADER_SIZE + BLOCK_DATA_SIZE; //gvx64 rollforward to 5.0-12188 - implement .rvz support

  static constexpr u64 GROUP_HEADER_SIZE = BLOCK_HEADER_SIZE * BLOCKS_PER_GROUP; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr u64 GROUP_DATA_SIZE = BLOCK_DATA_SIZE * BLOCKS_PER_GROUP; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static constexpr u64 GROUP_TOTAL_SIZE = GROUP_HEADER_SIZE + GROUP_DATA_SIZE; //gvx64 rollforward to 5.0-12188 - implement .rvz support

  struct HashBlock  //gvx64 rollforward to 5.0-12188 - implement .rvz support
  {
    u8 h0[31][SHA1_SIZE];
    u8 padding_0[20];
    u8 h1[8][SHA1_SIZE];
    u8 padding_1[32];
    u8 h2[8][SHA1_SIZE];
    u8 padding_2[32];
  };
  static_assert(sizeof(HashBlock) == BLOCK_HEADER_SIZE);//gvx64 rollforward to 5.0-12188 - implement .rvz support

  VolumeWii(std::unique_ptr<BlobReader> reader);
  ~VolumeWii();
  bool Read(u64 _Offset, u64 _Length, u8* _pBuffer, const Partition& partition) const override;
  bool IsEncryptedAndHashed() const override; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  std::vector<Partition> GetPartitions() const override;
  Partition GetGamePartition() const override;
  std::optional<u64> GetTitleID(const Partition& partition) const override;
  const IOS::ES::TicketReader& GetTicket(const Partition& partition) const override;
  const IOS::ES::TMDReader& GetTMD(const Partition& partition) const override;
  const FileSystem* GetFileSystem(const Partition& partition) const override {}; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  static u64 EncryptedPartitionOffsetToRawOffset(u64 offset, const Partition& partition,
                                                 u64 partition_data_offset); //gvx64 rollforward to 5.0-12188 - implement .rvz support
  u64 PartitionOffsetToRawOffset(u64 offset, const Partition& partition) const override;  //gvx64 rollforward to 5.0-12188 - implement .rvz support
  std::string GetGameID(const Partition& partition) const override;
  std::string GetMakerID(const Partition& partition) const override;
  std::optional<u16> GetRevision(const Partition& partition) const override;
  std::string GetInternalName(const Partition& partition) const override;
  std::map<Language, std::string> GetLongNames() const override;
  std::vector<u32> GetBanner(int* width, int* height) const override;
  std::string GetApploaderDate(const Partition& partition) const override;
  std::optional<u8> GetDiscNumber(const Partition& partition) const override;

  Platform GetVolumeType() const override;
  bool SupportsIntegrityCheck() const override { return true; }
  bool CheckIntegrity(const Partition& partition) const override;

  Region GetRegion() const override;
  Country GetCountry(const Partition& partition) const override;
  BlobType GetBlobType() const override;
  u64 GetSize() const override;
  u64 GetRawSize() const override;


  // The in parameter can either contain all the data to begin with,
  // or read_function can write data into the in parameter when called.
  // The latter lets reading run in parallel with hashing.
  // This function returns false iff read_function returns false.
  static bool HashGroup(const std::array<u8, BLOCK_DATA_SIZE> in[BLOCKS_PER_GROUP],
                        HashBlock out[BLOCKS_PER_GROUP],
                        const std::function<bool(size_t block)>& read_function = {});  //gvx64 rollforward to 5.0-12188 - implement .rvz support

  static bool EncryptGroup(u64 offset, u64 partition_data_offset, u64 partition_data_decrypted_size,
                           const std::array<u8, AES_KEY_SIZE>& key, BlobReader* blob,
                           std::array<u8, GROUP_TOTAL_SIZE>* out,
                           const std::function<void(HashBlock hash_blocks[BLOCKS_PER_GROUP])>&
                               hash_exception_callback = {});  //gvx64 rollforward to 5.0-12188 - implement .rvz support

  static void DecryptBlockHashes(const u8* in, HashBlock* out, mbedtls_aes_context* aes_context);
  static void DecryptBlockData(const u8* in, u8* out, mbedtls_aes_context* aes_context);


  //gvx64 static u64 PartitionOffsetToRawOffset(u64 offset, const Partition& partition);

//gvx64  static constexpr unsigned int BLOCK_HEADER_SIZE = 0x0400;
//gvx64  static constexpr unsigned int BLOCK_DATA_SIZE = 0x7C00;
//gvx64  static constexpr unsigned int BLOCK_TOTAL_SIZE = BLOCK_HEADER_SIZE + BLOCK_DATA_SIZE;

private:
  std::unique_ptr<BlobReader> m_pReader;
  std::map<Partition, std::unique_ptr<mbedtls_aes_context>> m_partition_keys;
  std::map<Partition, IOS::ES::TicketReader> m_partition_tickets;
  std::map<Partition, IOS::ES::TMDReader> m_partition_tmds;
  Partition m_game_partition;
  bool m_encrypted; //gvx64 rollforward to 5.0-12188 - implement .rvz support

  mutable u64 m_last_decrypted_block;
  mutable u8 m_last_decrypted_block_data[BLOCK_DATA_SIZE];
};

}  // namespace
