// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/ConfigManager.h"

#include <cinttypes>
#include <climits>
#include <memory>
#include <optional>
#include <sstream>
#include <variant>

#include "AudioCommon/AudioCommon.h"

#include "Common/CDUtils.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Common/SysConf.h"

#include "Core/Analytics.h"
#include "Core/Boot/Boot.h"
#include "Core/Core.h"
#include "Core/FifoPlayer/FifoDataFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/SI/SI.h"
#include "Core/IOS/ES/Formats.h"
#include "Core/IOS/USB/Bluetooth/BTBase.h"
#include "Core/PatchEngine.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/TitleDatabase.h"
#include "VideoCommon/HiresTextures.h"

#include "DiscIO/Enums.h"
#include "DiscIO/NANDContentLoader.h"
#include "DiscIO/Volume.h"

SConfig* SConfig::m_Instance;

SConfig::SConfig()
{
  LoadDefaults();
  // Make sure we have log manager
  LoadSettings();
  LoadSettingsFromSysconf();
}

void SConfig::Init()
{
  m_Instance = new SConfig;
}

void SConfig::Shutdown()
{
  delete m_Instance;
  m_Instance = nullptr;
}

SConfig::~SConfig()
{
  SaveSettings();
  SaveSettingsToSysconf();
}

void SConfig::SaveSettings()
{
  NOTICE_LOG(BOOT, "Saving settings to %s", File::GetUserPath(F_DOLPHINCONFIG_IDX).c_str());
  IniFile ini;
  ini.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));  // load first to not kill unknown stuff

  SaveGeneralSettings(ini);
  SaveInterfaceSettings(ini);
  SaveDisplaySettings(ini);
  SaveGameListSettings(ini);
  SaveCoreSettings(ini);
  SaveMovieSettings(ini);
  SaveDSPSettings(ini);
  SaveInputSettings(ini);
  SaveFifoPlayerSettings(ini);
  SaveAnalyticsSettings(ini);
  SaveNetworkSettings(ini);
  SaveBluetoothPassthroughSettings(ini);
  SaveUSBPassthroughSettings(ini);

  ini.Save(File::GetUserPath(F_DOLPHINCONFIG_IDX));

  Config::Save();
}

namespace
{
void CreateDumpPath(const std::string& path)
{
  if (path.empty())
    return;
  File::SetUserPath(D_DUMP_IDX, path + '/');
  File::CreateFullPath(File::GetUserPath(D_DUMPAUDIO_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPDSP_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPSSL_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPFRAMES_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPTEXTURES_IDX));
}
}  // namespace

void SConfig::SaveGeneralSettings(IniFile& ini)
{
  IniFile::Section* general = ini.GetOrCreateSection("General");

  // General
  general->Set("ShowLag", m_ShowLag);
  general->Set("ShowFrameCount", m_ShowFrameCount);

  // ISO folders
  // Clear removed folders
  int oldPaths;
  int numPaths = (int)m_ISOFolder.size();
  general->Get("ISOPaths", &oldPaths, 0);
  for (int i = numPaths; i < oldPaths; i++)
  {
    ini.DeleteKey("General", StringFromFormat("ISOPath%i", i));
  }

  general->Set("ISOPaths", numPaths);
  for (int i = 0; i < numPaths; i++)
  {
    general->Set(StringFromFormat("ISOPath%i", i), m_ISOFolder[i]);
  }

  general->Set("RecursiveISOPaths", m_RecursiveISOFolder);
  general->Set("NANDRootPath", m_NANDPath);
  general->Set("DumpPath", m_DumpPath);
  CreateDumpPath(m_DumpPath);
  general->Set("WirelessMac", m_WirelessMac);
  general->Set("WiiSDCardPath", m_strWiiSDCardPath);

#ifdef USE_GDBSTUB
#ifndef _WIN32
  general->Set("GDBSocket", gdb_socket);
#endif
  general->Set("GDBPort", iGDBPort);
#endif
}

void SConfig::SaveInterfaceSettings(IniFile& ini)
{
  IniFile::Section* interface = ini.GetOrCreateSection("Interface");

  interface->Set("ConfirmStop", bConfirmStop);
  interface->Set("UsePanicHandlers", bUsePanicHandlers);
  interface->Set("OnScreenDisplayMessages", bOnScreenDisplayMessages);
  interface->Set("HideCursor", bHideCursor);
  interface->Set("MainWindowPosX", iPosX);
  interface->Set("MainWindowPosY", iPosY);
  interface->Set("MainWindowWidth", iWidth);
  interface->Set("MainWindowHeight", iHeight);
  interface->Set("LanguageCode", m_InterfaceLanguage);
  interface->Set("ShowToolbar", m_InterfaceToolbar);
  interface->Set("ShowStatusbar", m_InterfaceStatusbar);
  interface->Set("ShowLogWindow", m_InterfaceLogWindow);
  interface->Set("ShowLogConfigWindow", m_InterfaceLogConfigWindow);
  interface->Set("ExtendedFPSInfo", m_InterfaceExtendedFPSInfo);
  interface->Set("ShowActiveTitle", m_show_active_title);
  interface->Set("ThemeName", theme_name);
  interface->Set("PauseOnFocusLost", m_PauseOnFocusLost);
  interface->Set("DisableTooltips", m_DisableTooltips);
}

void SConfig::SaveDisplaySettings(IniFile& ini)
{
  IniFile::Section* display = ini.GetOrCreateSection("Display");

  display->Set("FullscreenResolution", strFullscreenResolution);
  display->Set("Fullscreen", bFullscreen);
  display->Set("RenderToMain", bRenderToMain);
  display->Set("RenderWindowXPos", iRenderWindowXPos);
  display->Set("RenderWindowYPos", iRenderWindowYPos);
  display->Set("RenderWindowWidth", iRenderWindowWidth);
  display->Set("RenderWindowHeight", iRenderWindowHeight);
  display->Set("RenderWindowAutoSize", bRenderWindowAutoSize);
  display->Set("KeepWindowOnTop", bKeepWindowOnTop);
  display->Set("ProgressiveScan", bProgressive);
  display->Set("PAL60", bPAL60);
  display->Set("DisableScreenSaver", bDisableScreenSaver);
  display->Set("ForceNTSCJ", bForceNTSCJ);
}

