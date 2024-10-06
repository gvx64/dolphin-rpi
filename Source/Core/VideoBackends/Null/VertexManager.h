// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>

#include "VideoCommon/VertexManagerBase.h"

namespace Null
{
class VertexManager : public VertexManagerBase
{
public:
  VertexManager();
  ~VertexManager();

  std::unique_ptr<NativeVertexFormat>
  CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;

protected:
  void ResetBuffer(u32 stride) override;

private:
//gvx64  void vFlush() override;
  void vFlush(bool use_dst_alpha) override; //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  std::vector<u8> m_local_v_buffer;
  std::vector<u16> m_local_i_buffer;
};
}
