// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/IOS/IOS.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/Boot/DolReader.h"
#include "Core/CommonTitles.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/WII_IPC.h"
#include "Core/IOS/DI/DI.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/DeviceStub.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/FS/FS.h"
#include "Core/IOS/FS/FileIO.h"
#include "Core/IOS/MIOS.h"
#include "Core/IOS/MemoryValues.h"
#include "Core/IOS/Network/IP/Top.h"
#include "Core/IOS/Network/KD/NetKDRequest.h"
#include "Core/IOS/Network/KD/NetKDTime.h"
#include "Core/IOS/Network/NCD/Manage.h"
#include "Core/IOS/Network/SSL.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/IOS/Network/WD/Command.h"
#include "Core/IOS/SDIO/SDIOSlot0.h"
#include "Core/IOS/STM/STM.h"
#include "Core/IOS/USB/Bluetooth/BTEmu.h"
#include "Core/IOS/USB/Bluetooth/BTReal.h"
#include "Core/IOS/USB/OH0/OH0.h"
#include "Core/IOS/USB/OH0/OH0Device.h"
#include "Core/IOS/USB/USB_HID/HIDv4.h"
#include "Core/IOS/USB/USB_KBD.h"
#include "Core/IOS/USB/USB_VEN/VEN.h"
#include "Core/IOS/WFS/WFSI.h"
#include "Core/IOS/WFS/WFSSRV.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/WiiRoot.h"
#include "DiscIO/NANDContentLoader.h"