void SConfig::SaveGameListSettings(IniFile& ini)
{
  IniFile::Section* gamelist = ini.GetOrCreateSection("GameList");

  gamelist->Set("ListDrives", m_ListDrives);
  gamelist->Set("ListWad", m_ListWad);
  gamelist->Set("ListElfDol", m_ListElfDol);
  gamelist->Set("ListWii", m_ListWii);
  gamelist->Set("ListGC", m_ListGC);
  gamelist->Set("ListJap", m_ListJap);
  gamelist->Set("ListPal", m_ListPal);
  gamelist->Set("ListUsa", m_ListUsa);
  gamelist->Set("ListAustralia", m_ListAustralia);
  gamelist->Set("ListFrance", m_ListFrance);
  gamelist->Set("ListGermany", m_ListGermany);
  gamelist->Set("ListItaly", m_ListItaly);
  gamelist->Set("ListKorea", m_ListKorea);
  gamelist->Set("ListNetherlands", m_ListNetherlands);
  gamelist->Set("ListRussia", m_ListRussia);
  gamelist->Set("ListSpain", m_ListSpain);
  gamelist->Set("ListTaiwan", m_ListTaiwan);
  gamelist->Set("ListWorld", m_ListWorld);
  gamelist->Set("ListUnknown", m_ListUnknown);
  gamelist->Set("ListSort", m_ListSort);
  gamelist->Set("ListSortSecondary", m_ListSort2);

  gamelist->Set("ColumnPlatform", m_showSystemColumn);
  gamelist->Set("ColumnBanner", m_showBannerColumn);
  gamelist->Set("ColumnDescription", m_showDescriptionColumn);
  gamelist->Set("ColumnTitle", m_showTitleColumn);
  gamelist->Set("ColumnNotes", m_showMakerColumn);
  gamelist->Set("ColumnFileName", m_showFileNameColumn);
  gamelist->Set("ColumnID", m_showIDColumn);
  gamelist->Set("ColumnRegion", m_showRegionColumn);
  gamelist->Set("ColumnSize", m_showSizeColumn);
  gamelist->Set("ColumnState", m_showStateColumn);
}

void SConfig::SaveCoreSettings(IniFile& ini)
{
  IniFile::Section* core = ini.GetOrCreateSection("Core");

  core->Set("SkipIPL", bHLE_BS2);
  core->Set("TimingVariance", iTimingVariance);
  core->Set("CPUCore", iCPUCore);
  core->Set("Fastmem", bFastmem);
  core->Set("CPUThread", bCPUThread);
  core->Set("DSPHLE", bDSPHLE);
  core->Set("SyncOnSkipIdle", bSyncGPUOnSkipIdleHack);
  core->Set("SyncGPU", bSyncGPU);
  core->Set("SyncGpuMaxDistance", iSyncGpuMaxDistance);
  core->Set("SyncGpuMinDistance", iSyncGpuMinDistance);
  core->Set("SyncGpuOverclock", fSyncGpuOverclock);
  core->Set("FPRF", bFPRF);
  core->Set("AccurateNaNs", bAccurateNaNs);
  core->Set("DefaultISO", m_strDefaultISO);
  core->Set("DVDRoot", m_strDVDRoot);
  core->Set("Apploader", m_strApploader);
  core->Set("EnableCheats", bEnableCheats);
  core->Set("ChangeDiscsAutomatically", bChangeDiscsAutomatically); //gvx64
  core->Set("SelectedLanguage", SelectedLanguage);
  core->Set("OverrideGCLang", bOverrideGCLanguage);
  core->Set("DPL2Decoder", bDPL2Decoder);
  core->Set("AudioLatency", iLatency);
  core->Set("AudioStretch", m_audio_stretch);
  core->Set("AudioStretchMaxLatency", m_audio_stretch_max_latency);
  core->Set("MemcardAPath", m_strMemoryCardA);
  core->Set("MemcardBPath", m_strMemoryCardB);
  core->Set("AgpCartAPath", m_strGbaCartA);
  core->Set("AgpCartBPath", m_strGbaCartB);
  core->Set("SlotA", m_EXIDevice[0]);
  core->Set("SlotB", m_EXIDevice[1]);
  core->Set("SerialPort1", m_EXIDevice[2]);
  core->Set("BBA_MAC", m_bba_mac);
  for (int i = 0; i < SerialInterface::MAX_SI_CHANNELS; ++i)
  {
    core->Set(StringFromFormat("SIDevice%i", i), m_SIDevice[i]);
    core->Set(StringFromFormat("AdapterRumble%i", i), m_AdapterRumble[i]);
    core->Set(StringFromFormat("SimulateKonga%i", i), m_AdapterKonga[i]);
  }
  core->Set("WiiSDCard", m_WiiSDCard);
  core->Set("WiiKeyboard", m_WiiKeyboard);
  core->Set("WiimoteContinuousScanning", m_WiimoteContinuousScanning);
  core->Set("WiimoteEnableSpeaker", m_WiimoteEnableSpeaker);
  core->Set("RunCompareServer", bRunCompareServer);
  core->Set("RunCompareClient", bRunCompareClient);
  core->Set("EmulationSpeed", m_EmulationSpeed);
  core->Set("FrameSkip", m_FrameSkip);
  core->Set("Overclock", m_OCFactor);
  core->Set("OverclockEnable", m_OCEnable);
  core->Set("GFXBackend", m_strVideoBackend);
  core->Set("GPUDeterminismMode", m_strGPUDeterminismMode);
  core->Set("PerfMapDir", m_perfDir);
  core->Set("EnableCustomRTC", bEnableCustomRTC);
  core->Set("CustomRTCValue", m_customRTCValue);
  core->Set("EnableSignatureChecks", m_enable_signature_checks);
}

