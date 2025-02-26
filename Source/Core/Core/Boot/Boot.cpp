// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/Boot/Boot.h"


#ifdef _MSC_VER //gvx64 roll-forward to 5.0-9343 to introduce m3u support
#include <experimental/filesystem> //gvx64 roll-forward to 5.0-9343 to introduce m3u support
namespace fs = std::experimental::filesystem; //gvx64 roll-forward to 5.0-9343 to introduce m3u support
#define HAS_STD_FILESYSTEM //gvx64 roll-forward to 5.0-9343 to introduce m3u support
#endif //gvx64 roll-forward to 5.0-9343 to introduce m3u support

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "Common/Assert.h" //gvx64 roll-forward to 5.0-9343 to introduce m3u file support
#include "Common/Align.h"
#include "Common/CDUtils.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/File.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"

#include "Core/Boot/DolReader.h"
#include "Core/Boot/ElfReader.h"
#include "Core/ConfigManager.h"
#include "Core/FifoPlayer/FifoPlayer.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI_DeviceIPL.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/VideoInterface.h"
#include "Core/Host.h"
#include "Core/IOS/IOS.h"
#include "Core/PatchEngine.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"

#include "DiscIO/Enums.h"
#include "DiscIO/NANDContentLoader.h"
#include "DiscIO/Volume.h"

//gvx64 roll-forward to 5.0-9343 to introduce m3u support
std::vector<std::string> ReadM3UFile(const std::string& m3u_path, const std::string& folder_path)
{
#ifndef HAS_STD_FILESYSTEM
  _assert_(folder_path.back() == '/');
#endif
  std::vector<std::string> result;
  std::vector<std::string> nonexistent;
  std::ifstream s;
  File::OpenFStream(s, m3u_path, std::ios_base::in);
  std::string line;
  while (std::getline(s, line))
  {
    if (StringBeginsWith(line, u8"\uFEFF"))
    {
      WARN_LOG(BOOT, "UTF-8 BOM in file: %s", m3u_path.c_str());
      line.erase(0, 3);
    }
    if (!line.empty() && line.front() != '#')  // Comments start with #
    {
#ifdef HAS_STD_FILESYSTEM
      const fs::path path_line = fs::u8path(line);
      const std::string path_to_add =
          path_line.is_relative() ? fs::u8path(folder_path).append(path_line).u8string() : line;
#else
      const std::string path_to_add = line.front() != '/' ? folder_path + line : line;
#endif
      (File::Exists(path_to_add) ? result : nonexistent).push_back(path_to_add);
    }
  }
  if (!nonexistent.empty())
  {
    PanicAlertT("Files specified in the M3U file \"%s\" were not found:\n%s", m3u_path.c_str(),
                JoinStrings(nonexistent, "\n").c_str());
    return {};
  }
  if (result.empty())
    PanicAlertT("No paths found in the M3U file \"%s\"", m3u_path.c_str());
  return result;
}
//gvx64 roll-forward to 5.0-9343 to introduce m3u support

BootParameters::BootParameters(Parameters&& parameters_) : parameters(std::move(parameters_))
{
}

std::unique_ptr<BootParameters> 
//gvx64 BootParameters::GenerateFromFile(const std::string& path)
BootParameters::GenerateFromFile(std::string boot_path) //gvx64 roll-forward to 5.0-9343 to introduce m3u support
{
//gvx64  const bool is_drive = cdio_is_cdrom(path);
  return GenerateFromFile(std::vector<std::string>{std::move(boot_path)}); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
}

std::unique_ptr<BootParameters>
BootParameters::GenerateFromFile(std::vector<std::string> paths)
{
//gvx64  printf("../Source/Core/Core/Boot/Boot.cpp, GenerateFromFile, paths.front().c_str() - %s \n", paths.front().c_str()); //gvx64
  _assert_(!paths.empty());
  const bool is_drive = cdio_is_cdrom(paths.front());

  // Check if the file exist, we may have gotten it from a --elf command line
  // that gave an incorrect file name
//gvx64  if (!is_drive && !File::Exists(path))
  if (!is_drive && !File::Exists(paths.front())) //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  {
//gvx64    PanicAlertT("The specified file \"%s\" does not exist", path.c_str());
    PanicAlertT("The specified file \"%s\" does not exist", paths.front().c_str()); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
    return {};
  }

  std::string folder_path; //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  std::string extension;