namespace IOS
{
namespace HLE
{
static std::unique_ptr<EmulationKernel> s_ios;

constexpr u64 ENQUEUE_REQUEST_FLAG = 0x100000000ULL;
constexpr u64 ENQUEUE_ACKNOWLEDGEMENT_FLAG = 0x200000000ULL;
static CoreTiming::EventType* s_event_enqueue;
static CoreTiming::EventType* s_event_sdio_notify;

constexpr u32 ADDR_MEM1_SIZE = 0x3100;
constexpr u32 ADDR_MEM1_SIM_SIZE = 0x3104;
constexpr u32 ADDR_MEM1_END = 0x3108;
constexpr u32 ADDR_MEM1_ARENA_BEGIN = 0x310c;
constexpr u32 ADDR_MEM1_ARENA_END = 0x3110;
constexpr u32 ADDR_PH1 = 0x3114;
constexpr u32 ADDR_MEM2_SIZE = 0x3118;
constexpr u32 ADDR_MEM2_SIM_SIZE = 0x311c;
constexpr u32 ADDR_MEM2_END = 0x3120;
constexpr u32 ADDR_MEM2_ARENA_BEGIN = 0x3124;
constexpr u32 ADDR_MEM2_ARENA_END = 0x3128;
constexpr u32 ADDR_PH2 = 0x312c;
constexpr u32 ADDR_IPC_BUFFER_BEGIN = 0x3130;
constexpr u32 ADDR_IPC_BUFFER_END = 0x3134;
constexpr u32 ADDR_HOLLYWOOD_REVISION = 0x3138;
constexpr u32 ADDR_PH3 = 0x313c;
constexpr u32 ADDR_IOS_VERSION = 0x3140;
constexpr u32 ADDR_IOS_DATE = 0x3144;
constexpr u32 ADDR_UNKNOWN_BEGIN = 0x3148;
constexpr u32 ADDR_UNKNOWN_END = 0x314c;
constexpr u32 ADDR_PH4 = 0x3150;
constexpr u32 ADDR_PH5 = 0x3154;
constexpr u32 ADDR_RAM_VENDOR = 0x3158;
constexpr u32 ADDR_BOOT_FLAG = 0x315c;
constexpr u32 ADDR_APPLOADER_FLAG = 0x315d;
constexpr u32 ADDR_DEVKIT_BOOT_PROGRAM_VERSION = 0x315e;
constexpr u32 ADDR_SYSMENU_SYNC = 0x3160;
constexpr u32 PLACEHOLDER = 0xDEADBEEF;

enum class MemorySetupType
{
  IOSReload,
  Full,
};

static bool SetupMemory(u64 ios_title_id, MemorySetupType setup_type)
{
  auto target_imv = std::find_if(
      GetMemoryValues().begin(), GetMemoryValues().end(),
      [&](const MemoryValues& imv) { return imv.ios_number == (ios_title_id & 0xffff); });

  if (target_imv == GetMemoryValues().end())
  {
    ERROR_LOG(IOS, "Unknown IOS version: %016" PRIx64, ios_title_id);
    return false;
  }

  if (setup_type == MemorySetupType::IOSReload)
  {
    Memory::Write_U32(target_imv->ios_version, ADDR_IOS_VERSION);

    // These values are written by the IOS kernel as part of its boot process (for IOS28 and newer).
    //
    // This works in a slightly different way on a real console: older IOS versions (< IOS28) all
    // have the same range (933E0000 - 93400000), thus they don't write it at boot and just inherit
    // all values. However, the range has changed since IOS28. To make things work properly
    // after a reload, newer IOSes always write the legacy range before loading an IOS kernel;
    // the new IOS either updates the range (>= IOS28) or inherits it (< IOS28).
    //
    // We can skip this convoluted process and just write the correct range directly.
    Memory::Write_U32(target_imv->mem2_physical_size, ADDR_MEM2_SIZE);
    Memory::Write_U32(target_imv->mem2_simulated_size, ADDR_MEM2_SIM_SIZE);
    Memory::Write_U32(target_imv->mem2_end, ADDR_MEM2_END);
    Memory::Write_U32(target_imv->mem2_arena_begin, ADDR_MEM2_ARENA_BEGIN);
    Memory::Write_U32(target_imv->mem2_arena_end, ADDR_MEM2_ARENA_END);
    Memory::Write_U32(target_imv->ipc_buffer_begin, ADDR_IPC_BUFFER_BEGIN);
    Memory::Write_U32(target_imv->ipc_buffer_end, ADDR_IPC_BUFFER_END);
    Memory::Write_U32(target_imv->unknown_begin, ADDR_UNKNOWN_BEGIN);
    Memory::Write_U32(target_imv->unknown_end, ADDR_UNKNOWN_END);

    return true;
  }

  Memory::Write_U32(target_imv->mem1_physical_size, ADDR_MEM1_SIZE);
  Memory::Write_U32(target_imv->mem1_simulated_size, ADDR_MEM1_SIM_SIZE);
  Memory::Write_U32(target_imv->mem1_end, ADDR_MEM1_END);
  Memory::Write_U32(target_imv->mem1_arena_begin, ADDR_MEM1_ARENA_BEGIN);
  Memory::Write_U32(target_imv->mem1_arena_end, ADDR_MEM1_ARENA_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH1);
  Memory::Write_U32(target_imv->mem2_physical_size, ADDR_MEM2_SIZE);
  Memory::Write_U32(target_imv->mem2_simulated_size, ADDR_MEM2_SIM_SIZE);
  Memory::Write_U32(target_imv->mem2_end, ADDR_MEM2_END);
  Memory::Write_U32(target_imv->mem2_arena_begin, ADDR_MEM2_ARENA_BEGIN);
  Memory::Write_U32(target_imv->mem2_arena_end, ADDR_MEM2_ARENA_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH2);
  Memory::Write_U32(target_imv->ipc_buffer_begin, ADDR_IPC_BUFFER_BEGIN);
  Memory::Write_U32(target_imv->ipc_buffer_end, ADDR_IPC_BUFFER_END);
  Memory::Write_U32(target_imv->hollywood_revision, ADDR_HOLLYWOOD_REVISION);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH3);
  Memory::Write_U32(target_imv->ios_version, ADDR_IOS_VERSION);
  Memory::Write_U32(target_imv->ios_date, ADDR_IOS_DATE);
  Memory::Write_U32(target_imv->unknown_begin, ADDR_UNKNOWN_BEGIN);
  Memory::Write_U32(target_imv->unknown_end, ADDR_UNKNOWN_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH4);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH5);
  Memory::Write_U32(target_imv->ram_vendor, ADDR_RAM_VENDOR);
  Memory::Write_U8(0xDE, ADDR_BOOT_FLAG);
  Memory::Write_U8(0xAD, ADDR_APPLOADER_FLAG);
  Memory::Write_U16(0xBEEF, ADDR_DEVKIT_BOOT_PROGRAM_VERSION);
  Memory::Write_U32(target_imv->sysmenu_sync, ADDR_SYSMENU_SYNC);
  return true;
}