void SConfig::SaveMovieSettings(IniFile& ini)
{
  IniFile::Section* movie = ini.GetOrCreateSection("Movie");

  movie->Set("PauseMovie", m_PauseMovie);
  movie->Set("Author", m_strMovieAuthor);
  movie->Set("DumpFrames", m_DumpFrames);
  movie->Set("DumpFramesSilent", m_DumpFramesSilent);
  movie->Set("ShowInputDisplay", m_ShowInputDisplay);
  movie->Set("ShowRTC", m_ShowRTC);
}

void SConfig::SaveDSPSettings(IniFile& ini)
{
  IniFile::Section* dsp = ini.GetOrCreateSection("DSP");

  dsp->Set("EnableJIT", m_DSPEnableJIT);
  dsp->Set("DumpAudio", m_DumpAudio);
  dsp->Set("DumpAudioSilent", m_DumpAudioSilent);
  dsp->Set("DumpUCode", m_DumpUCode);
  dsp->Set("Backend", sBackend);
  dsp->Set("Volume", m_Volume);
  dsp->Set("CaptureLog", m_DSPCaptureLog);
}

void SConfig::SaveInputSettings(IniFile& ini)
{
  IniFile::Section* input = ini.GetOrCreateSection("Input");

  input->Set("BackgroundInput", m_BackgroundInput);
}

void SConfig::SaveFifoPlayerSettings(IniFile& ini)
{
  IniFile::Section* fifoplayer = ini.GetOrCreateSection("FifoPlayer");

  fifoplayer->Set("LoopReplay", bLoopFifoReplay);
}

void SConfig::SaveNetworkSettings(IniFile& ini)
{
  IniFile::Section* network = ini.GetOrCreateSection("Network");

  network->Set("SSLDumpRead", m_SSLDumpRead);
  network->Set("SSLDumpWrite", m_SSLDumpWrite);
  network->Set("SSLVerifyCertificates", m_SSLVerifyCert);
  network->Set("SSLDumpRootCA", m_SSLDumpRootCA);
  network->Set("SSLDumpPeerCert", m_SSLDumpPeerCert);
}

void SConfig::SaveAnalyticsSettings(IniFile& ini)
{
  IniFile::Section* analytics = ini.GetOrCreateSection("Analytics");

  analytics->Set("ID", m_analytics_id);
  analytics->Set("Enabled", m_analytics_enabled);
  analytics->Set("PermissionAsked", m_analytics_permission_asked);
}

void SConfig::SaveBluetoothPassthroughSettings(IniFile& ini)
{
  IniFile::Section* section = ini.GetOrCreateSection("BluetoothPassthrough");

  section->Set("Enabled", m_bt_passthrough_enabled);
  section->Set("VID", m_bt_passthrough_vid);
  section->Set("PID", m_bt_passthrough_pid);
  section->Set("LinkKeys", m_bt_passthrough_link_keys);
}

void SConfig::SaveUSBPassthroughSettings(IniFile& ini)
{
  IniFile::Section* section = ini.GetOrCreateSection("USBPassthrough");

  std::ostringstream oss;
  for (const auto& device : m_usb_passthrough_devices)
    oss << StringFromFormat("%04x:%04x", device.first, device.second) << ',';
  std::string devices_string = oss.str();
  if (!devices_string.empty())
    devices_string.pop_back();

  section->Set("Devices", devices_string);
}

void SConfig::SaveSettingsToSysconf()
{
  SysConf sysconf{Common::FromWhichRoot::FROM_CONFIGURED_ROOT};

  sysconf.SetData<u8>("IPL.SSV", SysConf::Entry::Type::Byte, m_wii_screensaver);
  sysconf.SetData<u8>("IPL.LNG", SysConf::Entry::Type::Byte, m_wii_language);

  sysconf.SetData<u8>("IPL.AR", SysConf::Entry::Type::Byte, m_wii_aspect_ratio);
  sysconf.SetData<u8>("BT.BAR", SysConf::Entry::Type::Byte, m_sensor_bar_position);
  sysconf.SetData<u32>("BT.SENS", SysConf::Entry::Type::Long, m_sensor_bar_sensitivity);
  sysconf.SetData<u8>("BT.SPKV", SysConf::Entry::Type::Byte, m_speaker_volume);
  sysconf.SetData<u8>("BT.MOT", SysConf::Entry::Type::Byte, m_wiimote_motor);
  sysconf.SetData<u8>("IPL.PGS", SysConf::Entry::Type::Byte, bProgressive);
  sysconf.SetData<u8>("IPL.E60", SysConf::Entry::Type::Byte, bPAL60);

  // Disable WiiConnect24's standby mode. If it is enabled, it prevents us from receiving
  // shutdown commands in the State Transition Manager (STM).
  // TODO: remove this if and once Dolphin supports WC24 standby mode.
  SysConf::Entry* idle_entry = sysconf.GetOrAddEntry("IPL.IDL", SysConf::Entry::Type::SmallArray);
  if (idle_entry->bytes.empty())
    idle_entry->bytes = std::vector<u8>(2);
  else
    idle_entry->bytes[0] = 0;
  NOTICE_LOG(COMMON, "Disabling WC24 'standby' (shutdown to idle) to avoid hanging on shutdown");

  IOS::HLE::RestoreBTInfoSection(&sysconf);

  sysconf.Save();
}

void SConfig::LoadSettings()
{
  Config::Load();

  INFO_LOG(BOOT, "Loading Settings from %s", File::GetUserPath(F_DOLPHINCONFIG_IDX).c_str());
  IniFile ini;
  ini.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));

  LoadGeneralSettings(ini);
  LoadInterfaceSettings(ini);
  LoadDisplaySettings(ini);
  LoadGameListSettings(ini);
  LoadCoreSettings(ini);
  LoadMovieSettings(ini);
  LoadDSPSettings(ini);
  LoadInputSettings(ini);
  LoadFifoPlayerSettings(ini);
  LoadNetworkSettings(ini);
  LoadAnalyticsSettings(ini);
  LoadBluetoothPassthroughSettings(ini);
  LoadUSBPassthroughSettings(ini);
}

