// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Null/VertexManager.h"

#include "VideoBackends/Null/ShaderCache.h"

#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/VertexLoaderManager.h"

namespace Null
{
class NullNativeVertexFormat : public NativeVertexFormat
{
public:
  NullNativeVertexFormat() {}
  void SetupVertexPointers() override {}
};

std::unique_ptr<NativeVertexFormat>
VertexManager::CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl)
{
  return std::make_unique<NullNativeVertexFormat>();
}

VertexManager::VertexManager() : m_local_v_buffer(MAXVBUFFERSIZE), m_local_i_buffer(MAXIBUFFERSIZE)
{
}

VertexManager::~VertexManager()
{
}

void VertexManager::ResetBuffer(u32 stride)
{
  m_cur_buffer_pointer = m_base_buffer_pointer = m_local_v_buffer.data();
  m_end_buffer_pointer = m_cur_buffer_pointer + m_local_v_buffer.size();
  IndexGenerator::Start(&m_local_i_buffer[0]);
}

//gvx64 void VertexManager::vFlush()
void VertexManager::vFlush(bool use_dst_alpha) //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
{
  VertexShaderCache::s_instance->SetShader(
      use_dst_alpha ? DSTALPHA_DUAL_SOURCE_BLEND : DSTALPHA_NONE, m_current_primitive_type); //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  GeometryShaderCache::s_instance->SetShader(
      use_dst_alpha ? DSTALPHA_DUAL_SOURCE_BLEND : DSTALPHA_NONE, m_current_primitive_type);//gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  PixelShaderCache::s_instance->SetShader(
      use_dst_alpha ? DSTALPHA_DUAL_SOURCE_BLEND : DSTALPHA_NONE, m_current_primitive_type);//gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
//gvx64  VertexShaderCache::s_instance->SetShader(m_current_primitive_type);
//gvx64  GeometryShaderCache::s_instance->SetShader(m_current_primitive_type);
//gvx64  PixelShaderCache::s_instance->SetShader(m_current_primitive_type);
}

}  // namespace
