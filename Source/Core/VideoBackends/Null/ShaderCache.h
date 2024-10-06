// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>

#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoCommon.h"

namespace Null
{
template <typename Uid>
class ShaderCache
{
public:
  ShaderCache();
  virtual ~ShaderCache();

  void Clear();
//gvx64  bool SetShader(u32 primitive_type);
  bool SetShader(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type);//gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

protected:
//gvx64  virtual Uid GetUid(u32 primitive_type, APIType api_type) = 0;
  virtual Uid GetUid(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type, APIType api_type) = 0; //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  virtual ShaderCode GenerateCode(APIType api_type, Uid uid) = 0;

private:
  std::map<Uid, std::string> m_shaders;
  const std::string* m_last_entry = nullptr;
  Uid m_last_uid;
};

class VertexShaderCache : public ShaderCache<VertexShaderUid>
{
public:
  static std::unique_ptr<VertexShaderCache> s_instance;

protected:
//gvx64  VertexShaderUid GetUid(u32 primitive_type, APIType api_type) override
  VertexShaderUid GetUid(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type,
                         APIType api_type) override //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  {
    return GetVertexShaderUid();
  }
  ShaderCode GenerateCode(APIType api_type, VertexShaderUid uid) override
  {
    return GenerateVertexShaderCode(api_type, uid.GetUidData());
  }
};

class GeometryShaderCache : public ShaderCache<GeometryShaderUid>
{
public:
  static std::unique_ptr<GeometryShaderCache> s_instance;

protected:
//gvx64  GeometryShaderUid GetUid(u32 primitive_type, APIType api_type) override
  GeometryShaderUid GetUid(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type,
                           APIType api_type) override //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  {
    return GetGeometryShaderUid(primitive_type);
  }
  ShaderCode GenerateCode(APIType api_type, GeometryShaderUid uid) override
  {
    return GenerateGeometryShaderCode(api_type, uid.GetUidData());
  }
};

class PixelShaderCache : public ShaderCache<PixelShaderUid>
{
public:
  static std::unique_ptr<PixelShaderCache> s_instance;

protected:
//gvx64  PixelShaderUid GetUid(u32 primitive_type, APIType api_type) override
  PixelShaderUid GetUid(DSTALPHA_MODE dst_alpha_mode, u32 primitive_type, APIType api_type) override //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  {
//gvx64    return GetPixelShaderUid();
    return GetPixelShaderUid(dst_alpha_mode); //gvx64
  }
  ShaderCode GenerateCode(APIType api_type, PixelShaderUid uid) override
  {
    return GeneratePixelShaderCode(api_type, uid.GetUidData());
  }
};

}  // namespace NULL
