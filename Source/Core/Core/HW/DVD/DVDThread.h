// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;
namespace DiscIO
{
struct Partition;
}
namespace DVDInterface
{
enum class ReplyType : u32;
}
namespace DiscIO
{
enum class Platform;
class Volume;
}
namespace IOS
{
namespace ES
{
class TMDReader;
class TicketReader;
}
}

namespace DVDThread
{
void Start();
void Stop();
void DoState(PointerWrap& p);

void SetDisc(std::unique_ptr<DiscIO::Volume> disc);
bool HasDisc();

DiscIO::Platform GetDiscType();
u64 PartitionOffsetToRawOffset(u64 offset, const DiscIO::Partition& partition); //gvx64 rollforward to 5.0-12188 - implement .rvz support
IOS::ES::TMDReader GetTMD(const DiscIO::Partition& partition);
IOS::ES::TicketReader GetTicket(const DiscIO::Partition& partition);
bool IsInsertedDiscRunning(); //gvx64 roll-forward to 5.0-9343 to introduce m3u file support
// This function returns true and calls SConfig::SetRunningGameMetadata(Volume&, Partition&)
// if both of the following conditions are true:
// - A disc is inserted
// - The title_id argument doesn't contain a value, or its value matches the disc's title ID
bool UpdateRunningGameMetadata(const DiscIO::Partition& partition,
                               std::optional<u64> title_id = {});

void StartRead(u64 dvd_offset, u32 length, const DiscIO::Partition& partition,
               DVDInterface::ReplyType reply_type, s64 ticks_until_completion);
void StartReadToEmulatedRAM(u32 output_address, u64 dvd_offset, u32 length,
                            const DiscIO::Partition& partition, DVDInterface::ReplyType reply_type,
                            s64 ticks_until_completion);
}