//gvx64  SplitPath(path, nullptr, nullptr, &extension);
  SplitPath(paths.front(), &folder_path, nullptr, &extension); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);


//gvx64 roll-forward to 5.0-9343 to introduce m3u support
  if (extension == ".m3u" || extension == ".m3u8")
  {
    paths = ReadM3UFile(paths.front(), folder_path);
    if (paths.empty())
      return {};
    SplitPath(paths.front(), nullptr, nullptr, &extension);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
  }
  const std::string path = paths.front();
  if (paths.size() == 1)
    paths.clear();
//gvx64 roll-forward to 5.0-9343 to introduce m3u support

  static const std::unordered_set<std::string> disc_image_extensions = {
//gvx64      {".gcm", ".iso", ".tgc", ".wbfs", ".ciso", ".gcz"}};
      {".gcm", ".iso", ".tgc", ".wbfs", ".ciso", ".gcz", ".wia", ".rvz", ".m3u"}}; //gvx64 rollforward to 5.0-12188 - implement .rvz support
  if (disc_image_extensions.find(extension) != disc_image_extensions.end() || is_drive)
  {
    auto volume = DiscIO::CreateVolumeFromFilename(path);
    if (!volume)
    {
      if (is_drive)
      {
        PanicAlertT("Could not read \"%s\". "
                    "There is no disc in the drive or it is not a GameCube/Wii backup. "
                    "Please note that Dolphin cannot play games directly from the original "
                    "GameCube and Wii discs.",
                    path.c_str());
      }
      else
      {
        PanicAlertT("\"%s\" is an invalid GCM/ISO file, or is not a GC/Wii ISO.", path.c_str());
      }
      return {};
    }
//gvx64    return std::make_unique<BootParameters>(Disc{path, std::move(volume)});
      return std::make_unique<BootParameters>(Disc{std::move(path), std::move(volume), paths}); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  }

  if (extension == ".elf")
  {
//gvx64    return std::make_unique<BootParameters>(Executable{path, std::make_unique<ElfReader>(path)});
    return std::make_unique<BootParameters>(
      Executable{std::move(path), std::make_unique<ElfReader>(path)}); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  }

  if (extension == ".dol")
  {
//gvx64    return std::make_unique<BootParameters>(Executable{path, std::make_unique<DolReader>(path)});
    return std::make_unique<BootParameters>( 
      Executable{std::move(path), std::make_unique<DolReader>(path)});  //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  }
  if (extension == ".dff")
  {
//gvx64    return std::make_unique<BootParameters>(DFF{path});
    return std::make_unique<BootParameters>(DFF{std::move(path)}); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  }

  if (DiscIO::NANDContentManager::Access().GetNANDLoader(path).IsValid())
    return std::make_unique<BootParameters>(NAND{path});

  PanicAlertT("Could not recognize file %s", path.c_str());
  return {};
}

BootParameters::IPL::IPL(DiscIO::Region region_) : region(region_)
{
  const std::string directory = SConfig::GetInstance().GetDirectoryForRegion(region);
  path = SConfig::GetInstance().GetBootROMPath(directory);
}

BootParameters::IPL::IPL(DiscIO::Region region_, Disc&& disc_) : IPL(region_)
{
  disc = std::move(disc_);
}

// Inserts a disc into the emulated disc drive and returns a pointer to it.
// The returned pointer must only be used while we are still booting,
// because DVDThread can do whatever it wants to the disc after that.
//gvx64 static const DiscIO::Volume* SetDisc(std::unique_ptr<DiscIO::Volume> volume)
static const DiscIO::Volume* SetDisc(std::unique_ptr<DiscIO::Volume> volume,
                                     std::vector<std::string> auto_disc_change_paths = {}) //gvx64 roll-forward to 5.0-9343 to introduce m3u support
{
  const DiscIO::Volume* pointer = volume.get();
//gvx64  DVDInterface::SetDisc(std::move(volume));
  DVDInterface::SetDisc(std::move(volume), auto_disc_change_paths); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
  return pointer;
}

bool CBoot::DVDRead(const DiscIO::Volume& volume, u64 dvd_offset, u32 output_address, u32 length,
                    const DiscIO::Partition& partition)
{
  std::vector<u8> buffer(length);
  if (!volume.Read(dvd_offset, length, buffer.data(), partition))
    return false;
  Memory::CopyToEmu(output_address, buffer.data(), length);
  return true;
}