void WriteReturnValue(s32 value, u32 address)
{
  Memory::Write_U32(static_cast<u32>(value), address);
}

Kernel::Kernel()
{
  // Until the Wii root and NAND path stuff is entirely managed by IOS and made non-static,
  // using more than one IOS instance at a time is not supported.
  _assert_(GetIOS() == nullptr);
  Core::InitializeWiiRoot(false);
  AddCoreDevices();
}

Kernel::~Kernel()
{
  // Close all devices that were opened
  for (auto& device : m_fdmap)
  {
    if (!device)
      continue;
    device->Close(0);
  }

  {
    std::lock_guard<std::mutex> lock(m_device_map_mutex);
    m_device_map.clear();
  }

  Core::ShutdownWiiRoot();
}

Kernel::Kernel(u64 title_id) : m_title_id(title_id)
{
}

EmulationKernel::EmulationKernel(u64 title_id) : Kernel(title_id)
{
  INFO_LOG(IOS, "Starting IOS %016" PRIx64, title_id);

  if (!SetupMemory(title_id, MemorySetupType::IOSReload))
    WARN_LOG(IOS, "No information about this IOS -- cannot set up memory values");

  Core::InitializeWiiRoot(Core::WantsDeterminism());

  if (title_id == Titles::MIOS)
  {
    MIOS::Load();
    return;
  }

  // IOS re-inits IPC and sends a dummy ack during its boot process.
  EnqueueIPCAcknowledgement(0);

  AddCoreDevices();
  AddStaticDevices();
}

EmulationKernel::~EmulationKernel()
{
  CoreTiming::RemoveAllEvents(s_event_enqueue);
}

// The title ID is a u64 where the first 32 bits are used for the title type.
// For IOS title IDs, the type will always be 00000001 (system), and the lower 32 bits
// are used for the IOS major version -- which is what we want here.
u32 Kernel::GetVersion() const
{
  return static_cast<u32>(m_title_id);
}

std::shared_ptr<Device::FS> Kernel::GetFS()
{
  return std::static_pointer_cast<Device::FS>(m_device_map.at("/dev/fs"));
}

std::shared_ptr<Device::ES> Kernel::GetES()
{
  return std::static_pointer_cast<Device::ES>(m_device_map.at("/dev/es"));
}

// Since we don't have actual processes, we keep track of only the PPC's UID/GID.
// These functions roughly correspond to syscalls 0x2b, 0x2c, 0x2d, 0x2e (though only for the PPC).
void Kernel::SetUidForPPC(u32 uid)
{
  m_ppc_uid = uid;
}

u32 Kernel::GetUidForPPC() const
{
  return m_ppc_uid;
}

void Kernel::SetGidForPPC(u16 gid)
{
  m_ppc_gid = gid;
}

u16 Kernel::GetGidForPPC() const
{
  return m_ppc_gid;
}

