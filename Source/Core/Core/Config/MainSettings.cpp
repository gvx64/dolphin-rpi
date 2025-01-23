// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Config/MainSettings.h"

#include <sstream>

#include <fmt/format.h>

#include "AudioCommon/AudioCommon.h"
#include "Common/Assert.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/EnumMap.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"
#include "Core/AchievementManager.h"
#include "Core/Config/DefaultLocale.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "Core/HW/HSP/HSP_Device.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/PowerPC/PowerPC.h"
#include "DiscIO/Enums.h"
#include "VideoCommon/VideoBackendBase.h"
#include "Core/Config/Config.h" //gvx64

namespace Config
{
// Main.Core

const ConfigInfo<bool> MAIN_SKIP_IPL{{System::Main, "Core", "SkipIPL"}, true};
//gvx64 const Info<PowerPC::CPUCore> MAIN_CPU_CORE{{System::Main, "Core", "CPUCore"},
const ConfigInfo<PowerPC::CPUCore> MAIN_CPU_CORE{{System::Main, "Core", "CPUCore"},
                                           PowerPC::DefaultCPUCore()};
const ConfigInfo<bool> MAIN_JIT_FOLLOW_BRANCH{{System::Main, "Core", "JITFollowBranch"}, true};
const ConfigInfo<bool> MAIN_FASTMEM{{System::Main, "Core", "Fastmem"}, true};
const ConfigInfo<bool> MAIN_FASTMEM_ARENA{{System::Main, "Core", "FastmemArena"}, true};
const ConfigInfo<bool> MAIN_LARGE_ENTRY_POINTS_MAP{{System::Main, "Core", "LargeEntryPointsMap"}, true};
const ConfigInfo<bool> MAIN_ACCURATE_CPU_CACHE{{System::Main, "Core", "AccurateCPUCache"}, false};
const ConfigInfo<bool> MAIN_DSP_HLE{{System::Main, "Core", "DSPHLE"}, true};
const ConfigInfo<int> MAIN_MAX_FALLBACK{{System::Main, "Core", "MaxFallback"}, 100};
const ConfigInfo<int> MAIN_TIMING_VARIANCE{{System::Main, "Core", "TimingVariance"}, 40};
const ConfigInfo<bool> MAIN_CPU_THREAD{{System::Main, "Core", "CPUThread"}, true};
const ConfigInfo<bool> MAIN_SYNC_ON_SKIP_IDLE{{System::Main, "Core", "SyncOnSkipIdle"}, true};
const ConfigInfo<std::string> MAIN_DEFAULT_ISO{{System::Main, "Core", "DefaultISO"}, ""};
const ConfigInfo<bool> MAIN_ENABLE_CHEATS{{System::Main, "Core", "EnableCheats"}, false};
const ConfigInfo<int> MAIN_GC_LANGUAGE{{System::Main, "Core", "SelectedLanguage"}, 0};
const ConfigInfo<bool> MAIN_OVERRIDE_REGION_SETTINGS{{System::Main, "Core", "OverrideRegionSettings"},
                                               false};
const ConfigInfo<bool> MAIN_DPL2_DECODER{{System::Main, "Core", "DPL2Decoder"}, false};
//gvx64 const ConfigInfo<AudioCommon::DPL2Quality> MAIN_DPL2_QUALITY{{System::Main, "Core", "DPL2Quality"},
//gvx64                                                       AudioCommon::GetDefaultDPL2Quality()};
const ConfigInfo<int> MAIN_AUDIO_LATENCY{{System::Main, "Core", "AudioLatency"}, 20};
const ConfigInfo<bool> MAIN_AUDIO_STRETCH{{System::Main, "Core", "AudioStretch"}, false};
const ConfigInfo<int> MAIN_AUDIO_STRETCH_LATENCY{{System::Main, "Core", "AudioStretchMaxLatency"}, 80};
const ConfigInfo<std::string> MAIN_MEMCARD_A_PATH{{System::Main, "Core", "MemcardAPath"}, ""};
const ConfigInfo<std::string> MAIN_MEMCARD_B_PATH{{System::Main, "Core", "MemcardBPath"}, ""};
//gvx64 const ConfigInfo<std::string>& GetInfoForMemcardPath(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64   _assert_(ExpansionInterface::IsMemcardSlot(slot));
//gvx64  static constexpr Common::EnumMap<const ConfigInfo<std::string>*, ExpansionInterface::MAX_MEMCARD_SLOT>
//gvx64      infos{
//gvx64          &MAIN_MEMCARD_A_PATH,
//gvx64          &MAIN_MEMCARD_B_PATH,
//gvx64      };
//gvx64  return *infos[slot];
//gvx64 }
const ConfigInfo<std::string> MAIN_AGP_CART_A_PATH{{System::Main, "Core", "AgpCartAPath"}, ""};
const ConfigInfo<std::string> MAIN_AGP_CART_B_PATH{{System::Main, "Core", "AgpCartBPath"}, ""};
//gvx64 const ConfigInfo<std::string>& GetInfoForAGPCartPath(ExpansionInterface::Slot slot)
//gv64 {
//gvx64   _assert_(ExpansionInterface::IsMemcardSlot(slot));
//gvx64  static constexpr Common::EnumMap<const ConfigInfo<std::string>*, ExpansionInterface::MAX_MEMCARD_SLOT>
//gvx64      infos{
//gvx64          &MAIN_AGP_CART_A_PATH,
//gvx64          &MAIN_AGP_CART_B_PATH,
//gvx64      };
//gvx64 return *infos[slot];
//gvx64}
const ConfigInfo<std::string> MAIN_GCI_FOLDER_A_PATH{{System::Main, "Core", "GCIFolderAPath"}, ""};
const ConfigInfo<std::string> MAIN_GCI_FOLDER_B_PATH{{System::Main, "Core", "GCIFolderBPath"}, ""};
//gvx64 const ConfigInfo<std::string>& GetInfoForGCIPath(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64  _assert_(ExpansionInterface::IsMemcardSlot(slot));
//gvx64  static constexpr Common::EnumMap<const ConfigInfo<std::string>*, ExpansionInterface::MAX_MEMCARD_SLOT>
//gvx64      infos{
//gvx64          &MAIN_GCI_FOLDER_A_PATH,
//gvx64          &MAIN_GCI_FOLDER_B_PATH,
//gvx64      };
//gvx64  return *infos[slot];
//gvx64 }
const ConfigInfo<std::string> MAIN_GCI_FOLDER_A_PATH_OVERRIDE{
    {System::Main, "Core", "GCIFolderAPathOverride"}, ""};
const ConfigInfo<std::string> MAIN_GCI_FOLDER_B_PATH_OVERRIDE{
    {System::Main, "Core", "GCIFolderBPathOverride"}, ""};
//gvx64 const ConfigInfo<std::string>& GetInfoForGCIPathOverride(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64  _assert_(ExpansionInterface::IsMemcardSlot(slot));
//gvx64  static constexpr Common::EnumMap<const ConfigInfo<std::string>*, ExpansionInterface::MAX_MEMCARD_SLOT>
//gvx64      infos{
//gvx64          &MAIN_GCI_FOLDER_A_PATH_OVERRIDE,
//gvx64          &MAIN_GCI_FOLDER_B_PATH_OVERRIDE,
//gvx64      };
//gvx64  return *infos[slot];
//gvx64 }

const ConfigInfo<int> MAIN_MEMORY_CARD_SIZE{{System::Main, "Core", "MemoryCardSize"}, -1};

//gvx64const ConfigInfo<ExpansionInterface::EXIDeviceType> MAIN_SLOT_A{
//gvx64    {System::Main, "Core", "SlotA"}, ExpansionInterface::EXIDeviceType::MemoryCardFolder};
const ConfigInfo<int> MAIN_SLOT_A{ //gvx64
    {System::Main, "Core", "SlotA"}, ExpansionInterface::EXIDEVICE_MEMORYCARDFOLDER}; //gvx64
//gvx64 const ConfigInfo<ExpansionInterface::EXIDeviceType> MAIN_SLOT_B{{System::Main, "Core", "SlotB"},
 const ConfigInfo<int> MAIN_SLOT_B{{System::Main, "Core", "SlotB"}, //gvx64
                                                                 ExpansionInterface::EXIDEVICE_MEMORYCARDFOLDER}; //gvx64
//gvx64                                                          ExpansionInterface::EXIDeviceType::None};
//gvx64const ConfigInfo<ExpansionInterface::EXIDeviceType> MAIN_SERIAL_PORT_1{
//gvx64    {System::Main, "Core", "SerialPort1"}, ExpansionInterface::EXIDeviceType::None};
const ConfigInfo<int> MAIN_SERIAL_PORT_1{ //gvx64
      {System::Main, "Core", "SerialPort1"}, ExpansionInterface::EXIDEVICE_NONE}; //gvx64

//gvx64 const ConfigInfo<ExpansionInterface::EXIDeviceType>& GetInfoForEXIDevice(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64  static constexpr Common::EnumMap<const ConfigInfo<ExpansionInterface::EXIDeviceType>*,
//gvx64                                   ExpansionInterface::MAX_SLOT>
//gvx64      infos{
//gvx64          &MAIN_SLOT_A,
//gvx64          &MAIN_SLOT_B,
//gvx64          &MAIN_SERIAL_PORT_1,
//gvx64      };
//gvx64  return *infos[slot];
//gvx64 }

const ConfigInfo<std::string> MAIN_BBA_MAC{{System::Main, "Core", "BBA_MAC"}, ""};
const ConfigInfo<std::string> MAIN_BBA_XLINK_IP{{System::Main, "Core", "BBA_XLINK_IP"}, "127.0.0.1"};
const ConfigInfo<bool> MAIN_BBA_XLINK_CHAT_OSD{{System::Main, "Core", "BBA_XLINK_CHAT_OSD"}, true};

// Schthack PSO Server - https://schtserv.com/
const ConfigInfo<std::string> MAIN_BBA_BUILTIN_DNS{{System::Main, "Core", "BBA_BUILTIN_DNS"},
                                             "3.18.217.27"};
const ConfigInfo<std::string> MAIN_BBA_TAPSERVER_DESTINATION{
    {System::Main, "Core", "BBA_TAPSERVER_DESTINATION"}, "/tmp/dolphin-tap"};
const ConfigInfo<std::string> MAIN_MODEM_TAPSERVER_DESTINATION{
    {System::Main, "Core", "MODEM_TAPSERVER_DESTINATION"}, "/tmp/dolphin-modem-tap"};
const ConfigInfo<std::string> MAIN_BBA_BUILTIN_IP{{System::Main, "Core", "BBA_BUILTIN_IP"}, ""};

const ConfigInfo<SerialInterface::SIDevices>& GetInfoForSIDevice(int channel)
{
  static const std::array<const ConfigInfo<SerialInterface::SIDevices>, 4> infos{
      ConfigInfo<SerialInterface::SIDevices>{{System::Main, "Core", "SIDevice0"},
                                       SerialInterface::SIDEVICE_GC_CONTROLLER},
      ConfigInfo<SerialInterface::SIDevices>{{System::Main, "Core", "SIDevice1"},
                                       SerialInterface::SIDEVICE_NONE},
      ConfigInfo<SerialInterface::SIDevices>{{System::Main, "Core", "SIDevice2"},
                                       SerialInterface::SIDEVICE_NONE},
      ConfigInfo<SerialInterface::SIDevices>{{System::Main, "Core", "SIDevice3"},
                                       SerialInterface::SIDEVICE_NONE},
  };
  return infos[channel];
}

const ConfigInfo<bool>& GetInfoForAdapterRumble(int channel)
{
  static const std::array<const ConfigInfo<bool>, 4> infos{
      ConfigInfo<bool>{{System::Main, "Core", "AdapterRumble0"}, true},
      ConfigInfo<bool>{{System::Main, "Core", "AdapterRumble1"}, true},
      ConfigInfo<bool>{{System::Main, "Core", "AdapterRumble2"}, true},
      ConfigInfo<bool>{{System::Main, "Core", "AdapterRumble3"}, true},
  };
  return infos[channel];
}

const ConfigInfo<bool>& GetInfoForSimulateKonga(int channel)
{
  static const std::array<const ConfigInfo<bool>, 4> infos{
      ConfigInfo<bool>{{System::Main, "Core", "SimulateKonga0"}, false},
      ConfigInfo<bool>{{System::Main, "Core", "SimulateKonga1"}, false},
      ConfigInfo<bool>{{System::Main, "Core", "SimulateKonga2"}, false},
      ConfigInfo<bool>{{System::Main, "Core", "SimulateKonga3"}, false},
  };
  return infos[channel];
}

const ConfigInfo<bool> MAIN_WII_SD_CARD{{System::Main, "Core", "WiiSDCard"}, true};
const ConfigInfo<bool> MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC{
    {System::Main, "Core", "WiiSDCardEnableFolderSync"}, false};
const ConfigInfo<u64> MAIN_WII_SD_CARD_FILESIZE{{System::Main, "Core", "WiiSDCardFilesize"}, 0};
const ConfigInfo<bool> MAIN_WII_KEYBOARD{{System::Main, "Core", "WiiKeyboard"}, false};
const ConfigInfo<bool> MAIN_WIIMOTE_CONTINUOUS_SCANNING{
    {System::Main, "Core", "WiimoteContinuousScanning"}, false};
const ConfigInfo<bool> MAIN_WIIMOTE_ENABLE_SPEAKER{{System::Main, "Core", "WiimoteEnableSpeaker"}, false};
const ConfigInfo<bool> MAIN_CONNECT_WIIMOTES_FOR_CONTROLLER_INTERFACE{
    {System::Main, "Core", "WiimoteControllerInterface"}, false};
const ConfigInfo<bool> MAIN_MMU{{System::Main, "Core", "MMU"}, false};
const ConfigInfo<bool> MAIN_PAUSE_ON_PANIC{{System::Main, "Core", "PauseOnPanic"}, false};
const ConfigInfo<int> MAIN_BB_DUMP_PORT{{System::Main, "Core", "BBDumpPort"}, -1};
const ConfigInfo<bool> MAIN_SYNC_GPU{{System::Main, "Core", "SyncGPU"}, false};
const ConfigInfo<int> MAIN_SYNC_GPU_MAX_DISTANCE{{System::Main, "Core", "SyncGpuMaxDistance"}, 200000};
const ConfigInfo<int> MAIN_SYNC_GPU_MIN_DISTANCE{{System::Main, "Core", "SyncGpuMinDistance"}, -200000};
const ConfigInfo<float> MAIN_SYNC_GPU_OVERCLOCK{{System::Main, "Core", "SyncGpuOverclock"}, 1.0f};
const ConfigInfo<bool> MAIN_FAST_DISC_SPEED{{System::Main, "Core", "FastDiscSpeed"}, false};
const ConfigInfo<bool> MAIN_LOW_DCBZ_HACK{{System::Main, "Core", "LowDCBZHack"}, false};
const ConfigInfo<bool> MAIN_FLOAT_EXCEPTIONS{{System::Main, "Core", "FloatExceptions"}, false};
const ConfigInfo<bool> MAIN_DIVIDE_BY_ZERO_EXCEPTIONS{{System::Main, "Core", "DivByZeroExceptions"},
                                                false};
const ConfigInfo<bool> MAIN_FPRF{{System::Main, "Core", "FPRF"}, false};
const ConfigInfo<bool> MAIN_ACCURATE_NANS{{System::Main, "Core", "AccurateNaNs"}, false};
const ConfigInfo<bool> MAIN_DISABLE_ICACHE{{System::Main, "Core", "DisableICache"}, false};
const ConfigInfo<float> MAIN_EMULATION_SPEED{{System::Main, "Core", "EmulationSpeed"}, 1.0f};
const ConfigInfo<float> MAIN_OVERCLOCK{{System::Main, "Core", "Overclock"}, 1.0f};
const ConfigInfo<bool> MAIN_OVERCLOCK_ENABLE{{System::Main, "Core", "OverclockEnable"}, false};
const ConfigInfo<bool> MAIN_RAM_OVERRIDE_ENABLE{{System::Main, "Core", "RAMOverrideEnable"}, false};
//gvx64 const ConfigInfo<u32> MAIN_MEM1_SIZE{{System::Main, "Core", "MEM1Size"}, Memory::MEM1_SIZE_RETAIL};
//gvx64 const ConfigInfo<u32> MAIN_MEM2_SIZE{{System::Main, "Core", "MEM2Size"}, Memory::MEM2_SIZE_RETAIL};
//gvx64 const ConfigInfo<std::string> MAIN_GFX_BACKEND{{System::Main, "Core", "GFXBackend"},
//gvx64                                         VideoBackendBase::GetDefaultBackendName()};
const ConfigInfo<HSP::HSPDeviceType> MAIN_HSP_DEVICE{{System::Main, "Core", "HSPDevice"},
                                               HSP::HSPDeviceType::None};
const ConfigInfo<u32> MAIN_ARAM_EXPANSION_SIZE{{System::Main, "Core", "ARAMExpansionSize"}, 0x400000};

const ConfigInfo<std::string> MAIN_GPU_DETERMINISM_MODE{{System::Main, "Core", "GPUDeterminismMode"},
                                                  "auto"};
const ConfigInfo<s32> MAIN_OVERRIDE_BOOT_IOS{{System::Main, "Core", "OverrideBootIOS"}, -1};

GPUDeterminismMode GetGPUDeterminismMode()
{
  auto mode = Config::Get(Config::MAIN_GPU_DETERMINISM_MODE);
  if (mode == "auto")
    return GPUDeterminismMode::Auto;
  if (mode == "none")
    return GPUDeterminismMode::Disabled;
  if (mode == "fake-completion")
    return GPUDeterminismMode::FakeCompletion;

//gvx64  NOTICE_LOG_FMT(CORE, "Unknown GPU determinism mode {}", mode);
  NOTICE_LOG(CORE, "Unknown GPU determinism mode {}", mode); //gvx64
  return GPUDeterminismMode::Auto;
}

const ConfigInfo<std::string> MAIN_PERF_MAP_DIR{{System::Main, "Core", "PerfMapDir"}, ""};
const ConfigInfo<bool> MAIN_CUSTOM_RTC_ENABLE{{System::Main, "Core", "EnableCustomRTC"}, false};
// Measured in seconds since the unix epoch (1.1.1970).  Default is 1.1.2000; there are 7 leap years
// between those dates.
const ConfigInfo<u32> MAIN_CUSTOM_RTC_VALUE{{System::Main, "Core", "CustomRTCValue"},
                                      (30 * 365 + 7) * 24 * 60 * 60};
//gvx64 const ConfigInfo<DiscIO::Region> MAIN_FALLBACK_REGION{{System::Main, "Core", "FallbackRegion"},
//gvx64                                                GetDefaultRegion()};
const ConfigInfo<bool> MAIN_AUTO_DISC_CHANGE{{System::Main, "Core", "AutoDiscChange"}, false};
const ConfigInfo<bool> MAIN_ALLOW_SD_WRITES{{System::Main, "Core", "WiiSDCardAllowWrites"}, true};
const ConfigInfo<bool> MAIN_ENABLE_SAVESTATES{{System::Main, "Core", "EnableSaveStates"}, false};
const ConfigInfo<bool> MAIN_REAL_WII_REMOTE_REPEAT_REPORTS{
    {System::Main, "Core", "RealWiiRemoteRepeatReports"}, true};
const ConfigInfo<bool> MAIN_WII_WIILINK_ENABLE{{System::Main, "Core", "EnableWiiLink"}, false};

// Empty means use the Dolphin default URL
const ConfigInfo<std::string> MAIN_WII_NUS_SHOP_URL{{System::Main, "Core", "WiiNusShopUrl"}, ""};

// Main.Display

const ConfigInfo<std::string> MAIN_FULLSCREEN_DISPLAY_RES{
    {System::Main, "Display", "FullscreenDisplayRes"}, "Auto"};
const ConfigInfo<bool> MAIN_FULLSCREEN{{System::Main, "Display", "Fullscreen"}, false};
const ConfigInfo<bool> MAIN_RENDER_TO_MAIN{{System::Main, "Display", "RenderToMain"}, false};
const ConfigInfo<int> MAIN_RENDER_WINDOW_XPOS{{System::Main, "Display", "RenderWindowXPos"}, -1};
const ConfigInfo<int> MAIN_RENDER_WINDOW_YPOS{{System::Main, "Display", "RenderWindowYPos"}, -1};
const ConfigInfo<int> MAIN_RENDER_WINDOW_WIDTH{{System::Main, "Display", "RenderWindowWidth"}, 640};
const ConfigInfo<int> MAIN_RENDER_WINDOW_HEIGHT{{System::Main, "Display", "RenderWindowHeight"}, 480};
const ConfigInfo<bool> MAIN_RENDER_WINDOW_AUTOSIZE{{System::Main, "Display", "RenderWindowAutoSize"},
                                             false};
const ConfigInfo<bool> MAIN_KEEP_WINDOW_ON_TOP{{System::Main, "Display", "KeepWindowOnTop"}, false};
const ConfigInfo<bool> MAIN_DISABLE_SCREENSAVER{{System::Main, "Display", "DisableScreenSaver"}, true};

// Main.DSP

const ConfigInfo<bool> MAIN_DSP_THREAD{{System::Main, "DSP", "DSPThread"}, false};
const ConfigInfo<bool> MAIN_DSP_CAPTURE_LOG{{System::Main, "DSP", "CaptureLog"}, false};
const ConfigInfo<bool> MAIN_DSP_JIT{{System::Main, "DSP", "EnableJIT"}, true};
const ConfigInfo<bool> MAIN_DUMP_AUDIO{{System::Main, "DSP", "DumpAudio"}, false};
const ConfigInfo<bool> MAIN_DUMP_AUDIO_SILENT{{System::Main, "DSP", "DumpAudioSilent"}, false};
const ConfigInfo<bool> MAIN_DUMP_UCODE{{System::Main, "DSP", "DumpUCode"}, false};
const ConfigInfo<std::string> MAIN_AUDIO_BACKEND{{System::Main, "DSP", "Backend"},
                                           AudioCommon::GetDefaultSoundBackend()};
const ConfigInfo<int> MAIN_AUDIO_VOLUME{{System::Main, "DSP", "Volume"}, 100};
const ConfigInfo<bool> MAIN_AUDIO_MUTED{{System::Main, "DSP", "Muted"}, false};
#ifdef _WIN32
const ConfigInfo<std::string> MAIN_WASAPI_DEVICE{{System::Main, "DSP", "WASAPIDevice"}, "Default"};
#endif

bool ShouldUseDPL2Decoder()
{
  return Get(MAIN_DPL2_DECODER) && !Get(MAIN_DSP_HLE);
}

// Main.General

const ConfigInfo<std::string> MAIN_DUMP_PATH{{System::Main, "General", "DumpPath"}, ""};
const ConfigInfo<std::string> MAIN_LOAD_PATH{{System::Main, "General", "LoadPath"}, ""};
const ConfigInfo<std::string> MAIN_RESOURCEPACK_PATH{{System::Main, "General", "ResourcePackPath"}, ""};
const ConfigInfo<std::string> MAIN_FS_PATH{{System::Main, "General", "NANDRootPath"}, ""};
const ConfigInfo<std::string> MAIN_WII_SD_CARD_IMAGE_PATH{{System::Main, "General", "WiiSDCardPath"}, ""};
const ConfigInfo<std::string> MAIN_WII_SD_CARD_SYNC_FOLDER_PATH{
    {System::Main, "General", "WiiSDCardSyncFolder"}, ""};
const ConfigInfo<std::string> MAIN_WFS_PATH{{System::Main, "General", "WFSPath"}, ""};
const ConfigInfo<bool> MAIN_SHOW_LAG{{System::Main, "General", "ShowLag"}, false};
const ConfigInfo<bool> MAIN_SHOW_FRAME_COUNT{{System::Main, "General", "ShowFrameCount"}, false};
const ConfigInfo<std::string> MAIN_WIRELESS_MAC{{System::Main, "General", "WirelessMac"}, ""};
const ConfigInfo<std::string> MAIN_GDB_SOCKET{{System::Main, "General", "GDBSocket"}, ""};
const ConfigInfo<int> MAIN_GDB_PORT{{System::Main, "General", "GDBPort"}, -1};
const ConfigInfo<int> MAIN_ISO_PATH_COUNT{{System::Main, "General", "ISOPaths"}, 0};
const ConfigInfo<std::string> MAIN_SKYLANDERS_PATH{{System::Main, "General", "SkylandersCollectionPath"},
                                             ""};

static ConfigInfo<std::string> MakeISOPathConfigInfo(size_t idx)
{
  return Config::ConfigInfo<std::string>{{Config::System::Main, "General", fmt::format("ISOPath{}", idx)},
                                   ""};
}

//gvx64std::vector<std::string> GetIsoPaths()
//gvx64{
//gvx64  size_t count = MathUtil::SaturatingCast<size_t>(Config::Get(Config::MAIN_ISO_PATH_COUNT));
//gvx64  std::vector<std::string> paths;
//gvx64  paths.reserve(count);
//gvx64  for (size_t i = 0; i < count; ++i)
//gvx64  {
//gvx64    std::string iso_path = Config::Get(MakeISOPathConfigInfo(i));
//gvx64    if (!iso_path.empty())
//gvx64      paths.emplace_back(std::move(iso_path));
//gvx64  }
//gvx64  return paths;
//gvx64}

//gvx64 void SetIsoPaths(const std::vector<std::string>& paths)
//gvx64 {
//gvx64  size_t old_size = MathUtil::SaturatingCast<size_t>(Config::Get(Config::MAIN_ISO_PATH_COUNT));
//gvx64  size_t new_size = paths.size();

//gvx64  size_t current_path_idx = 0;
//gvx64  for (const std::string& p : paths)
//gvx64  {
//gvx64    if (p.empty())
//gvx64    {
//gvx64      --new_size;
//gvx64      continue;
//gvx64    }

//gvx64    Config::SetBase(MakeISOPathConfigInfo(current_path_idx), p);
//gvx64    ++current_path_idx;
//gvx64  }

//gvx64  for (size_t i = current_path_idx; i < old_size; ++i)
//gvx64  {
    // TODO: This actually needs a Config::Erase().
//gvx64    Config::SetBase(MakeISOPathConfigInfo(i), "");
//gvx64  }

//gvx64  Config::SetBase(Config::MAIN_ISO_PATH_COUNT, MathUtil::SaturatingCast<int>(new_size));
//gvx64}

// Main.GBA

#ifdef HAS_LIBMGBA
const ConfigInfo<std::string> MAIN_GBA_BIOS_PATH{{System::Main, "GBA", "BIOS"}, ""};
const std::array<ConfigInfo<std::string>, 4> MAIN_GBA_ROM_PATHS{
    ConfigInfo<std::string>{{System::Main, "GBA", "Rom1"}, ""},
    ConfigInfo<std::string>{{System::Main, "GBA", "Rom2"}, ""},
    ConfigInfo<std::string>{{System::Main, "GBA", "Rom3"}, ""},
    ConfigInfo<std::string>{{System::Main, "GBA", "Rom4"}, ""}};
const ConfigInfo<std::string> MAIN_GBA_SAVES_PATH{{System::Main, "GBA", "SavesPath"}, ""};
const ConfigInfo<bool> MAIN_GBA_SAVES_IN_ROM_PATH{{System::Main, "GBA", "SavesInRomPath"}, false};
const ConfigInfo<bool> MAIN_GBA_THREADS{{System::Main, "GBA", "Threads"}, true};
#endif

// Main.Network

const ConfigInfo<bool> MAIN_NETWORK_SSL_DUMP_READ{{System::Main, "Network", "SSLDumpRead"}, false};
const ConfigInfo<bool> MAIN_NETWORK_SSL_DUMP_WRITE{{System::Main, "Network", "SSLDumpWrite"}, false};
const ConfigInfo<bool> MAIN_NETWORK_SSL_VERIFY_CERTIFICATES{
    {System::Main, "Network", "SSLVerifyCertificates"}, true};
const ConfigInfo<bool> MAIN_NETWORK_SSL_DUMP_ROOT_CA{{System::Main, "Network", "SSLDumpRootCA"}, false};
const ConfigInfo<bool> MAIN_NETWORK_SSL_DUMP_PEER_CERT{{System::Main, "Network", "SSLDumpPeerCert"},
                                                 false};
const ConfigInfo<bool> MAIN_NETWORK_DUMP_BBA{{System::Main, "Network", "DumpBBA"}, false};
const ConfigInfo<bool> MAIN_NETWORK_DUMP_AS_PCAP{{System::Main, "Network", "DumpAsPCAP"}, false};
// Default value based on:
//  - [RFC 1122] 4.2.3.5 TCP Connection Failures (at least 3 minutes)
//  - https://dolp.in/pr8759 hwtest (3 minutes and 10 seconds)
const ConfigInfo<int> MAIN_NETWORK_TIMEOUT{{System::Main, "Network", "NetworkTimeout"}, 190};

// Main.Interface

const ConfigInfo<bool> MAIN_USE_HIGH_CONTRAST_TOOLTIPS{
    {System::Main, "Interface", "UseHighContrastTooltips"}, true};
const ConfigInfo<bool> MAIN_USE_PANIC_HANDLERS{{System::Main, "Interface", "UsePanicHandlers"}, true};
const ConfigInfo<bool> MAIN_ABORT_ON_PANIC_ALERT{{System::Main, "Interface", "AbortOnPanicAlert"}, false};
const ConfigInfo<bool> MAIN_OSD_MESSAGES{{System::Main, "Interface", "OnScreenDisplayMessages"}, true};
const ConfigInfo<bool> MAIN_SKIP_NKIT_WARNING{{System::Main, "Interface", "SkipNKitWarning"}, false};
const ConfigInfo<bool> MAIN_CONFIRM_ON_STOP{{System::Main, "Interface", "ConfirmStop"}, true};
const ConfigInfo<ShowCursor> MAIN_SHOW_CURSOR{{System::Main, "Interface", "CursorVisibility"},
                                        ShowCursor::OnMovement};
const ConfigInfo<bool> MAIN_LOCK_CURSOR{{System::Main, "Interface", "LockCursor"}, false};
const ConfigInfo<std::string> MAIN_INTERFACE_LANGUAGE{{System::Main, "Interface", "LanguageCode"}, ""};
const ConfigInfo<bool> MAIN_SHOW_ACTIVE_TITLE{{System::Main, "Interface", "ShowActiveTitle"}, true};
const ConfigInfo<bool> MAIN_USE_BUILT_IN_TITLE_DATABASE{
    {System::Main, "Interface", "UseBuiltinTitleDatabase"}, true};
const ConfigInfo<std::string> MAIN_THEME_NAME{{System::Main, "Interface", "ThemeName"},
                                        DEFAULT_THEME_DIR};
const ConfigInfo<bool> MAIN_PAUSE_ON_FOCUS_LOST{{System::Main, "Interface", "PauseOnFocusLost"}, false};
const ConfigInfo<bool> MAIN_ENABLE_DEBUGGING{{System::Main, "Interface", "DebugModeEnabled"}, false};

// Main.Analytics

const ConfigInfo<std::string> MAIN_ANALYTICS_ID{{System::Main, "Analytics", "ID"}, ""};
const ConfigInfo<bool> MAIN_ANALYTICS_ENABLED{{System::Main, "Analytics", "Enabled"}, false};
const ConfigInfo<bool> MAIN_ANALYTICS_PERMISSION_ASKED{{System::Main, "Analytics", "PermissionAsked"},
                                                 false};

// Main.GameList

const ConfigInfo<bool> MAIN_GAMELIST_LIST_DRIVES{{System::Main, "GameList", "ListDrives"}, false};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_WAD{{System::Main, "GameList", "ListWad"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_ELF_DOL{{System::Main, "GameList", "ListElfDol"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_WII{{System::Main, "GameList", "ListWii"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_GC{{System::Main, "GameList", "ListGC"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_JPN{{System::Main, "GameList", "ListJap"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_PAL{{System::Main, "GameList", "ListPal"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_USA{{System::Main, "GameList", "ListUsa"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_AUSTRALIA{{System::Main, "GameList", "ListAustralia"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_FRANCE{{System::Main, "GameList", "ListFrance"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_GERMANY{{System::Main, "GameList", "ListGermany"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_ITALY{{System::Main, "GameList", "ListItaly"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_KOREA{{System::Main, "GameList", "ListKorea"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_NETHERLANDS{{System::Main, "GameList", "ListNetherlands"},
                                                true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_RUSSIA{{System::Main, "GameList", "ListRussia"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_SPAIN{{System::Main, "GameList", "ListSpain"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_TAIWAN{{System::Main, "GameList", "ListTaiwan"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_WORLD{{System::Main, "GameList", "ListWorld"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_LIST_UNKNOWN{{System::Main, "GameList", "ListUnknown"}, true};
const ConfigInfo<int> MAIN_GAMELIST_LIST_SORT{{System::Main, "GameList", "ListSort"}, 3};
const ConfigInfo<int> MAIN_GAMELIST_LIST_SORT_SECONDARY{{System::Main, "GameList", "ListSortSecondary"},
                                                  0};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_PLATFORM{{System::Main, "GameList", "ColumnPlatform"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_DESCRIPTION{{System::Main, "GameList", "ColumnDescription"},
                                                  false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_BANNER{{System::Main, "GameList", "ColumnBanner"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_TITLE{{System::Main, "GameList", "ColumnTitle"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_MAKER{{System::Main, "GameList", "ColumnNotes"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_FILE_NAME{{System::Main, "GameList", "ColumnFileName"},
                                                false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_FILE_PATH{{System::Main, "GameList", "ColumnFilePath"},
                                                false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_GAME_ID{{System::Main, "GameList", "ColumnID"}, false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_REGION{{System::Main, "GameList", "ColumnRegion"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_FILE_SIZE{{System::Main, "GameList", "ColumnSize"}, true};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_FILE_FORMAT{{System::Main, "GameList", "ColumnFileFormat"},
                                                  false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_BLOCK_SIZE{{System::Main, "GameList", "ColumnBlockSize"},
                                                 false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_COMPRESSION{{System::Main, "GameList", "ColumnCompression"},
                                                  false};
const ConfigInfo<bool> MAIN_GAMELIST_COLUMN_TAGS{{System::Main, "GameList", "ColumnTags"}, false};

// Main.FifoPlayer

const ConfigInfo<bool> MAIN_FIFOPLAYER_LOOP_REPLAY{{System::Main, "FifoPlayer", "LoopReplay"}, true};
const ConfigInfo<bool> MAIN_FIFOPLAYER_EARLY_MEMORY_UPDATES{
    {System::Main, "FifoPlayer", "EarlyMemoryUpdates"}, false};

// Main.AutoUpdate

//gvx64 const ConfigInfo<std::string> MAIN_AUTOUPDATE_UPDATE_TRACK{{System::Main, "AutoUpdate", "UpdateTrack"},
//gvx64                                                     Common::GetScmUpdateTrackStr()};
const ConfigInfo<std::string> MAIN_AUTOUPDATE_HASH_OVERRIDE{{System::Main, "AutoUpdate", "HashOverride"},
                                                      ""};

// Main.Movie

const ConfigInfo<bool> MAIN_MOVIE_PAUSE_MOVIE{{System::Main, "Movie", "PauseMovie"}, false};
const ConfigInfo<std::string> MAIN_MOVIE_MOVIE_AUTHOR{{System::Main, "Movie", "Author"}, ""};
const ConfigInfo<bool> MAIN_MOVIE_DUMP_FRAMES{{System::Main, "Movie", "DumpFrames"}, false};
const ConfigInfo<bool> MAIN_MOVIE_DUMP_FRAMES_SILENT{{System::Main, "Movie", "DumpFramesSilent"}, false};
const ConfigInfo<bool> MAIN_MOVIE_SHOW_INPUT_DISPLAY{{System::Main, "Movie", "ShowInputDisplay"}, false};
const ConfigInfo<bool> MAIN_MOVIE_SHOW_RTC{{System::Main, "Movie", "ShowRTC"}, false};
const ConfigInfo<bool> MAIN_MOVIE_SHOW_RERECORD{{System::Main, "Movie", "ShowRerecord"}, false};

// Main.Input

const ConfigInfo<bool> MAIN_INPUT_BACKGROUND_INPUT{{System::Main, "Input", "BackgroundInput"}, false};

// Main.Debug

const ConfigInfo<bool> MAIN_DEBUG_JIT_OFF{{System::Main, "Debug", "JitOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_OFF{{System::Main, "Debug", "JitLoadStoreOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_LXZ_OFF{{System::Main, "Debug", "JitLoadStorelXzOff"},
                                                   false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_LWZ_OFF{{System::Main, "Debug", "JitLoadStorelwzOff"},
                                                   false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_LBZX_OFF{{System::Main, "Debug", "JitLoadStorelbzxOff"},
                                                    false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_FLOATING_OFF{
    {System::Main, "Debug", "JitLoadStoreFloatingOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_LOAD_STORE_PAIRED_OFF{
    {System::Main, "Debug", "JitLoadStorePairedOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_FLOATING_POINT_OFF{{System::Main, "Debug", "JitFloatingPointOff"},
                                                   false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_INTEGER_OFF{{System::Main, "Debug", "JitIntegerOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_PAIRED_OFF{{System::Main, "Debug", "JitPairedOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_SYSTEM_REGISTERS_OFF{
    {System::Main, "Debug", "JitSystemRegistersOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_BRANCH_OFF{{System::Main, "Debug", "JitBranchOff"}, false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_REGISTER_CACHE_OFF{{System::Main, "Debug", "JitRegisterCacheOff"},
                                                   false};
const ConfigInfo<bool> MAIN_DEBUG_JIT_ENABLE_PROFILING{{System::Main, "Debug", "JitEnableProfiling"},
                                                 false};

// Main.BluetoothPassthrough

const ConfigInfo<bool> MAIN_BLUETOOTH_PASSTHROUGH_ENABLED{
    {System::Main, "BluetoothPassthrough", "Enabled"}, false};
const ConfigInfo<int> MAIN_BLUETOOTH_PASSTHROUGH_VID{{System::Main, "BluetoothPassthrough", "VID"}, -1};
const ConfigInfo<int> MAIN_BLUETOOTH_PASSTHROUGH_PID{{System::Main, "BluetoothPassthrough", "PID"}, -1};
const ConfigInfo<std::string> MAIN_BLUETOOTH_PASSTHROUGH_LINK_KEYS{
    {System::Main, "BluetoothPassthrough", "LinkKeys"}, ""};

// Main.USBPassthrough

const ConfigInfo<std::string> MAIN_USB_PASSTHROUGH_DEVICES{{System::Main, "USBPassthrough", "Devices"},
                                                     ""};

static std::set<std::pair<u16, u16>> LoadUSBWhitelistFromString(const std::string& devices_string)
{
  std::set<std::pair<u16, u16>> devices;
  for (const auto& pair : SplitString(devices_string, ','))
  {
    const auto index = pair.find(':');
    if (index == std::string::npos)
      continue;

    const u16 vid = static_cast<u16>(strtol(pair.substr(0, index).c_str(), nullptr, 16));
    const u16 pid = static_cast<u16>(strtol(pair.substr(index + 1).c_str(), nullptr, 16));
    if (vid && pid)
      devices.emplace(vid, pid);
  }
  return devices;
}

//gvx64 static std::string SaveUSBWhitelistToString(const std::set<std::pair<u16, u16>>& devices)
//gvx64 {
//gvx64  std::ostringstream oss;
//gvx64  for (const auto& device : devices)
//gvx64    oss << fmt::format("{:04x}:{:04x}", device.first, device.second) << ',';
//gvx64  std::string devices_string = oss.str();
//gvx64  if (!devices_string.empty())
//gvx64    devices_string.pop_back();
//gvx64  return devices_string;
//gvx64 }

std::set<std::pair<u16, u16>> GetUSBDeviceWhitelist()
{
  return LoadUSBWhitelistFromString(Config::Get(Config::MAIN_USB_PASSTHROUGH_DEVICES));
}

//gvx64 void SetUSBDeviceWhitelist(const std::set<std::pair<u16, u16>>& devices)
//gvx64 {
//gvx64  Config::SetBase(Config::MAIN_USB_PASSTHROUGH_DEVICES, SaveUSBWhitelistToString(devices));
//gvx64 }

// Main.EmulatedUSBDevices

const ConfigInfo<bool> MAIN_EMULATE_SKYLANDER_PORTAL{
    {System::Main, "EmulatedUSBDevices", "EmulateSkylanderPortal"}, false};

const ConfigInfo<bool> MAIN_EMULATE_INFINITY_BASE{
    {System::Main, "EmulatedUSBDevices", "EmulateInfinityBase"}, false};

// The reason we need this function is because some memory card code
// expects to get a non-NTSC-K region even if we're emulating an NTSC-K Wii.
DiscIO::Region ToGameCubeRegion(DiscIO::Region region)
{
  if (region != DiscIO::Region::NTSC_K)
    return region;

  // GameCube has no NTSC-K region. No choice of replacement value is completely
  // non-arbitrary, but let's go with NTSC-J since Korean GameCubes are NTSC-J.
  return DiscIO::Region::NTSC_J;
}

//gvx64 const char* GetDirectoryForRegion(DiscIO::Region region, RegionDirectoryStyle style)
//gvx64 {
//gvx64  if (region == DiscIO::Region::Unknown)
//gvx64  if (region == DiscIO::Region::UNKNOWN_REGION) //gvx64
//gvx64     region = ToGameCubeRegion(Config::Get(Config::MAIN_FALLBACK_REGION));

//gvx64   switch (region)
//gvx64   {
//gvx64   case DiscIO::Region::NTSC_J:
//gvx64     return JAP_DIR; //gvx64
//gvx64    return style == RegionDirectoryStyle::Legacy ? JAP_DIR : JPN_DIR;

//gvx64   case DiscIO::Region::NTSC_U:
//gvx64     return USA_DIR;

//gvx64   case DiscIO::Region::PAL:
//gvx64     return EUR_DIR;

//gvx64   case DiscIO::Region::NTSC_K:
    // See ToGameCubeRegion
//gvx64     _assert_msg_(BOOT, false, "NTSC-K is not a valid GameCube region");
//gvx64     return JAP_DIR; //gvx64
//gvx64    return style == RegionDirectoryStyle::Legacy ? JAP_DIR : JPN_DIR;

//gvx64   default:
//gvx64     _assert_msg_(BOOT, false, "Default case should not be reached");
//gvx64     return EUR_DIR;
//gvx64   }
//gvx64  }

std::string GetBootROMPath(const std::string& region_directory)
{
  const std::string path =
      File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + region_directory + DIR_SEP GC_IPL;
  if (!File::Exists(path))
    return File::GetSysDirectory() + GC_SYS_DIR + DIR_SEP + region_directory + DIR_SEP GC_IPL;
  return path;
}

//gvx64 std::string GetMemcardPath(ExpansionInterface::Slot slot, std::optional<DiscIO::Region> region,
//gvx64                           u16 size_mb)
//gvx64 {
//gvx64  return GetMemcardPath(Config::Get(GetInfoForMemcardPath(slot)), slot, region, size_mb);
//gvx64 }

//gvx64 std::string GetMemcardPath(std::string configured_filename, ExpansionInterface::Slot slot,
//gvx64                            std::optional<DiscIO::Region> region, u16 size_mb)
//gvx64 {
//gvx64   const std::string blocks_string = size_mb < Memcard::MBIT_SIZE_MEMORY_CARD_2043 ?
//gvx64                                         fmt::format(".{}", Memcard::MbitToFreeBlocks(size_mb)) :
//gvx64                                         "";

//gvx64   if (configured_filename.empty())
//gvx64   {
    // Use default memcard path if there is no user defined one.
//gvx64     const bool is_slot_a = slot == ExpansionInterface::Slot::A;
//gvx64     const std::string region_string = Config::GetDirectoryForRegion(
//gvx64         Config::ToGameCubeRegion(region ? *region : Config::Get(Config::MAIN_FALLBACK_REGION)));
//gvx64     return fmt::format("{}{}.{}{}.raw", File::GetUserPath(D_GCUSER_IDX),
//gvx64                        is_slot_a ? GC_MEMCARDA : GC_MEMCARDB, region_string, blocks_string);
//gvx64   }

  // Custom path is expected to be stored in the form of
  // "/path/to/file.{region_code}.raw"
  // with an arbitrary but supported region code.
  // Try to extract and replace that region code.
  // If there's no region code just insert one before the extension.

//gvx64   std::string dir;
//gvx64   std::string name;
//gvx64   std::string ext;
//gvx64   UnifyPathSeparators(configured_filename);
//gvx64   SplitPath(configured_filename, &dir, &name, &ext);

//gvx64   constexpr std::string_view us_region = "." USA_DIR;
//gvx64   constexpr std::string_view jp_region = "." JAP_DIR;
//gvx64   constexpr std::string_view eu_region = "." EUR_DIR;
//gvx64   std::optional<DiscIO::Region> path_region = std::nullopt;
//gvx64   if (name.ends_with(us_region))
//gvx64   {
//gvx64     name = name.substr(0, name.size() - us_region.size());
//gvx64     path_region = DiscIO::Region::NTSC_U;
//gvx64   }
//gvx64   else if (name.ends_with(jp_region))
//gvx64   {
//gvx64    name = name.substr(0, name.size() - jp_region.size());
//gvx64     path_region = DiscIO::Region::NTSC_J;
//gvx64   }
//gvx64   else if (name.ends_with(eu_region))
//gvx64   {
//gvx64     name = name.substr(0, name.size() - eu_region.size());
//gvx64     path_region = DiscIO::Region::PAL;
//gvx64   }

//gvx64   const DiscIO::Region used_region =
//gvx64       region ? *region : (path_region ? *path_region : Config::Get(Config::MAIN_FALLBACK_REGION));
//gvx64   return fmt::format("{}{}.{}{}{}", dir, name,
//gvx64                      Config::GetDirectoryForRegion(Config::ToGameCubeRegion(used_region)),
//gvx64                      blocks_string, ext);
//gvx64 }

//gvx64 bool IsDefaultMemcardPathConfigured(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64  return Config::Get(GetInfoForMemcardPath(slot)).empty();
//gvx64 }

//gvx64 std::string GetGCIFolderPath(ExpansionInterface::Slot slot, std::optional<DiscIO::Region> region)
//gvx64 {
//gvx64  return GetGCIFolderPath(Config::Get(GetInfoForGCIPath(slot)), slot, region);
//gvx64 }

//gvx64 std::string GetGCIFolderPath(std::string configured_folder, ExpansionInterface::Slot slot,
//gvx64                             std::optional<DiscIO::Region> region)
//gvx64{
//gvx64  if (configured_folder.empty())
//gxv64  {
//gvx64    const auto region_dir = Config::GetDirectoryForRegion(
//gvx64        Config::ToGameCubeRegion(region ? *region : Config::Get(Config::MAIN_FALLBACK_REGION)));
//gvx64    const bool is_slot_a = slot == ExpansionInterface::Slot::A;
//gvx64    return fmt::format("{}{}/Card {}", File::GetUserPath(D_GCUSER_IDX), region_dir,
//gvx64                       is_slot_a ? 'A' : 'B');
//gvx64  }

  // Custom path is expected to be stored in the form of
  // "/path/to/folder/{region_code}"
  // with an arbitrary but supported region code.
  // Try to extract and replace that region code.
  // If there's no region code just insert one at the end.

//gvx64  UnifyPathSeparators(configured_folder);
//gvx64  while (configured_folder.ends_with('/'))
//gvx64    configured_folder.pop_back();

//gvx64  constexpr std::string_view us_region = "/" USA_DIR;
//gvx64  constexpr std::string_view jp_region = "/" JPN_DIR;
//gvx64  constexpr std::string_view eu_region = "/" EUR_DIR;
//gvx64  std::string_view base_path = configured_folder;
//gvx64  std::optional<DiscIO::Region> path_region = std::nullopt;
//gvx64  if (base_path.ends_with(us_region))
//gvx64  {
//gvx64    base_path = base_path.substr(0, base_path.size() - us_region.size());
//gvx64    path_region = DiscIO::Region::NTSC_U;
//gvx64  }
//gvx64  else if (base_path.ends_with(jp_region))
//gvx64  {
//gvx64    base_path = base_path.substr(0, base_path.size() - jp_region.size());
//gvx64    path_region = DiscIO::Region::NTSC_J;
//gvx64  }
//gvx64  else if (base_path.ends_with(eu_region))
//gvx64  {
//gvx64    base_path = base_path.substr(0, base_path.size() - eu_region.size());
//gvx64    path_region = DiscIO::Region::PAL;
//gvx64  }

//gvx64  const DiscIO::Region used_region =
//gvx64      region ? *region : (path_region ? *path_region : Config::Get(Config::MAIN_FALLBACK_REGION));
//gvx64  return fmt::format("{}/{}", base_path,
//gvx64                     Config::GetDirectoryForRegion(Config::ToGameCubeRegion(used_region),
//gvx64                                                   Config::RegionDirectoryStyle::Modern));
//gvx64 }

//gvx64 bool IsDefaultGCIFolderPathConfigured(ExpansionInterface::Slot slot)
//gvx64 {
//gvx64  return Config::Get(GetInfoForGCIPath(slot)).empty();
//gvx64 }

bool AreCheatsEnabled()
{
  return Config::Get(::Config::MAIN_ENABLE_CHEATS) &&
         !AchievementManager::GetInstance().IsHardcoreModeActive();
}

bool IsDebuggingEnabled()
{
  return Config::Get(::Config::MAIN_ENABLE_DEBUGGING) &&
         !AchievementManager::GetInstance().IsHardcoreModeActive();
}

}  // namespace Config
