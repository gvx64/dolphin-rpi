// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Null/ShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VideoCommon.h"

namespace Null
{
template <typename Uid>
ShaderCache<Uid>::ShaderCache()
{
  Clear();

  SETSTAT(stats.numPixelShadersCreated, 0);
  SETSTAT(stats.numPixelShadersAlive, 0);
}

template <typename Uid>
ShaderCache<Uid>::~ShaderCache()
{
  Clear();
}

template <typename Uid>
void ShaderCache<Uid>::Clear()
{
  m_shaders.clear();
  m_last_entry = nullptr;
}

template <typename Uid>
//gvx64 bool ShaderCache<Uid>::SetShader(u32 primitive_type)
bool ShaderCache<Uid>::SetShader(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type) //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
{
//gvx64  Uid uid = GetUid(primitive_type, APIType::OpenGL);
  Uid uid = GetUid(dst_alpha_mode, primitive_type, APIType::OpenGL); //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

  // Check if the shader is already set
  if (m_last_entry)
  {
    if (uid == m_last_uid)
    {
      return true;
    }
  }

  m_last_uid = uid;

  // Check if the shader is already in the cache
  auto iter = m_shaders.find(uid);
  if (iter != m_shaders.end())
  {
    const std::string& entry = iter->second;
    m_last_entry = &entry;

    GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
    return true;
  }

  // Need to compile a new shader
  ShaderCode code = GenerateCode(APIType::OpenGL, uid);
  m_shaders.emplace(uid, code.GetBuffer());

  GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
  return true;
}

template class ShaderCache<VertexShaderUid>;
template class ShaderCache<GeometryShaderUid>;
template class ShaderCache<PixelShaderUid>;

std::unique_ptr<VertexShaderCache> VertexShaderCache::s_instance;
std::unique_ptr<GeometryShaderCache> GeometryShaderCache::s_instance;
std::unique_ptr<PixelShaderCache> PixelShaderCache::s_instance;
}
