// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

namespace DiscIO
{
class FileInfo;
struct Partition;
class Volume;

constexpr u32 PARTITION_DATA = 0;
constexpr u32 PARTITION_UPDATE = 1;
constexpr u32 PARTITION_CHANNEL = 2;  // Mario Kart Wii, Wii Fit, Wii Fit Plus, Rabbids Go Home
constexpr u32 PARTITION_INSTALL = 3;  // Dragon Quest X only

std::string NameForPartitionType(u32 partition_type, bool include_prefix);

u64 ReadFile(const Volume& volume, const Partition& partition, const FileInfo* file_info,
             u8* buffer, u64 max_buffer_size, u64 offset_in_file = 0);
u64 ReadFile(const Volume& volume, const Partition& partition, std::string_view path, u8* buffer,
             u64 max_buffer_size, u64 offset_in_file = 0);
bool ExportData(const Volume& volume, const Partition& partition, u64 offset, u64 size,
                const std::string& export_filename);
bool ExportFile(const Volume& volume, const Partition& partition, const FileInfo* file_info,
                const std::string& export_filename);
bool ExportFile(const Volume& volume, const Partition& partition, std::string_view path,
                const std::string& export_filename);

// update_progress is called once for each child (file or directory).
// If update_progress returns true, the extraction gets cancelled.
// filesystem_path is supposed to be the path corresponding to the directory argument.
void ExportDirectory(const Volume& volume, const Partition& partition, const FileInfo& directory,
                     bool recursive, const std::string& filesystem_path,
                     const std::string& export_folder,
                     const std::function<bool(const std::string& path)>& update_progress);

// To export everything listed below, you can use ExportSystemData

bool ExportWiiUnencryptedHeader(const Volume& volume, const std::string& export_filename);
bool ExportWiiRegionData(const Volume& volume, const std::string& export_filename);

bool ExportTicket(const Volume& volume, const Partition& partition,
                  const std::string& export_filename);
bool ExportTMD(const Volume& volume, const Partition& partition,
               const std::string& export_filename);
bool ExportCertificateChain(const Volume& volume, const Partition& partition,
                            const std::string& export_filename);
bool ExportH3Hashes(const Volume& volume, const Partition& partition,
                    const std::string& export_filename);

bool ExportHeader(const Volume& volume, const Partition& partition,
                  const std::string& export_filename);
bool ExportBI2Data(const Volume& volume, const Partition& partition,
                   const std::string& export_filename);
bool ExportApploader(const Volume& volume, const Partition& partition,
                     const std::string& export_filename);
std::optional<u64> GetBootDOLOffset(const Volume& volume, const Partition& partition);
std::optional<u32> GetBootDOLSize(const Volume& volume, const Partition& partition, u64 dol_offset);
bool ExportDOL(const Volume& volume, const Partition& partition,
               const std::string& export_filename);
//gvx64 std::optional<u64> GetFSTOffset(const Volume& volume, const Partition& partition) {};
std::optional<u64> GetFSTOffset(const Volume& volume, const Partition& partition) {}; //gvx64 rollforward to 5.0-12188 - implement .rvz support
std::optional<u64> GetFSTSize(const Volume& volume, const Partition& partition);
bool ExportFST(const Volume& volume, const Partition& partition,
               const std::string& export_filename);

bool ExportSystemData(const Volume& volume, const Partition& partition,
                      const std::string& export_folder);

}  // namespace DiscIO