void SConfig::LoadGeneralSettings(IniFile& ini)
{
  IniFile::Section* general = ini.GetOrCreateSection("General");

  general->Get("ShowLag", &m_ShowLag, false);
  general->Get("ShowFrameCount", &m_ShowFrameCount, false);
#ifdef USE_GDBSTUB
#ifndef _WIN32
  general->Get("GDBSocket", &gdb_socket, "");
#endif
  general->Get("GDBPort", &(iGDBPort), -1);
#endif

  m_ISOFolder.clear();
  int numISOPaths;

  if (general->Get("ISOPaths", &numISOPaths, 0))
  {
    for (int i = 0; i < numISOPaths; i++)
    {
      std::string tmpPath;
      general->Get(StringFromFormat("ISOPath%i", i), &tmpPath, "");
      m_ISOFolder.push_back(std::move(tmpPath));
    }
  }

  general->Get("RecursiveISOPaths", &m_RecursiveISOFolder, false);
  general->Get("NANDRootPath", &m_NANDPath);
  File::SetUserPath(D_WIIROOT_IDX, m_NANDPath);
  general->Get("DumpPath", &m_DumpPath);
  CreateDumpPath(m_DumpPath);
  general->Get("WirelessMac", &m_WirelessMac);
  general->Get("WiiSDCardPath", &m_strWiiSDCardPath, File::GetUserPath(F_WIISDCARD_IDX));
  File::SetUserPath(F_WIISDCARD_IDX, m_strWiiSDCardPath);
}

void SConfig::LoadInterfaceSettings(IniFile& ini)
{
  IniFile::Section* interface = ini.GetOrCreateSection("Interface");

  interface->Get("ConfirmStop", &bConfirmStop, true);
  interface->Get("UsePanicHandlers", &bUsePanicHandlers, true);
  interface->Get("OnScreenDisplayMessages", &bOnScreenDisplayMessages, true);
  interface->Get("HideCursor", &bHideCursor, false);
  interface->Get("MainWindowPosX", &iPosX, INT_MIN);
  interface->Get("MainWindowPosY", &iPosY, INT_MIN);
  interface->Get("MainWindowWidth", &iWidth, -1);
  interface->Get("MainWindowHeight", &iHeight, -1);
  interface->Get("LanguageCode", &m_InterfaceLanguage, "");
  interface->Get("ShowToolbar", &m_InterfaceToolbar, true);
  interface->Get("ShowStatusbar", &m_InterfaceStatusbar, true);
  interface->Get("ShowLogWindow", &m_InterfaceLogWindow, false);
  interface->Get("ShowLogConfigWindow", &m_InterfaceLogConfigWindow, false);
  interface->Get("ExtendedFPSInfo", &m_InterfaceExtendedFPSInfo, false);
  interface->Get("ShowActiveTitle", &m_show_active_title, true);
  interface->Get("ThemeName", &theme_name, DEFAULT_THEME_DIR);
  interface->Get("PauseOnFocusLost", &m_PauseOnFocusLost, false);
  interface->Get("DisableTooltips", &m_DisableTooltips, false);
}

void SConfig::LoadDisplaySettings(IniFile& ini)
{
  IniFile::Section* display = ini.GetOrCreateSection("Display");

  display->Get("Fullscreen", &bFullscreen, false);
  display->Get("FullscreenResolution", &strFullscreenResolution, "Auto");
  display->Get("RenderToMain", &bRenderToMain, false);
  display->Get("RenderWindowXPos", &iRenderWindowXPos, -1);
  display->Get("RenderWindowYPos", &iRenderWindowYPos, -1);
  display->Get("RenderWindowWidth", &iRenderWindowWidth, 640);
  display->Get("RenderWindowHeight", &iRenderWindowHeight, 480);
  display->Get("RenderWindowAutoSize", &bRenderWindowAutoSize, false);
  display->Get("KeepWindowOnTop", &bKeepWindowOnTop, false);
  display->Get("ProgressiveScan", &bProgressive, false);
  display->Get("PAL60", &bPAL60, true);
  display->Get("DisableScreenSaver", &bDisableScreenSaver, true);
  display->Get("ForceNTSCJ", &bForceNTSCJ, false);
}

void SConfig::LoadGameListSettings(IniFile& ini)
{
  IniFile::Section* gamelist = ini.GetOrCreateSection("GameList");

  gamelist->Get("ListDrives", &m_ListDrives, false);
  gamelist->Get("ListWad", &m_ListWad, true);
  gamelist->Get("ListElfDol", &m_ListElfDol, true);
  gamelist->Get("ListWii", &m_ListWii, true);
  gamelist->Get("ListGC", &m_ListGC, true);
  gamelist->Get("ListJap", &m_ListJap, true);
  gamelist->Get("ListPal", &m_ListPal, true);
  gamelist->Get("ListUsa", &m_ListUsa, true);

  gamelist->Get("ListAustralia", &m_ListAustralia, true);
  gamelist->Get("ListFrance", &m_ListFrance, true);
  gamelist->Get("ListGermany", &m_ListGermany, true);
  gamelist->Get("ListItaly", &m_ListItaly, true);
  gamelist->Get("ListKorea", &m_ListKorea, true);
  gamelist->Get("ListNetherlands", &m_ListNetherlands, true);
  gamelist->Get("ListRussia", &m_ListRussia, true);
  gamelist->Get("ListSpain", &m_ListSpain, true);
  gamelist->Get("ListTaiwan", &m_ListTaiwan, true);
  gamelist->Get("ListWorld", &m_ListWorld, true);
  gamelist->Get("ListUnknown", &m_ListUnknown, true);
  gamelist->Get("ListSort", &m_ListSort, 3);
  gamelist->Get("ListSortSecondary", &m_ListSort2, 0);

  // Gamelist columns toggles
  gamelist->Get("ColumnPlatform", &m_showSystemColumn, true);
  gamelist->Get("ColumnDescription", &m_showDescriptionColumn, false);
  gamelist->Get("ColumnBanner", &m_showBannerColumn, true);
  gamelist->Get("ColumnTitle", &m_showTitleColumn, true);
  gamelist->Get("ColumnNotes", &m_showMakerColumn, true);
  gamelist->Get("ColumnFileName", &m_showFileNameColumn, false);
  gamelist->Get("ColumnID", &m_showIDColumn, false);
  gamelist->Get("ColumnRegion", &m_showRegionColumn, true);
  gamelist->Get("ColumnSize", &m_showSizeColumn, true);
  gamelist->Get("ColumnState", &m_showStateColumn, true);
}

