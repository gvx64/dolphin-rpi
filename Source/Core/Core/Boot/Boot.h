// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Common/CommonTypes.h"
#include "DiscIO/Enums.h"
#include "DiscIO/Volume.h"

struct RegionSetting
{
  const std::string area;
  const std::string video;
  const std::string game;
  const std::string code;
};

class BootExecutableReader;

struct BootParameters
{
  struct Disc
  {
    std::string path;
    std::unique_ptr<DiscIO::Volume> volume;
  };

  struct Executable
  {
    std::string path;
    std::unique_ptr<BootExecutableReader> reader;
  };

  struct NAND
  {
    std::string content_path;
  };

  struct IPL
  {
    explicit IPL(DiscIO::Region region_);
    IPL(DiscIO::Region region_, Disc&& disc_);
    std::string path;
    DiscIO::Region region;
    // It is possible to boot the IPL with a disc inserted (with "skip IPL" disabled).
    std::optional<Disc> disc;
  };

  struct DFF
  {
    std::string dff_path;
  };

  static std::unique_ptr<BootParameters> GenerateFromFile(const std::string& path);

  using Parameters = std::variant<Disc, Executable, NAND, IPL, DFF>;
  BootParameters(Parameters&& parameters_);

  Parameters parameters;
};

class CBoot
{
public:
  static bool BootUp(std::unique_ptr<BootParameters> boot);

  // Tries to find a map file for the current game by looking first in the
  // local user directory, then in the shared user directory.
  //
  // If existing_map_file is not nullptr and a map file exists, it is set to the
  // path to the existing map file.
  //
  // If writable_map_file is not nullptr, it is set to the path to where a map
  // file should be saved.
  //
  // Returns true if a map file exists, false if none could be found.
  static bool FindMapFile(std::string* existing_map_file, std::string* writable_map_file);
  static bool LoadMapFromFilename();

private:
  static bool DVDRead(const DiscIO::Volume& volume, u64 dvd_offset, u32 output_address, u32 length,
                      const DiscIO::Partition& partition);
  static void RunFunction(u32 address);

  static void UpdateDebugger_MapLoaded();

  static bool Boot_WiiWAD(const std::string& filename);

  static void SetupMSR();
  static void SetupBAT(bool is_wii);
  static bool RunApploader(bool is_wii, const DiscIO::Volume& volume);
  static bool EmulatedBS2_GC(const DiscIO::Volume* volume, bool skip_app_loader = false);
  static bool EmulatedBS2_Wii(const DiscIO::Volume* volume);
  static bool EmulatedBS2(bool is_wii, const DiscIO::Volume* volume);
  static bool Load_BS2(const std::string& boot_rom_filename);
  static void Load_FST(bool is_wii, const DiscIO::Volume* volume);

  static bool SetupWiiMemory(const DiscIO::Volume* volume, u64 ios_title_id);
};

class BootExecutableReader
{
public:
  explicit BootExecutableReader(const std::string& file_name);
  explicit BootExecutableReader(const std::vector<u8>& buffer);
  virtual ~BootExecutableReader();

  virtual u32 GetEntryPoint() const = 0;
  virtual bool IsValid() const = 0;
  virtual bool IsWii() const = 0;
  virtual bool LoadIntoMemory(bool only_in_mem1 = false) const = 0;
  virtual bool LoadSymbols() const = 0;

protected:
  std::vector<u8> m_bytes;
};