void CBoot::Load_FST(bool is_wii, const DiscIO::Volume* volume)
{
  if (!volume)
    return;

  const DiscIO::Partition partition = volume->GetGamePartition();

  // copy first 32 bytes of disc to start of Mem 1
  DVDRead(*volume, /*offset*/ 0, /*address*/ 0, /*length*/ 0x20, DiscIO::PARTITION_NONE);

  // copy of game id
  Memory::Write_U32(Memory::Read_U32(0x0000), 0x3180);

  u32 shift = 0;
  if (is_wii)
    shift = 2;

  const std::optional<u32> fst_offset = volume->ReadSwapped<u32>(0x0424, partition);
  const std::optional<u32> fst_size = volume->ReadSwapped<u32>(0x0428, partition);
  const std::optional<u32> max_fst_size = volume->ReadSwapped<u32>(0x042c, partition);
  if (!fst_offset || !fst_size || !max_fst_size)
    return;

  u32 arena_high = Common::AlignDown(0x817FFFFF - (*max_fst_size << shift), 0x20);
  Memory::Write_U32(arena_high, 0x00000034);

  // load FST
  DVDRead(*volume, *fst_offset << shift, arena_high, *fst_size << shift, partition);
  Memory::Write_U32(arena_high, 0x00000038);
  Memory::Write_U32(*max_fst_size << shift, 0x0000003c);

  if (is_wii)
  {
    // the apploader changes IOS MEM1_ARENA_END too
    Memory::Write_U32(arena_high, 0x00003110);
  }
}

void CBoot::UpdateDebugger_MapLoaded()
{
  Host_NotifyMapLoaded();
}

// Get map file paths for the active title.
bool CBoot::FindMapFile(std::string* existing_map_file, std::string* writable_map_file)
{
  const std::string& game_id = SConfig::GetInstance().m_debugger_game_id;

  if (writable_map_file)
    *writable_map_file = File::GetUserPath(D_MAPS_IDX) + game_id + ".map";

  bool found = false;
  static const std::string maps_directories[] = {File::GetUserPath(D_MAPS_IDX),
                                                 File::GetSysDirectory() + MAPS_DIR DIR_SEP};
  for (size_t i = 0; !found && i < ArraySize(maps_directories); ++i)
  {
    std::string path = maps_directories[i] + game_id + ".map";
    if (File::Exists(path))
    {
      found = true;
      if (existing_map_file)
        *existing_map_file = path;
    }
  }

  return found;
}

bool CBoot::LoadMapFromFilename()
{
  std::string strMapFilename;
  bool found = FindMapFile(&strMapFilename, nullptr);
  if (found && g_symbolDB.LoadMap(strMapFilename))
  {
    UpdateDebugger_MapLoaded();
    return true;
  }

  return false;
}