void SConfig::LoadCoreSettings(IniFile& ini)
{
  IniFile::Section* core = ini.GetOrCreateSection("Core");

  core->Get("SkipIPL", &bHLE_BS2, true);
#ifdef _M_X86
  core->Get("CPUCore", &iCPUCore, PowerPC::CORE_JIT64);
#elif _M_ARM_64
  core->Get("CPUCore", &iCPUCore, PowerPC::CORE_JITARM64);
#else
  core->Get("CPUCore", &iCPUCore, PowerPC::CORE_INTERPRETER);
#endif
  core->Get("Fastmem", &bFastmem, true);
  core->Get("DSPHLE", &bDSPHLE, true);
  core->Get("TimingVariance", &iTimingVariance, 40);
  core->Get("CPUThread", &bCPUThread, true);
  core->Get("SyncOnSkipIdle", &bSyncGPUOnSkipIdleHack, true);
  core->Get("DefaultISO", &m_strDefaultISO);
  core->Get("DVDRoot", &m_strDVDRoot);
  core->Get("Apploader", &m_strApploader);
  core->Get("EnableCheats", &bEnableCheats, false);
  core->Get("ChangeDiscsAutomatically", &bChangeDiscsAutomatically, false); //gvx64
  core->Get("SelectedLanguage", &SelectedLanguage, 0);
  core->Get("OverrideGCLang", &bOverrideGCLanguage, false);
  core->Get("DPL2Decoder", &bDPL2Decoder, false);
  core->Get("AudioLatency", &iLatency, 20);
  core->Get("AudioStretch", &m_audio_stretch, false);
  core->Get("AudioStretchMaxLatency", &m_audio_stretch_max_latency, 80);
  core->Get("MemcardAPath", &m_strMemoryCardA);
  core->Get("MemcardBPath", &m_strMemoryCardB);
  core->Get("AgpCartAPath", &m_strGbaCartA);
  core->Get("AgpCartBPath", &m_strGbaCartB);
  core->Get("SlotA", (int*)&m_EXIDevice[0], ExpansionInterface::EXIDEVICE_MEMORYCARDFOLDER);
  core->Get("SlotB", (int*)&m_EXIDevice[1], ExpansionInterface::EXIDEVICE_NONE);
  core->Get("SerialPort1", (int*)&m_EXIDevice[2], ExpansionInterface::EXIDEVICE_NONE);
  core->Get("BBA_MAC", &m_bba_mac);
  for (int i = 0; i < SerialInterface::MAX_SI_CHANNELS; ++i)
  {
    core->Get(StringFromFormat("SIDevice%i", i), (u32*)&m_SIDevice[i],
              (i == 0) ? SerialInterface::SIDEVICE_GC_CONTROLLER : SerialInterface::SIDEVICE_NONE);
    core->Get(StringFromFormat("AdapterRumble%i", i), &m_AdapterRumble[i], true);
    core->Get(StringFromFormat("SimulateKonga%i", i), &m_AdapterKonga[i], false);
  }
  core->Get("WiiSDCard", &m_WiiSDCard, false);
  core->Get("WiiKeyboard", &m_WiiKeyboard, false);
  core->Get("WiimoteContinuousScanning", &m_WiimoteContinuousScanning, false);
  core->Get("WiimoteEnableSpeaker", &m_WiimoteEnableSpeaker, false);
  core->Get("RunCompareServer", &bRunCompareServer, false);
  core->Get("RunCompareClient", &bRunCompareClient, false);
  core->Get("MMU", &bMMU, false);
  core->Get("BBDumpPort", &iBBDumpPort, -1);
  core->Get("SyncGPU", &bSyncGPU, false);
  core->Get("SyncGpuMaxDistance", &iSyncGpuMaxDistance, 200000);
  core->Get("SyncGpuMinDistance", &iSyncGpuMinDistance, -200000);
  core->Get("SyncGpuOverclock", &fSyncGpuOverclock, 1.0f);
  core->Get("FastDiscSpeed", &bFastDiscSpeed, false);
  core->Get("DCBZ", &bDCBZOFF, false);
  core->Get("LowDCBZHack", &bLowDCBZHack, false);
  core->Get("FPRF", &bFPRF, false);
  core->Get("AccurateNaNs", &bAccurateNaNs, false);
  core->Get("EmulationSpeed", &m_EmulationSpeed, 1.0f);
  core->Get("Overclock", &m_OCFactor, 1.0f);
  core->Get("OverclockEnable", &m_OCEnable, false);
  core->Get("FrameSkip", &m_FrameSkip, 0);
  core->Get("GFXBackend", &m_strVideoBackend, "");
  core->Get("GPUDeterminismMode", &m_strGPUDeterminismMode, "auto");
  core->Get("PerfMapDir", &m_perfDir, "");
  core->Get("EnableCustomRTC", &bEnableCustomRTC, false);
  // Default to seconds between 1.1.1970 and 1.1.2000
  core->Get("CustomRTCValue", &m_customRTCValue, 946684800);
  core->Get("EnableSignatureChecks", &m_enable_signature_checks, true);
}

void SConfig::LoadMovieSettings(IniFile& ini)
{
  IniFile::Section* movie = ini.GetOrCreateSection("Movie");

  movie->Get("PauseMovie", &m_PauseMovie, false);
  movie->Get("Author", &m_strMovieAuthor, "");
  movie->Get("DumpFrames", &m_DumpFrames, false);
  movie->Get("DumpFramesSilent", &m_DumpFramesSilent, false);
  movie->Get("ShowInputDisplay", &m_ShowInputDisplay, false);
  movie->Get("ShowRTC", &m_ShowRTC, false);
}