// This corresponds to syscall 0x41, which loads a binary from the NAND and bootstraps the PPC.
// Unlike 0x42, IOS will set up some constants in memory before booting the PPC.
bool Kernel::BootstrapPPC(const DiscIO::NANDContentLoader& content_loader)
{
  if (!content_loader.IsValid())
    return false;

  const auto* content = content_loader.GetContentByIndex(content_loader.GetTMD().GetBootIndex());
  if (!content)
    return false;

  const auto dol_loader = std::make_unique<DolReader>(content->m_Data->Get());
  if (!dol_loader->IsValid())
    return false;

  if (!SetupMemory(m_title_id, MemorySetupType::Full))
    return false;

  if (!dol_loader->LoadIntoMemory())
    return false;

  // NAND titles start with address translation off at 0x3400 (via the PPC bootstub)
  // The state of other CPU registers (like the BAT registers) doesn't matter much
  // because the realmode code at 0x3400 initializes everything itself anyway.
  MSR = 0;
  PC = 0x3400;

  return true;
}

// Similar to syscall 0x42 (ios_boot); this is used to change the current active IOS.
// IOS writes the new version to 0x3140 before restarting, but it does *not* poke any
// of the other constants to the memory. Warning: this resets the kernel instance.
bool Kernel::BootIOS(const u64 ios_title_id)
{
  // A real Wii goes through several steps before getting to MIOS.
  //
  // * The System Menu detects a GameCube disc and launches BC (1-100) instead of the game.
  // * BC (similar to boot1) lowers the clock speed to the Flipper's and then launches boot2.
  // * boot2 sees the lowered clock speed and launches MIOS (1-101) instead of the System Menu.
  //
  // Because we currently don't have boot1 and boot2, and BC is only ever used to launch MIOS
  // (indirectly via boot2), we can just launch MIOS when BC is launched.
  if (ios_title_id == Titles::BC)
  {
    NOTICE_LOG(IOS, "BC: Launching MIOS...");
    return BootIOS(Titles::MIOS);
  }

  // Shut down the active IOS first before switching to the new one.
  s_ios.reset();
  s_ios = std::make_unique<EmulationKernel>(ios_title_id);
  return true;
}

void Kernel::AddDevice(std::unique_ptr<Device::Device> device)
{
  _assert_(device->GetDeviceType() == Device::Device::DeviceType::Static);
  m_device_map[device->GetDeviceName()] = std::move(device);
}

void Kernel::AddCoreDevices()
{
  std::lock_guard<std::mutex> lock(m_device_map_mutex);
  AddDevice(std::make_unique<Device::FS>(*this, "/dev/fs"));
  AddDevice(std::make_unique<Device::ES>(*this, "/dev/es"));
}

void Kernel::AddStaticDevices()
{
  std::lock_guard<std::mutex> lock(m_device_map_mutex);

  if (!SConfig::GetInstance().m_bt_passthrough_enabled)
    AddDevice(std::make_unique<Device::BluetoothEmu>(*this, "/dev/usb/oh1/57e/305"));
  else
    AddDevice(std::make_unique<Device::BluetoothReal>(*this, "/dev/usb/oh1/57e/305"));

  AddDevice(std::make_unique<Device::STMImmediate>(*this, "/dev/stm/immediate"));
  AddDevice(std::make_unique<Device::STMEventHook>(*this, "/dev/stm/eventhook"));
  AddDevice(std::make_unique<Device::DI>(*this, "/dev/di"));
  AddDevice(std::make_unique<Device::NetKDRequest>(*this, "/dev/net/kd/request"));
  AddDevice(std::make_unique<Device::NetKDTime>(*this, "/dev/net/kd/time"));
  AddDevice(std::make_unique<Device::NetNCDManage>(*this, "/dev/net/ncd/manage"));
  AddDevice(std::make_unique<Device::NetWDCommand>(*this, "/dev/net/wd/command"));
  AddDevice(std::make_unique<Device::NetIPTop>(*this, "/dev/net/ip/top"));
  AddDevice(std::make_unique<Device::NetSSL>(*this, "/dev/net/ssl"));
  AddDevice(std::make_unique<Device::USB_KBD>(*this, "/dev/usb/kbd"));
  AddDevice(std::make_unique<Device::SDIOSlot0>(*this, "/dev/sdio/slot0"));
  AddDevice(std::make_unique<Device::Stub>(*this, "/dev/sdio/slot1"));
  AddDevice(std::make_unique<Device::USB_HIDv4>(*this, "/dev/usb/hid"));
  AddDevice(std::make_unique<Device::OH0>(*this, "/dev/usb/oh0"));
  AddDevice(std::make_unique<Device::Stub>(*this, "/dev/usb/oh1"));
  AddDevice(std::make_unique<Device::USB_VEN>(*this, "/dev/usb/ven"));
  AddDevice(std::make_unique<Device::WFSSRV>(*this, "/dev/usb/wfssrv"));
  AddDevice(std::make_unique<Device::WFSI>(*this, "/dev/wfsi"));
}