// If ipl.bin is not found, this function does *some* of what BS1 does:
// loading IPL(BS2) and jumping to it.
// It does not initialize the hardware or anything else like BS1 does.
bool CBoot::Load_BS2(const std::string& boot_rom_filename)
{
  // CRC32 hashes of the IPL file; including source where known
  // https://forums.dolphin-emu.org/Thread-unknown-hash-on-ipl-bin?pid=385344#pid385344
  constexpr u32 USA_v1_0 = 0x6D740AE7;
  // https://forums.dolphin-emu.org/Thread-unknown-hash-on-ipl-bin?pid=385334#pid385334
  constexpr u32 USA_v1_1 = 0xD5E6FEEA;
  // https://forums.dolphin-emu.org/Thread-unknown-hash-on-ipl-bin?pid=385399#pid385399
  constexpr u32 USA_v1_2 = 0x86573808;
  // GameCubes sold in Brazil have this IPL. Same as USA v1.2 but localized
  constexpr u32 BRA_v1_0 = 0x667D0B64;
  // Redump
  constexpr u32 JAP_v1_0 = 0x6DAC1F2A;
  // https://bugs.dolphin-emu.org/issues/8936
  constexpr u32 JAP_v1_1 = 0xD235E3F9;
  constexpr u32 JAP_v1_2 = 0x8BDABBD4;
  // Redump
  constexpr u32 PAL_v1_0 = 0x4F319F43;
  // https://forums.dolphin-emu.org/Thread-ipl-with-unknown-hash-dd8cab7c-problem-caused-by-my-pal-gamecube-bios?pid=435463#pid435463
  constexpr u32 PAL_v1_1 = 0xDD8CAB7C;
  // Redump
  constexpr u32 PAL_v1_2 = 0xAD1B7F16;

  // Load the whole ROM dump
  std::string data;
  if (!File::ReadFileToString(boot_rom_filename, data))
    return false;

  // Use zlibs crc32 implementation to compute the hash
  u32 ipl_hash = crc32(0L, Z_NULL, 0);
  ipl_hash = crc32(ipl_hash, (const Bytef*)data.data(), (u32)data.size());
  DiscIO::Region ipl_region;
  switch (ipl_hash)
  {
  case USA_v1_0:
  case USA_v1_1:
  case USA_v1_2:
  case BRA_v1_0:
    ipl_region = DiscIO::Region::NTSC_U;
    break;
  case JAP_v1_0:
  case JAP_v1_1:
  case JAP_v1_2:
    ipl_region = DiscIO::Region::NTSC_J;
    break;
  case PAL_v1_0:
  case PAL_v1_1:
  case PAL_v1_2:
    ipl_region = DiscIO::Region::PAL;
    break;
  default:
    PanicAlertT("IPL with unknown hash %x", ipl_hash);
    ipl_region = DiscIO::Region::UNKNOWN_REGION;
    break;
  }

  const DiscIO::Region boot_region = SConfig::GetInstance().m_region;
  if (ipl_region != DiscIO::Region::UNKNOWN_REGION && boot_region != ipl_region)
    PanicAlertT("%s IPL found in %s directory. The disc might not be recognized",
                SConfig::GetDirectoryForRegion(ipl_region),
                SConfig::GetDirectoryForRegion(boot_region));

  // Run the descrambler over the encrypted section containing BS1/BS2
  ExpansionInterface::CEXIIPL::Descrambler((u8*)data.data() + 0x100, 0x1AFE00);

  // TODO: Execution is supposed to start at 0xFFF00000, not 0x81200000;
  // copying the initial boot code to 0x81200000 is a hack.
  // For now, HLE the first few instructions and start at 0x81200150
  // to work around this.
  Memory::CopyToEmu(0x01200000, data.data() + 0x100, 0x700);
  Memory::CopyToEmu(0x01300000, data.data() + 0x820, 0x1AFE00);

  PowerPC::ppcState.gpr[3] = 0xfff0001f;
  PowerPC::ppcState.gpr[4] = 0x00002030;
  PowerPC::ppcState.gpr[5] = 0x0000009c;

  UReg_MSR& m_MSR = ((UReg_MSR&)PowerPC::ppcState.msr);
  m_MSR.FP = 1;
  m_MSR.DR = 1;
  m_MSR.IR = 1;

  PowerPC::ppcState.spr[SPR_HID0] = 0x0011c464;
  PowerPC::ppcState.spr[SPR_IBAT3U] = 0xfff0001f;
  PowerPC::ppcState.spr[SPR_IBAT3L] = 0xfff00001;
  PowerPC::ppcState.spr[SPR_DBAT3U] = 0xfff0001f;
  PowerPC::ppcState.spr[SPR_DBAT3L] = 0xfff00001;
  SetupBAT(/*is_wii*/ false);

  PC = 0x81200150;
  return true;
}

static const DiscIO::Volume* SetDefaultDisc()
{
  const SConfig& config = SConfig::GetInstance();
  // load default image or create virtual drive from directory
  if (!config.m_strDVDRoot.empty())
    return SetDisc(DiscIO::CreateVolumeFromDirectory(config.m_strDVDRoot, config.bWii));
  if (!config.m_strDefaultISO.empty())
    return SetDisc(DiscIO::CreateVolumeFromFilename(config.m_strDefaultISO));
  return nullptr;
}