void SConfig::LoadDSPSettings(IniFile& ini)
{
  IniFile::Section* dsp = ini.GetOrCreateSection("DSP");

  dsp->Get("EnableJIT", &m_DSPEnableJIT, true);
  dsp->Get("DumpAudio", &m_DumpAudio, false);
  dsp->Get("DumpAudioSilent", &m_DumpAudioSilent, false);
  dsp->Get("DumpUCode", &m_DumpUCode, false);
  dsp->Get("Backend", &sBackend, AudioCommon::GetDefaultSoundBackend());
  dsp->Get("Volume", &m_Volume, 100);
  dsp->Get("CaptureLog", &m_DSPCaptureLog, false);

  m_IsMuted = false;
}

void SConfig::LoadInputSettings(IniFile& ini)
{
  IniFile::Section* input = ini.GetOrCreateSection("Input");

  input->Get("BackgroundInput", &m_BackgroundInput, false);
}

void SConfig::LoadFifoPlayerSettings(IniFile& ini)
{
  IniFile::Section* fifoplayer = ini.GetOrCreateSection("FifoPlayer");

  fifoplayer->Get("LoopReplay", &bLoopFifoReplay, true);
}

void SConfig::LoadNetworkSettings(IniFile& ini)
{
  IniFile::Section* network = ini.GetOrCreateSection("Network");

  network->Get("SSLDumpRead", &m_SSLDumpRead, false);
  network->Get("SSLDumpWrite", &m_SSLDumpWrite, false);
  network->Get("SSLVerifyCertificates", &m_SSLVerifyCert, true);
  network->Get("SSLDumpRootCA", &m_SSLDumpRootCA, false);
  network->Get("SSLDumpPeerCert", &m_SSLDumpPeerCert, false);
}

void SConfig::LoadAnalyticsSettings(IniFile& ini)
{
  IniFile::Section* analytics = ini.GetOrCreateSection("Analytics");

  analytics->Get("ID", &m_analytics_id, "");
  analytics->Get("Enabled", &m_analytics_enabled, false);
  analytics->Get("PermissionAsked", &m_analytics_permission_asked, false);
}

void SConfig::LoadBluetoothPassthroughSettings(IniFile& ini)
{
  IniFile::Section* section = ini.GetOrCreateSection("BluetoothPassthrough");

  section->Get("Enabled", &m_bt_passthrough_enabled, false);
  section->Get("VID", &m_bt_passthrough_vid, -1);
  section->Get("PID", &m_bt_passthrough_pid, -1);
  section->Get("LinkKeys", &m_bt_passthrough_link_keys, "");
}

void SConfig::LoadUSBPassthroughSettings(IniFile& ini)
{
  IniFile::Section* section = ini.GetOrCreateSection("USBPassthrough");
  m_usb_passthrough_devices.clear();
  std::string devices_string;
  section->Get("Devices", &devices_string, "");
  for (const auto& pair : SplitString(devices_string, ','))
  {
    const auto index = pair.find(':');
    if (index == std::string::npos)
      continue;

    const u16 vid = static_cast<u16>(strtol(pair.substr(0, index).c_str(), nullptr, 16));
    const u16 pid = static_cast<u16>(strtol(pair.substr(index + 1).c_str(), nullptr, 16));
    if (vid && pid)
      m_usb_passthrough_devices.emplace(vid, pid);
  }
}

void SConfig::LoadSettingsFromSysconf()
{
  SysConf sysconf{Common::FromWhichRoot::FROM_CONFIGURED_ROOT};

  m_wii_screensaver = sysconf.GetData<u8>("IPL.SSV", m_wii_screensaver);
  m_wii_language = sysconf.GetData<u8>("IPL.LNG", m_wii_language);
  m_wii_aspect_ratio = sysconf.GetData<u8>("IPL.AR", m_wii_aspect_ratio);
  m_sensor_bar_position = sysconf.GetData<u8>("BT.BAR", m_sensor_bar_position);
  m_sensor_bar_sensitivity = sysconf.GetData<u32>("BT.SENS", m_sensor_bar_sensitivity);
  m_speaker_volume = sysconf.GetData<u8>("BT.SPKV", m_speaker_volume);
  m_wiimote_motor = sysconf.GetData<u8>("BT.MOT", m_wiimote_motor) != 0;
  bProgressive = sysconf.GetData<u8>("IPL.PGS", bProgressive) != 0;
  bPAL60 = sysconf.GetData<u8>("IPL.E60", bPAL60) != 0;
}

void SConfig::ResetRunningGameMetadata()
{
  SetRunningGameMetadata("00000000", 0, 0, Core::TitleDatabase::TitleType::Other);
}

void SConfig::SetRunningGameMetadata(const DiscIO::Volume& volume,
                                     const DiscIO::Partition& partition)
{
  SetRunningGameMetadata(volume.GetGameID(partition), volume.GetTitleID(partition).value_or(0),
                         volume.GetRevision(partition).value_or(0),
                         Core::TitleDatabase::TitleType::Other);
}

void SConfig::SetRunningGameMetadata(const IOS::ES::TMDReader& tmd)
{
  const u64 tmd_title_id = tmd.GetTitleId();

  // If we're launching a disc game, we want to read the revision from
  // the disc header instead of the TMD. They can differ.
  // (IOS HLE ES calls us with a TMDReader rather than a volume when launching
  // a disc game, because ES has no reason to be accessing the disc directly.)
  if (!DVDInterface::UpdateRunningGameMetadata(tmd_title_id))
  {
    // If not launching a disc game, just read everything from the TMD.
    SetRunningGameMetadata(tmd.GetGameID(), tmd_title_id, tmd.GetTitleVersion(),
                           Core::TitleDatabase::TitleType::Channel);
  }
}