s32 Kernel::GetFreeDeviceID()
{
  for (u32 i = 0; i < IPC_MAX_FDS; i++)
  {
    if (m_fdmap[i] == nullptr)
    {
      return i;
    }
  }

  return -1;
}

std::shared_ptr<Device::Device> Kernel::GetDeviceByName(const std::string& device_name)
{
  std::lock_guard<std::mutex> lock(m_device_map_mutex);
  const auto iterator = m_device_map.find(device_name);
  return iterator != m_device_map.end() ? iterator->second : nullptr;
}

std::shared_ptr<Device::Device> EmulationKernel::GetDeviceByName(const std::string& device_name)
{
  return Kernel::GetDeviceByName(device_name);
}

// Returns the FD for the newly opened device (on success) or an error code.
s32 Kernel::OpenDevice(OpenRequest& request)
{
  const s32 new_fd = GetFreeDeviceID();
  INFO_LOG(IOS, "Opening %s (mode %d, fd %d)", request.path.c_str(), request.flags, new_fd);
  if (new_fd < 0 || new_fd >= IPC_MAX_FDS)
  {
    ERROR_LOG(IOS, "Couldn't get a free fd, too many open files");
    return FS_EFDEXHAUSTED;
  }
  request.fd = new_fd;

  std::shared_ptr<Device::Device> device;
  if (request.path.find("/dev/usb/oh0/") == 0 && !GetDeviceByName(request.path))
  {
    device = std::make_shared<Device::OH0Device>(*this, request.path);
  }
  else if (request.path.find("/dev/") == 0)
  {
    device = GetDeviceByName(request.path);
  }
  else if (request.path.find('/') == 0)
  {
    device = std::make_shared<Device::FileIO>(*this, request.path);
  }

  if (!device)
  {
    ERROR_LOG(IOS, "Unknown device: %s", request.path.c_str());
    return IPC_ENOENT;
  }

  const ReturnCode code = device->Open(request);
  if (code < IPC_SUCCESS)
    return code;
  m_fdmap[new_fd] = device;
  return new_fd;
}

IPCCommandResult Kernel::HandleIPCCommand(const Request& request)
{
  if (request.command == IPC_CMD_OPEN)
  {
    OpenRequest open_request{request.address};
    const s32 new_fd = OpenDevice(open_request);
    return Device::Device::GetDefaultReply(new_fd);
  }

  const auto device = (request.fd < IPC_MAX_FDS) ? m_fdmap[request.fd] : nullptr;
  if (!device)
    return Device::Device::GetDefaultReply(IPC_EINVAL);

  switch (request.command)
  {
  case IPC_CMD_CLOSE:
    m_fdmap[request.fd].reset();
    return Device::Device::GetDefaultReply(device->Close(request.fd));
  case IPC_CMD_READ:
    return device->Read(ReadWriteRequest{request.address});
  case IPC_CMD_WRITE:
    return device->Write(ReadWriteRequest{request.address});
  case IPC_CMD_SEEK:
    return device->Seek(SeekRequest{request.address});
  case IPC_CMD_IOCTL:
    return device->IOCtl(IOCtlRequest{request.address});
  case IPC_CMD_IOCTLV:
    return device->IOCtlV(IOCtlVRequest{request.address});
  default:
    _assert_msg_(IOS, false, "Unexpected command: %x", request.command);
    return Device::Device::GetDefaultReply(IPC_EINVAL);
  }
}