// Third boot step after BootManager and Core. See Call schedule in BootManager.cpp
bool CBoot::BootUp(std::unique_ptr<BootParameters> boot)
{
  SConfig& config = SConfig::GetInstance();

  g_symbolDB.Clear();

  // PAL Wii uses NTSC framerate and linecount in 60Hz modes
  VideoInterface::Preset(DiscIO::IsNTSC(config.m_region) || (config.bWii && config.bPAL60));

  struct BootTitle
  {
    BootTitle() : config(SConfig::GetInstance()) {}
    bool operator()(BootParameters::Disc& disc) const
    {
      NOTICE_LOG(BOOT, "Booting from disc: %s", disc.path.c_str());
//gvx64      const DiscIO::Volume* volume = SetDisc(std::move(disc.volume));
      const DiscIO::Volume* volume = SetDisc(std::move(disc.volume), disc.auto_disc_change_paths); //gvx64 roll-forward to 5.0-9343 to introduce m3u support

      if (!volume)
        return false;

      if (!EmulatedBS2(config.bWii, volume))
        return false;

      // Try to load the symbol map if there is one, and then scan it for
      // and eventually replace code
      if (LoadMapFromFilename())
        HLE::PatchFunctions();

      return true;
    }

    bool operator()(const BootParameters::Executable& executable) const
    {
      NOTICE_LOG(BOOT, "Booting from executable: %s", executable.path.c_str());

      if (!executable.reader->IsValid())
        return false;

      const DiscIO::Volume* volume = nullptr;
      // VolumeDirectory only works with DOLs.
      if (StringEndsWith(executable.path, ".dol"))
      {
        if (!config.m_strDVDRoot.empty())
        {
          NOTICE_LOG(BOOT, "Setting DVDRoot %s", config.m_strDVDRoot.c_str());
          volume = SetDisc(DiscIO::CreateVolumeFromDirectory(
              config.m_strDVDRoot, config.bWii, config.m_strApploader, executable.path));
        }
        else if (!config.m_strDefaultISO.empty())
        {
          NOTICE_LOG(BOOT, "Loading default ISO %s", config.m_strDefaultISO.c_str());
          volume = SetDisc(DiscIO::CreateVolumeFromFilename(config.m_strDefaultISO));
        }
      }
      else
      {
        volume = SetDefaultDisc();
      }

      if (!executable.reader->LoadIntoMemory())
      {
        PanicAlertT("Failed to load the executable to memory.");
        return false;
      }

      // Poor man's bootup
      if (config.bWii)
      {
        HID4.SBE = 1;
        SetupMSR();
        SetupBAT(config.bWii);
        // Because there is no TMD to get the requested system (IOS) version from,
        // we default to IOS58, which is the version used by the Homebrew Channel.
        SetupWiiMemory(volume, 0x000000010000003a);
      }
      else
      {
        EmulatedBS2_GC(volume, true);
      }

      Load_FST(config.bWii, volume);
      PC = executable.reader->GetEntryPoint();

      if (executable.reader->LoadSymbols() || LoadMapFromFilename())
      {
        UpdateDebugger_MapLoaded();
        HLE::PatchFunctions();
      }
      return true;
    }

    bool operator()(const BootParameters::NAND& nand) const
    {
      NOTICE_LOG(BOOT, "Booting from NAND: %s", nand.content_path.c_str());
      SetDefaultDisc();
      return Boot_WiiWAD(nand.content_path);
    }

    bool operator()(const BootParameters::IPL& ipl) const
    {
      NOTICE_LOG(BOOT, "Booting GC IPL: %s", ipl.path.c_str());
      if (!File::Exists(ipl.path))
      {
        if (ipl.disc)
          PanicAlertT("Cannot start the game, because the GC IPL could not be found.");
        else
          PanicAlertT("Cannot find the GC IPL.");
        return false;
      }

      if (!Load_BS2(ipl.path))
        return false;

      if (ipl.disc)
      {
        NOTICE_LOG(BOOT, "Inserting disc: %s", ipl.disc->path.c_str());
//gvx64        SetDisc(DiscIO::CreateVolumeFromFilename(ipl.disc->path));
        SetDisc(DiscIO::CreateVolumeFromFilename(ipl.disc->path), ipl.disc->auto_disc_change_paths); //gvx64 roll-forward to 5.0-9343 to introduce m3u support
      }

      if (LoadMapFromFilename())
        HLE::PatchFunctions();

      return true;
    }

    bool operator()(const BootParameters::DFF& dff) const
    {
      NOTICE_LOG(BOOT, "Booting DFF: %s", dff.dff_path.c_str());
      return FifoPlayer::GetInstance().Open(dff.dff_path);
    }

  private:
    const SConfig& config;
  };

  if (!std::visit(BootTitle(), boot->parameters))
    return false;

  PatchEngine::LoadPatches();
  HLE::PatchFixedFunctions();
  return true;
}

BootExecutableReader::BootExecutableReader(const std::string& file_name)
{
  m_bytes.resize(File::GetSize(file_name));
  File::IOFile file{file_name, "rb"};
  file.ReadBytes(m_bytes.data(), m_bytes.size());
}

BootExecutableReader::BootExecutableReader(const std::vector<u8>& bytes) : m_bytes(bytes)
{
}

BootExecutableReader::~BootExecutableReader() = default;