void SConfig::SetRunningGameMetadata(const std::string& game_id, u64 title_id, u16 revision,
                                     Core::TitleDatabase::TitleType type)
{
  const bool was_changed = m_game_id != game_id || m_title_id != title_id || m_revision != revision;
  m_game_id = game_id;
  m_title_id = title_id;
  m_revision = revision;

  if (game_id.length() == 6)
  {
    m_debugger_game_id = game_id;
  }
  else if (title_id != 0)
  {
    m_debugger_game_id =
        StringFromFormat("%08X_%08X", static_cast<u32>(title_id >> 32), static_cast<u32>(title_id));
  }
  else
  {
    m_debugger_game_id.clear();
  }

  if (!was_changed)
    return;

  if (game_id == "00000000")
  {
    m_title_description.clear();
    return;
  }

  const Core::TitleDatabase title_database;
  m_title_description = title_database.Describe(m_game_id, type);
  NOTICE_LOG(CORE, "Active title: %s", m_title_description.c_str());

  if (Core::IsRunning())
  {
    // TODO: have a callback mechanism for title changes?
    g_symbolDB.Clear();
    CBoot::LoadMapFromFilename();
    HLE::Reload();
    PatchEngine::Reload();
    HiresTexture::Update();
    DolphinAnalytics::Instance()->ReportGameStart();
  }
}

void SConfig::LoadDefaults()
{
  bEnableDebugging = false;
  bAutomaticStart = false;
  bBootToPause = false;

#ifdef USE_GDBSTUB
  iGDBPort = -1;
#ifndef _WIN32
  gdb_socket = "";
#endif
#endif

  iCPUCore = PowerPC::DefaultCPUCore();
  iTimingVariance = 40;
  bCPUThread = false;
  bSyncGPUOnSkipIdleHack = true;
  bRunCompareServer = false;
  bDSPHLE = true;
  bFastmem = true;
  bFPRF = false;
  bAccurateNaNs = false;
  bMMU = false;
  bDCBZOFF = false;
  bLowDCBZHack = false;
  iBBDumpPort = -1;
  bSyncGPU = false;
  bFastDiscSpeed = false;
  m_strWiiSDCardPath = File::GetUserPath(F_WIISDCARD_IDX);
  bEnableMemcardSdWriting = true;
  SelectedLanguage = 0;
  bOverrideGCLanguage = false;
  bWii = false;
  bDPL2Decoder = false;
  iLatency = 20;
  m_audio_stretch = false;
  m_audio_stretch_max_latency = 80;

  iPosX = INT_MIN;
  iPosY = INT_MIN;
  iWidth = -1;
  iHeight = -1;

  m_analytics_id = "";
  m_analytics_enabled = false;
  m_analytics_permission_asked = false;

  bLoopFifoReplay = true;

  bJITOff = false;  // debugger only settings
  bJITLoadStoreOff = false;
  bJITLoadStoreFloatingOff = false;
  bJITLoadStorePairedOff = false;
  bJITFloatingPointOff = false;
  bJITIntegerOff = false;
  bJITPairedOff = false;
  bJITSystemRegistersOff = false;
  bJITBranchOff = false;

  ResetRunningGameMetadata();
}

bool SConfig::IsUSBDeviceWhitelisted(const std::pair<u16, u16> vid_pid) const
{
  return m_usb_passthrough_devices.find(vid_pid) != m_usb_passthrough_devices.end();
}

const char* SConfig::GetDirectoryForRegion(DiscIO::Region region)
{
  switch (region)
  {
  case DiscIO::Region::NTSC_J:
    return JAP_DIR;

  case DiscIO::Region::NTSC_U:
    return USA_DIR;

  case DiscIO::Region::PAL:
    return EUR_DIR;

  case DiscIO::Region::NTSC_K:
    // This function can't return a Korean directory name, because this
    // function is only used for GameCube things (memory cards, IPL), and
    // GameCube has no NTSC-K region. Since NTSC-K doesn't correspond to any
    // GameCube region, let's return an arbitrary pick. Returning nullptr like
    // with unknown regions would be inappropriate, because Dolphin expects
    // to get valid memory card paths even when running an NTSC-K Wii game.
    return JAP_DIR;

  default:
    return nullptr;
  }
}

std::string SConfig::GetBootROMPath(const std::string& region_directory) const
{
  const std::string path =
      File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + region_directory + DIR_SEP GC_IPL;
  if (!File::Exists(path))
    return File::GetSysDirectory() + GC_SYS_DIR + DIR_SEP + region_directory + DIR_SEP GC_IPL;
  return path;
}

struct SetGameMetadata
{
  SetGameMetadata(SConfig* config_, DiscIO::Region* region_) : config(config_), region(region_) {}
  bool operator()(const BootParameters::Disc& disc) const
  {
    config->SetRunningGameMetadata(*disc.volume, disc.volume->GetGamePartition());
    config->bWii = disc.volume->GetVolumeType() == DiscIO::Platform::WII_DISC;
    config->m_disc_booted_from_game_list = true;
    *region = disc.volume->GetRegion();
    return true;
  }

  bool operator()(const BootParameters::Executable& executable) const
  {
    if (!executable.reader->IsValid())
      return false;

    config->bWii = executable.reader->IsWii();

    // TODO: Right now GC homebrew boots in NTSC and Wii homebrew in PAL.
    // This is intentional so that Wii homebrew can boot in both 50Hz and 60Hz,
    // without forcing all GC homebrew to 50Hz.
    // In the future, it probably makes sense to add a Region setting for homebrew somewhere in
    // the emulator config.
    *region = config->bWii ? DiscIO::Region::PAL : DiscIO::Region::NTSC_U;

    // Strip the .elf/.dol file extension and directories before the name
    SplitPath(executable.path, nullptr, &config->m_debugger_game_id, nullptr);
    return true;
  }

  bool operator()(const BootParameters::NAND& nand) const
  {
    const auto& loader = DiscIO::NANDContentManager::Access().GetNANDLoader(nand.content_path);
    if (!loader.IsValid())
      return false;

    const IOS::ES::TMDReader& tmd = loader.GetTMD();

    config->SetRunningGameMetadata(tmd);
    config->bWii = true;
    *region = tmd.GetRegion();
    return true;
  }

  bool operator()(const BootParameters::IPL& ipl) const
  {
    config->bWii = false;
    *region = ipl.region;
    return true;
  }