void Kernel::ExecuteIPCCommand(const u32 address)
{
  Request request{address};
  IPCCommandResult result = HandleIPCCommand(request);

  if (!result.send_reply)
    return;

  // Ensure replies happen in order
  const s64 ticks_until_last_reply = m_last_reply_time - CoreTiming::GetTicks();
  if (ticks_until_last_reply > 0)
    result.reply_delay_ticks += ticks_until_last_reply;
  m_last_reply_time = CoreTiming::GetTicks() + result.reply_delay_ticks;

  EnqueueIPCReply(request, result.return_value, static_cast<int>(result.reply_delay_ticks));
}

// Happens AS SOON AS IPC gets a new pointer!
void Kernel::EnqueueIPCRequest(u32 address)
{
  CoreTiming::ScheduleEvent(1000, s_event_enqueue, address | ENQUEUE_REQUEST_FLAG);
}

// Called to send a reply to an IOS syscall
void Kernel::EnqueueIPCReply(const Request& request, const s32 return_value, int cycles_in_future,
                             CoreTiming::FromThread from)
{
  Memory::Write_U32(static_cast<u32>(return_value), request.address + 4);
  // IOS writes back the command that was responded to in the FD field.
  Memory::Write_U32(request.command, request.address + 8);
  // IOS also overwrites the command type with the reply type.
  Memory::Write_U32(IPC_REPLY, request.address);
  CoreTiming::ScheduleEvent(cycles_in_future, s_event_enqueue, request.address, from);
}

void Kernel::EnqueueIPCAcknowledgement(u32 address, int cycles_in_future)
{
  CoreTiming::ScheduleEvent(cycles_in_future, s_event_enqueue,
                            address | ENQUEUE_ACKNOWLEDGEMENT_FLAG);
}

void Kernel::HandleIPCEvent(u64 userdata)
{
  if (userdata & ENQUEUE_ACKNOWLEDGEMENT_FLAG)
    m_ack_queue.push_back(static_cast<u32>(userdata));
  else if (userdata & ENQUEUE_REQUEST_FLAG)
    m_request_queue.push_back(static_cast<u32>(userdata));
  else
    m_reply_queue.push_back(static_cast<u32>(userdata));

  UpdateIPC();
}

// This is called every IPC_HLE_PERIOD from SystemTimers.cpp
// Takes care of routing ipc <-> ipc HLE
void Kernel::UpdateIPC()
{
  if (!IsReady())
    return;

  if (m_request_queue.size())
  {
    GenerateAck(m_request_queue.front());
    u32 command = m_request_queue.front();
    m_request_queue.pop_front();
    ExecuteIPCCommand(command);
    return;
  }

  if (m_reply_queue.size())
  {
    GenerateReply(m_reply_queue.front());
    DEBUG_LOG(IOS, "<<-- Reply to IPC Request @ 0x%08x", m_reply_queue.front());
    m_reply_queue.pop_front();
    return;
  }

  if (m_ack_queue.size())
  {
    GenerateAck(m_ack_queue.front());
    WARN_LOG(IOS, "<<-- Double-ack to IPC Request @ 0x%08x", m_ack_queue.front());
    m_ack_queue.pop_front();
    return;
  }
}

void Kernel::UpdateDevices()
{
  // Check if a hardware device must be updated
  for (const auto& entry : m_device_map)
  {
    if (entry.second->IsOpened())
    {
      entry.second->Update();
    }
  }
}

void Kernel::UpdateWantDeterminism(const bool new_want_determinism)
{
  WiiSockMan::GetInstance().UpdateWantDeterminism(new_want_determinism);
  for (const auto& device : m_device_map)
    device.second->UpdateWantDeterminism(new_want_determinism);
}

void Kernel::SDIO_EventNotify()
{
  // TODO: Potential race condition: If IsRunning() becomes false after
  // it's checked, an event may be scheduled after CoreTiming shuts down.
  if (SConfig::GetInstance().bWii && Core::IsRunning())
    CoreTiming::ScheduleEvent(0, s_event_sdio_notify, 0, CoreTiming::FromThread::NON_CPU);
}

void Kernel::DoState(PointerWrap& p)
{
  p.Do(m_request_queue);
  p.Do(m_reply_queue);
  p.Do(m_last_reply_time);
  p.Do(m_title_id);
  p.Do(m_ppc_uid);
  p.Do(m_ppc_gid);

  m_iosc.DoState(p);

  if (m_title_id == Titles::MIOS)
    return;

  // We need to make sure all file handles are closed so IOS::HLE::Device::FS::DoState can
  // successfully save or re-create /tmp
  for (auto& descriptor : m_fdmap)
  {
    if (descriptor)
      descriptor->PrepareForState(p.GetMode());
  }

  for (const auto& entry : m_device_map)
    entry.second->DoState(p);

  if (p.GetMode() == PointerWrap::MODE_READ)
  {
    for (u32 i = 0; i < IPC_MAX_FDS; i++)
    {
      u32 exists = 0;
      p.Do(exists);
      if (exists)
      {
        auto device_type = Device::Device::DeviceType::Static;
        p.Do(device_type);
        switch (device_type)
        {
        case Device::Device::DeviceType::Static:
        {
          std::string device_name;
          p.Do(device_name);
          m_fdmap[i] = GetDeviceByName(device_name);
          break;
        }
        case Device::Device::DeviceType::FileIO:
          m_fdmap[i] = std::make_shared<Device::FileIO>(*this, "");
          m_fdmap[i]->DoState(p);
          break;
        case Device::Device::DeviceType::OH0:
          m_fdmap[i] = std::make_shared<Device::OH0Device>(*this, "");
          m_fdmap[i]->DoState(p);
          break;
        }
      }
    }
  }
  else
  {
    for (auto& descriptor : m_fdmap)
    {
      u32 exists = descriptor ? 1 : 0;
      p.Do(exists);
      if (exists)
      {
        auto device_type = descriptor->GetDeviceType();
        p.Do(device_type);
        if (device_type == Device::Device::DeviceType::Static)
        {
          std::string device_name = descriptor->GetDeviceName();
          p.Do(device_name);
        }
        else
        {
          descriptor->DoState(p);
        }
      }
    }
  }
}

IOSC& Kernel::GetIOSC()
{
  return m_iosc;
}

void Init()
{
  s_event_enqueue = CoreTiming::RegisterEvent("IPCEvent", [](u64 userdata, s64) {
    if (s_ios)
      s_ios->HandleIPCEvent(userdata);
  });

  s_event_sdio_notify = CoreTiming::RegisterEvent("SDIO_EventNotify", [](u64, s64) {
    if (!s_ios)
      return;

    auto device = static_cast<Device::SDIOSlot0*>(s_ios->GetDeviceByName("/dev/sdio/slot0").get());
    if (device)
      device->EventNotify();
  });

  // Start with IOS80 to simulate part of the Wii boot process.
  s_ios = std::make_unique<EmulationKernel>(Titles::SYSTEM_MENU_IOS);
  // On a Wii, boot2 launches the system menu IOS, which then launches the system menu
  // (which bootstraps the PPC). Bootstrapping the PPC results in memory values being set up.
  // This means that the constants in the 0x3100 region are always set up by the time
  // a game is launched. This is necessary because booting games from the game list skips
  // a significant part of a Wii's boot process.
  SetupMemory(Titles::SYSTEM_MENU_IOS, MemorySetupType::Full);
}

void Shutdown()
{
  s_ios.reset();
}

EmulationKernel* GetIOS()
{
  return s_ios.get();
}
}  // namespace HLE
}  // namespace IOS