  bool operator()(const BootParameters::DFF& dff) const
  {
    std::unique_ptr<FifoDataFile> dff_file(FifoDataFile::Load(dff.dff_path, true));
    if (!dff_file)
      return false;

    config->bWii = dff_file->GetIsWii();
    *region = DiscIO::Region::NTSC_U;
    return true;
  }

private:
  SConfig* config;
  DiscIO::Region* region;
};

bool SConfig::SetPathsAndGameMetadata(const BootParameters& boot)
{
  m_is_mios = false;
  m_disc_booted_from_game_list = false;
  DiscIO::Region region;
  if (!std::visit(SetGameMetadata(this, &region), boot.parameters))
    return false;

  // Set up region
  const char* retrieved_region_dir = GetDirectoryForRegion(region);
  m_region = retrieved_region_dir ? region : DiscIO::Region::PAL;
  const std::string set_region_dir = retrieved_region_dir ? retrieved_region_dir : EUR_DIR;
  if (!retrieved_region_dir &&
      !PanicYesNoT("Your GCM/ISO file seems to be invalid (invalid country)."
                   "\nContinue with PAL region?"))
  {
    return false;
  }

  // Set up paths
  CheckMemcardPath(SConfig::GetInstance().m_strMemoryCardA, set_region_dir, true);
  CheckMemcardPath(SConfig::GetInstance().m_strMemoryCardB, set_region_dir, false);
  m_strSRAM = File::GetUserPath(F_GCSRAM_IDX);
  m_strBootROM = GetBootROMPath(set_region_dir);

  return true;
}

void SConfig::CheckMemcardPath(std::string& memcardPath, const std::string& gameRegion,
                               bool isSlotA)
{
  std::string ext("." + gameRegion + ".raw");
  if (memcardPath.empty())
  {
    // Use default memcard path if there is no user defined name
    std::string defaultFilename = isSlotA ? GC_MEMCARDA : GC_MEMCARDB;
    memcardPath = File::GetUserPath(D_GCUSER_IDX) + defaultFilename + ext;
  }
  else
  {
    std::string filename = memcardPath;
    std::string region = filename.substr(filename.size() - 7, 3);
    bool hasregion = false;
    hasregion |= region.compare(USA_DIR) == 0;
    hasregion |= region.compare(JAP_DIR) == 0;
    hasregion |= region.compare(EUR_DIR) == 0;
    if (!hasregion)
    {
      // filename doesn't have region in the extension
      if (File::Exists(filename))
      {
        // If the old file exists we are polite and ask if we should copy it
        std::string oldFilename = filename;
        filename.replace(filename.size() - 4, 4, ext);
        if (PanicYesNoT("Memory Card filename in Slot %c is incorrect\n"
                        "Region not specified\n\n"
                        "Slot %c path was changed to\n"
                        "%s\n"
                        "Would you like to copy the old file to this new location?\n",
                        isSlotA ? 'A' : 'B', isSlotA ? 'A' : 'B', filename.c_str()))
        {
          if (!File::Copy(oldFilename, filename))
            PanicAlertT("Copy failed");
        }
      }
      memcardPath = filename;  // Always correct the path!
    }
    else if (region.compare(gameRegion) != 0)
    {
      // filename has region, but it's not == gameRegion
      // Just set the correct filename, the EXI Device will create it if it doesn't exist
      memcardPath = filename.replace(filename.size() - ext.size(), ext.size(), ext);
    }
  }
}

DiscIO::Language SConfig::GetCurrentLanguage(bool wii) const
{
  int language_value;
  if (wii)
    language_value = SConfig::GetInstance().m_wii_language;
  else
    language_value = SConfig::GetInstance().SelectedLanguage + 1;
  DiscIO::Language language = static_cast<DiscIO::Language>(language_value);

  // Get rid of invalid values (probably doesn't matter, but might as well do it)
  if (language > DiscIO::Language::LANGUAGE_UNKNOWN ||
      language < DiscIO::Language::LANGUAGE_JAPANESE)
    language = DiscIO::Language::LANGUAGE_UNKNOWN;
  return language;
}

IniFile SConfig::LoadDefaultGameIni() const
{
  return LoadDefaultGameIni(GetGameID(), m_revision);
}

IniFile SConfig::LoadLocalGameIni() const
{
  return LoadLocalGameIni(GetGameID(), m_revision);
}

IniFile SConfig::LoadGameIni() const
{
  return LoadGameIni(GetGameID(), m_revision);
}

IniFile SConfig::LoadDefaultGameIni(const std::string& id, std::optional<u16> revision)
{
  IniFile game_ini;
  for (const std::string& filename : GetGameIniFilenames(id, revision))
    game_ini.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + filename, true);
  return game_ini;
}

IniFile SConfig::LoadLocalGameIni(const std::string& id, std::optional<u16> revision)
{
  IniFile game_ini;
  for (const std::string& filename : GetGameIniFilenames(id, revision))
    game_ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + filename, true);
  return game_ini;
}

IniFile SConfig::LoadGameIni(const std::string& id, std::optional<u16> revision)
{
  IniFile game_ini;
  for (const std::string& filename : GetGameIniFilenames(id, revision))
    game_ini.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + filename, true);
  for (const std::string& filename : GetGameIniFilenames(id, revision))
    game_ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + filename, true);
  return game_ini;
}

// Returns all possible filenames in ascending order of priority
std::vector<std::string> SConfig::GetGameIniFilenames(const std::string& id,
                                                      std::optional<u16> revision)
{
  std::vector<std::string> filenames;

  if (id.empty())
    return filenames;

  // INIs that match the system code (unique for each Virtual Console system)
  filenames.push_back(id.substr(0, 1) + ".ini");

  // INIs that match all regions
  if (id.size() >= 4)
    filenames.push_back(id.substr(0, 3) + ".ini");

  // Regular INIs
  filenames.push_back(id + ".ini");

  // INIs with specific revisions
  if (revision)
    filenames.push_back(id + StringFromFormat("r%d", *revision) + ".ini");

  return filenames;
}
