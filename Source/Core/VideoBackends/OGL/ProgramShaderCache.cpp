// Copyright 2011 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/OGL/ProgramShaderCache.h"

#include <memory>
#include <string>

#include "Common/Align.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/OGL/Render.h"
#include "VideoBackends/OGL/StreamBuffer.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/ImageWrite.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoCommon.h"

namespace OGL
{
static const u32 UBO_LENGTH = 32 * 1024 * 1024;

u32 ProgramShaderCache::s_ubo_buffer_size;
s32 ProgramShaderCache::s_ubo_align;

static std::unique_ptr<StreamBuffer> s_buffer;
static int num_failures = 0;

static LinearDiskCache<SHADERUID, u8> g_program_disk_cache;
static GLuint CurrentProgram = 0;
ProgramShaderCache::PCache ProgramShaderCache::pshaders;
ProgramShaderCache::PCacheEntry* ProgramShaderCache::last_entry;
SHADERUID ProgramShaderCache::last_uid;

static std::string s_glsl_header = "";

static std::string GetGLSLVersionString()
{
  GLSL_VERSION v = g_ogl_config.eSupportedGLSLVersion;
  switch (v)
  {
  case GLSLES_300:
    return "#version 300 es";
  case GLSLES_310:
    return "#version 310 es";
  case GLSLES_320:
    return "#version 320 es";
  case GLSL_130:
    return "#version 130";
  case GLSL_140:
    return "#version 140";
  case GLSL_150:
    return "#version 150";
  case GLSL_330:
    return "#version 330";
  case GLSL_400:
    return "#version 400";
  case GLSL_430:
    return "#version 430";
  default:
    // Shouldn't ever hit this
    return "#version ERROR";
  }
}

void SHADER::SetProgramVariables()
{
  // Bind UBO and texture samplers
  if (!g_ActiveConfig.backend_info.bSupportsBindingLayout)
  {
    // glsl shader must be bind to set samplers if we don't support binding layout
    Bind();

    GLint PSBlock_id = glGetUniformBlockIndex(glprogid, "PSBlock");
    GLint VSBlock_id = glGetUniformBlockIndex(glprogid, "VSBlock");
    GLint GSBlock_id = glGetUniformBlockIndex(glprogid, "GSBlock");

    if (PSBlock_id != -1)
      glUniformBlockBinding(glprogid, PSBlock_id, 1);
    if (VSBlock_id != -1)
      glUniformBlockBinding(glprogid, VSBlock_id, 2);
    if (GSBlock_id != -1)
      glUniformBlockBinding(glprogid, GSBlock_id, 3);

    // Bind Texture Samplers
    for (int a = 0; a <= 9; ++a)
    {
      std::string name = StringFromFormat(a < 8 ? "samp[%d]" : "samp%d", a);

      // Still need to get sampler locations since we aren't binding them statically in the shaders
      int loc = glGetUniformLocation(glprogid, name.c_str());
      if (loc != -1)
        glUniform1i(loc, a);
    }
  }
}

void SHADER::SetProgramBindings(bool is_compute)
{
  if (!is_compute)
  {
    if (g_ActiveConfig.backend_info.bSupportsDualSourceBlend)
    {
      // So we do support extended blending
      // So we need to set a few more things here.
      // Bind our out locations
      glBindFragDataLocationIndexed(glprogid, 0, 0, "ocol0");
      glBindFragDataLocationIndexed(glprogid, 0, 1, "ocol1");
    }
    // Need to set some attribute locations
    glBindAttribLocation(glprogid, SHADER_POSITION_ATTRIB, "rawpos");

    glBindAttribLocation(glprogid, SHADER_POSMTX_ATTRIB, "posmtx");

    glBindAttribLocation(glprogid, SHADER_COLOR0_ATTRIB, "color0");
    glBindAttribLocation(glprogid, SHADER_COLOR1_ATTRIB, "color1");

    glBindAttribLocation(glprogid, SHADER_NORM0_ATTRIB, "rawnorm0");
    glBindAttribLocation(glprogid, SHADER_NORM1_ATTRIB, "rawnorm1");
    glBindAttribLocation(glprogid, SHADER_NORM2_ATTRIB, "rawnorm2");
  }

  for (int i = 0; i < 8; i++)
  {
    std::string attrib_name = StringFromFormat("tex%d", i);
    glBindAttribLocation(glprogid, SHADER_TEXTURE0_ATTRIB + i, attrib_name.c_str());
  }
}

void SHADER::Bind() const
{
  if (CurrentProgram != glprogid)
  {
    INCSTAT(stats.thisFrame.numShaderChanges);
    glUseProgram(glprogid);
    CurrentProgram = glprogid;
  }
}

void ProgramShaderCache::UploadConstants()
{
  if (PixelShaderManager::dirty || VertexShaderManager::dirty || GeometryShaderManager::dirty)
  {
    auto buffer = s_buffer->Map(s_ubo_buffer_size, s_ubo_align);

    memcpy(buffer.first, &PixelShaderManager::constants, sizeof(PixelShaderConstants));

    memcpy(buffer.first + Common::AlignUp(sizeof(PixelShaderConstants), s_ubo_align),
           &VertexShaderManager::constants, sizeof(VertexShaderConstants));

    memcpy(buffer.first + Common::AlignUp(sizeof(PixelShaderConstants), s_ubo_align) +
               Common::AlignUp(sizeof(VertexShaderConstants), s_ubo_align),
           &GeometryShaderManager::constants, sizeof(GeometryShaderConstants));

    s_buffer->Unmap(s_ubo_buffer_size);
    glBindBufferRange(GL_UNIFORM_BUFFER, 1, s_buffer->m_buffer, buffer.second,
                      sizeof(PixelShaderConstants));
    glBindBufferRange(GL_UNIFORM_BUFFER, 2, s_buffer->m_buffer,
                      buffer.second + Common::AlignUp(sizeof(PixelShaderConstants), s_ubo_align),
                      sizeof(VertexShaderConstants));
    glBindBufferRange(GL_UNIFORM_BUFFER, 3, s_buffer->m_buffer,
                      buffer.second + Common::AlignUp(sizeof(PixelShaderConstants), s_ubo_align) +
                          Common::AlignUp(sizeof(VertexShaderConstants), s_ubo_align),
                      sizeof(GeometryShaderConstants));

    PixelShaderManager::dirty = false;
    VertexShaderManager::dirty = false;
    GeometryShaderManager::dirty = false;

    ADDSTAT(stats.thisFrame.bytesUniformStreamed, s_ubo_buffer_size);
  }
}

//gvx64SHADER* ProgramShaderCache::SetShader(u32 primitive_type)
SHADER* ProgramShaderCache::SetShader(DSTALPHA_MODE dstAlphaMode, u32 primitive_type) //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
{
  SHADERUID uid;
//gvx64  GetShaderId(&uid, primitive_type);
 GetShaderId(&uid, dstAlphaMode, primitive_type);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

  // Check if the shader is already set
  if (last_entry)
  {
    if (uid == last_uid)
    {
      GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
      last_entry->shader.Bind();
      return &last_entry->shader;
    }
  }

  last_uid = uid;

  // Check if shader is already in cache
  PCache::iterator iter = pshaders.find(uid);
  if (iter != pshaders.end())
  {
    PCacheEntry* entry = &iter->second;
    last_entry = entry;

    GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
    last_entry->shader.Bind();
    return &last_entry->shader;
  }

  // Make an entry in the table
  PCacheEntry& newentry = pshaders[uid];
  last_entry = &newentry;
  newentry.in_cache = 0;

  ShaderCode vcode = GenerateVertexShaderCode(APIType::OpenGL, uid.vuid.GetUidData());
  ShaderCode pcode = GeneratePixelShaderCode(APIType::OpenGL, uid.puid.GetUidData());
  ShaderCode gcode;
  if (g_ActiveConfig.backend_info.bSupportsGeometryShaders &&
      !uid.guid.GetUidData()->IsPassthrough())
    gcode = GenerateGeometryShaderCode(APIType::OpenGL, uid.guid.GetUidData());

#if defined(_DEBUG) || defined(DEBUGFAST)
  if (g_ActiveConfig.iLog & CONF_SAVESHADERS)
  {
    static int counter = 0;
    std::string filename =
        StringFromFormat("%svs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), counter++);
    SaveData(filename, vcode.GetBuffer());

    filename = StringFromFormat("%sps_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), counter++);
    SaveData(filename, pcode.GetBuffer());

    if (!gcode.GetBuffer().empty())
    {
      filename =
          StringFromFormat("%sgs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), counter++);
      SaveData(filename, gcode.GetBuffer());
    }
  }
#endif

  if (!CompileShader(newentry.shader, vcode.GetBuffer(), pcode.GetBuffer(), gcode.GetBuffer()))
  {
    GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
    return nullptr;
  }

  INCSTAT(stats.numPixelShadersCreated);
  SETSTAT(stats.numPixelShadersAlive, pshaders.size());
  GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);

  last_entry->shader.Bind();
  return &last_entry->shader;
}

bool ProgramShaderCache::CompileShader(SHADER& shader, const std::string& vcode,
                                       const std::string& pcode, const std::string& gcode)
{
  GLuint vsid = CompileSingleShader(GL_VERTEX_SHADER, vcode);
  GLuint psid = CompileSingleShader(GL_FRAGMENT_SHADER, pcode);

  // Optional geometry shader
  GLuint gsid = 0;
  if (!gcode.empty())
    gsid = CompileSingleShader(GL_GEOMETRY_SHADER, gcode);

  if (!vsid || !psid || (!gcode.empty() && !gsid))
  {
    glDeleteShader(vsid);
    glDeleteShader(psid);
    glDeleteShader(gsid);
    return false;
  }

  GLuint pid = shader.glprogid = glCreateProgram();

  glAttachShader(pid, vsid);
  glAttachShader(pid, psid);
  if (gsid)
    glAttachShader(pid, gsid);

  if (g_ogl_config.bSupportsGLSLCache)
    glProgramParameteri(pid, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

  shader.SetProgramBindings(false);

  glLinkProgram(pid);

  // original shaders aren't needed any more
  glDeleteShader(vsid);
  glDeleteShader(psid);
  glDeleteShader(gsid);

  GLint linkStatus;
  glGetProgramiv(pid, GL_LINK_STATUS, &linkStatus);
  GLsizei length = 0;
  glGetProgramiv(pid, GL_INFO_LOG_LENGTH, &length);
  if (linkStatus != GL_TRUE || (length > 1 && DEBUG_GLSL))
  {
    std::string info_log;
    info_log.resize(length);
    glGetProgramInfoLog(pid, length, &length, &info_log[0]);
    ERROR_LOG(VIDEO, "Program info log:\n%s", info_log.c_str());

    std::string filename =
        StringFromFormat("%sbad_p_%d.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
    std::ofstream file;
    File::OpenFStream(file, filename, std::ios_base::out);
    file << s_glsl_header << vcode << s_glsl_header << pcode;
    if (!gcode.empty())
      file << s_glsl_header << gcode;
    file << info_log;
    file.close();

    if (linkStatus != GL_TRUE)
    {
      PanicAlert("Failed to link shaders: %s\n"
                 "Debug info (%s, %s, %s):\n%s",
                 filename.c_str(), g_ogl_config.gl_vendor, g_ogl_config.gl_renderer,
                 g_ogl_config.gl_version, info_log.c_str());
    }
  }
  if (linkStatus != GL_TRUE)
  {
    // Compile failed
    ERROR_LOG(VIDEO, "Program linking failed; see info log");

    // Don't try to use this shader
    glDeleteProgram(pid);
    return false;
  }

  shader.SetProgramVariables();

  return true;
}

bool ProgramShaderCache::CompileComputeShader(SHADER& shader, const std::string& code)
{
  // We need to enable GL_ARB_compute_shader for drivers that support the extension,
  // but not GLSL 4.3. Mesa is one example.
  std::string header;
  if (g_ActiveConfig.backend_info.bSupportsComputeShaders &&
      g_ogl_config.eSupportedGLSLVersion < GLSL_430)
  {
    header = "#extension GL_ARB_compute_shader : enable\n";
  }

  GLuint shader_id = CompileSingleShader(GL_COMPUTE_SHADER, header + code);
  if (!shader_id)
    return false;

  GLuint pid = shader.glprogid = glCreateProgram();
  glAttachShader(pid, shader_id);
  if (g_ogl_config.bSupportsGLSLCache)
    glProgramParameteri(pid, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

  shader.SetProgramBindings(true);

  glLinkProgram(pid);

  // original shaders aren't needed any more
  glDeleteShader(shader_id);

  GLint linkStatus;
  glGetProgramiv(pid, GL_LINK_STATUS, &linkStatus);
  GLsizei length = 0;
  glGetProgramiv(pid, GL_INFO_LOG_LENGTH, &length);
  if (linkStatus != GL_TRUE || (length > 1 && DEBUG_GLSL))
  {
    std::string info_log;
    info_log.resize(length);
    glGetProgramInfoLog(pid, length, &length, &info_log[0]);
    ERROR_LOG(VIDEO, "Program info log:\n%s", info_log.c_str());

    std::string filename =
        StringFromFormat("%sbad_p_%d.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
    std::ofstream file;
    File::OpenFStream(file, filename, std::ios_base::out);
    file << s_glsl_header << code;
    file << info_log;
    file.close();

    if (linkStatus != GL_TRUE)
    {
      PanicAlert("Failed to link shaders: %s\n"
                 "Debug info (%s, %s, %s):\n%s",
                 filename.c_str(), g_ogl_config.gl_vendor, g_ogl_config.gl_renderer,
                 g_ogl_config.gl_version, info_log.c_str());
    }
  }
  if (linkStatus != GL_TRUE)
  {
    // Compile failed
    ERROR_LOG(VIDEO, "Program linking failed; see info log");

    // Don't try to use this shader
    glDeleteProgram(pid);
    return false;
  }

  return true;
}

GLuint ProgramShaderCache::CompileSingleShader(GLuint type, const std::string& code)
{
  GLuint result = glCreateShader(type);

  const char* src[] = {s_glsl_header.c_str(), code.c_str()};

  glShaderSource(result, 2, src, nullptr);
  glCompileShader(result);
  GLint compileStatus;
  glGetShaderiv(result, GL_COMPILE_STATUS, &compileStatus);
  GLsizei length = 0;
  glGetShaderiv(result, GL_INFO_LOG_LENGTH, &length);

  if (compileStatus != GL_TRUE || (length > 1 && DEBUG_GLSL))
  {
    std::string info_log;
    info_log.resize(length);
    glGetShaderInfoLog(result, length, &length, &info_log[0]);

    const char* prefix = "";
    switch (type)
    {
    case GL_VERTEX_SHADER:
      prefix = "vs";
      break;
    case GL_GEOMETRY_SHADER:
      prefix = "gs";
      break;
    case GL_FRAGMENT_SHADER:
      prefix = "ps";
      break;
    case GL_COMPUTE_SHADER:
      prefix = "cs";
      break;
    }

    ERROR_LOG(VIDEO, "%s Shader info log:\n%s", prefix, info_log.c_str());

    std::string filename = StringFromFormat(
        "%sbad_%s_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), prefix, num_failures++);
    std::ofstream file;
    File::OpenFStream(file, filename, std::ios_base::out);
    file << s_glsl_header << code << info_log;
    file.close();

    if (compileStatus != GL_TRUE)
    {
      PanicAlert("Failed to compile %s shader: %s\n"
                 "Debug info (%s, %s, %s):\n%s",
                 prefix, filename.c_str(), g_ogl_config.gl_vendor, g_ogl_config.gl_renderer,
                 g_ogl_config.gl_version, info_log.c_str());
    }
  }
  if (compileStatus != GL_TRUE)
  {
    // Compile failed
    ERROR_LOG(VIDEO, "Shader compilation failed; see info log");

    // Don't try to use this shader
    glDeleteShader(result);
    return 0;
  }

  return result;
}

//gvx64 void ProgramShaderCache::GetShaderId(SHADERUID* uid, u32 primitive_type)
void ProgramShaderCache::GetShaderId(SHADERUID* uid, DSTALPHA_MODE dstAlphaMode, u32 primitive_type)  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
{
//gvx64  uid->puid = GetPixelShaderUid();
  uid->puid = GetPixelShaderUid(dstAlphaMode);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  uid->vuid = GetVertexShaderUid();
  uid->guid = GetGeometryShaderUid(primitive_type);
}

ProgramShaderCache::PCacheEntry ProgramShaderCache::GetShaderProgram()
{
  return *last_entry;
}

void ProgramShaderCache::Init()
{
  // We have to get the UBO alignment here because
  // if we generate a buffer that isn't aligned
  // then the UBO will fail.
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &s_ubo_align);

  s_ubo_buffer_size =
      static_cast<u32>(Common::AlignUp(sizeof(PixelShaderConstants), s_ubo_align) +
                       Common::AlignUp(sizeof(VertexShaderConstants), s_ubo_align) +
                       Common::AlignUp(sizeof(GeometryShaderConstants), s_ubo_align));

  // We multiply by *4*4 because we need to get down to basic machine units.
  // So multiply by four to get how many floats we have from vec4s
  // Then once more to get bytes
  s_buffer = StreamBuffer::Create(GL_UNIFORM_BUFFER, UBO_LENGTH);

  // Read our shader cache, only if supported and enabled
  if (g_ogl_config.bSupportsGLSLCache && g_ActiveConfig.bShaderCache)
  {
    GLint Supported;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &Supported);
    if (!Supported)
    {
      ERROR_LOG(VIDEO, "GL_ARB_get_program_binary is supported, but no binary format is known. So "
                       "disable shader cache.");
      g_ogl_config.bSupportsGLSLCache = false;
    }
    else
    {
      if (!File::Exists(File::GetUserPath(D_SHADERCACHE_IDX)))
        File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

      std::string cache_filename =
          StringFromFormat("%sogl-%s-shaders.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str(),
                           SConfig::GetInstance().GetGameID().c_str());

      ProgramShaderCacheInserter inserter;
      g_program_disk_cache.OpenAndRead(cache_filename, inserter);
    }
    SETSTAT(stats.numPixelShadersAlive, pshaders.size());
  }

  CreateHeader();

  CurrentProgram = 0;
  last_entry = nullptr;
}

void ProgramShaderCache::Shutdown()
{
  // store all shaders in cache on disk
  if (g_ogl_config.bSupportsGLSLCache)
  {
    for (auto& entry : pshaders)
    {
      // Clear any prior error code
      glGetError();

      if (entry.second.in_cache)
      {
        continue;
      }

      GLint link_status = GL_FALSE, delete_status = GL_TRUE, binary_size = 0;
      glGetProgramiv(entry.second.shader.glprogid, GL_LINK_STATUS, &link_status);
      glGetProgramiv(entry.second.shader.glprogid, GL_DELETE_STATUS, &delete_status);
      glGetProgramiv(entry.second.shader.glprogid, GL_PROGRAM_BINARY_LENGTH, &binary_size);
      if (glGetError() != GL_NO_ERROR || link_status == GL_FALSE || delete_status == GL_TRUE ||
          !binary_size)
      {
        continue;
      }

      std::vector<u8> data(binary_size + sizeof(GLenum));
      u8* binary = &data[sizeof(GLenum)];
      GLenum* prog_format = (GLenum*)&data[0];
      glGetProgramBinary(entry.second.shader.glprogid, binary_size, nullptr, prog_format, binary);
      if (glGetError() != GL_NO_ERROR)
      {
        continue;
      }

      g_program_disk_cache.Append(entry.first, &data[0], binary_size + sizeof(GLenum));
    }

    g_program_disk_cache.Sync();
    g_program_disk_cache.Close();
  }

  glUseProgram(0);

  for (auto& entry : pshaders)
  {
    entry.second.Destroy();
  }
  pshaders.clear();

  s_buffer.reset();
}

void ProgramShaderCache::CreateHeader()
{
  GLSL_VERSION v = g_ogl_config.eSupportedGLSLVersion;
  bool is_glsles = v >= GLSLES_300;
  std::string SupportedESPointSize;
  std::string SupportedESTextureBuffer;
  switch (g_ogl_config.SupportedESPointSize)
  {
  case 1:
    SupportedESPointSize = "#extension GL_OES_geometry_point_size : enable";
    break;
  case 2:
    SupportedESPointSize = "#extension GL_EXT_geometry_point_size : enable";
    break;
  default:
    SupportedESPointSize = "";
    break;
  }

  switch (g_ogl_config.SupportedESTextureBuffer)
  {
  case ES_TEXBUF_TYPE::TEXBUF_EXT:
    SupportedESTextureBuffer = "#extension GL_EXT_texture_buffer : enable";
    break;
  case ES_TEXBUF_TYPE::TEXBUF_OES:
    SupportedESTextureBuffer = "#extension GL_OES_texture_buffer : enable";
    break;
  case ES_TEXBUF_TYPE::TEXBUF_CORE:
  case ES_TEXBUF_TYPE::TEXBUF_NONE:
    SupportedESTextureBuffer = "";
    break;
  }

  std::string earlyz_string = "";
  if (g_ActiveConfig.backend_info.bSupportsEarlyZ)
  {
    if (g_ogl_config.bSupportsImageLoadStore)
    {
      earlyz_string = "#define FORCE_EARLY_Z layout(early_fragment_tests) in\n";
    }
    else if (g_ogl_config.bSupportsConservativeDepth)
    {
      // See PixelShaderGen for details about this fallback.
      earlyz_string = "#define FORCE_EARLY_Z layout(depth_unchanged) out float gl_FragDepth\n";
      earlyz_string += "#extension GL_ARB_conservative_depth : enable\n";
    }
  }

  s_glsl_header = StringFromFormat(
      "%s\n"
      "%s\n"  // ubo
      "%s\n"  // early-z
      "%s\n"  // 420pack
      "%s\n"  // msaa
      "%s\n"  // Input/output/sampler binding
      "%s\n"  // Varying location
      "%s\n"  // storage buffer
      "%s\n"  // shader5
      "%s\n"  // SSAA
      "%s\n"  // Geometry point size
      "%s\n"  // AEP
      "%s\n"  // texture buffer
      "%s\n"  // ES texture buffer
      "%s\n"  // ES dual source blend
      "%s\n"  // shader image load store

      // Precision defines for GLSL ES
      "%s\n"
      "%s\n"
      "%s\n"
      "%s\n"
      "%s\n"
      "%s\n"

      // Silly differences
      "#define float2 vec2\n"
      "#define float3 vec3\n"
      "#define float4 vec4\n"
      "#define uint2 uvec2\n"
      "#define uint3 uvec3\n"
      "#define uint4 uvec4\n"
      "#define int2 ivec2\n"
      "#define int3 ivec3\n"
      "#define int4 ivec4\n"

      // hlsl to glsl function translation
      "#define frac fract\n"
      "#define lerp mix\n"

      ,
      GetGLSLVersionString().c_str(),
      v < GLSL_140 ? "#extension GL_ARB_uniform_buffer_object : enable" : "", earlyz_string.c_str(),
      (g_ActiveConfig.backend_info.bSupportsBindingLayout && v < GLSLES_310) ?
          "#extension GL_ARB_shading_language_420pack : enable" :
          "",
      (g_ogl_config.bSupportsMSAA && v < GLSL_150) ?
          "#extension GL_ARB_texture_multisample : enable" :
          "",
      // Attribute and fragment output bindings are still done via glBindAttribLocation and
      // glBindFragDataLocation. In the future this could be moved to the layout qualifier
      // in GLSL, but requires verification of GL_ARB_explicit_attrib_location.
      g_ActiveConfig.backend_info.bSupportsBindingLayout ?
          "#define ATTRIBUTE_LOCATION(x)\n"
          "#define FRAGMENT_OUTPUT_LOCATION(x)\n"
          "#define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y)\n"
          "#define UBO_BINDING(packing, x) layout(packing, binding = x)\n"
          "#define SAMPLER_BINDING(x) layout(binding = x)\n"
          "#define SSBO_BINDING(x) layout(binding = x)\n" :
          "#define ATTRIBUTE_LOCATION(x)\n"
          "#define FRAGMENT_OUTPUT_LOCATION(x)\n"
          "#define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y)\n"
          "#define UBO_BINDING(packing, x) layout(packing)\n"
          "#define SAMPLER_BINDING(x)\n",
      // Input/output blocks are matched by name during program linking
      "#define VARYING_LOCATION(x)\n",
      !is_glsles && g_ActiveConfig.backend_info.bSupportsFragmentStoresAndAtomics ?
          "#extension GL_ARB_shader_storage_buffer_object : enable" :
          "",
      v < GLSL_400 && g_ActiveConfig.backend_info.bSupportsGSInstancing ?
          "#extension GL_ARB_gpu_shader5 : enable" :
          "",
      v < GLSL_400 && g_ActiveConfig.backend_info.bSupportsSSAA ?
          "#extension GL_ARB_sample_shading : enable" :
          "",
      SupportedESPointSize.c_str(),
      g_ogl_config.bSupportsAEP ? "#extension GL_ANDROID_extension_pack_es31a : enable" : "",
      v < GLSL_140 && g_ActiveConfig.backend_info.bSupportsPaletteConversion ?
          "#extension GL_ARB_texture_buffer_object : enable" :
          "",
      SupportedESTextureBuffer.c_str(),
      is_glsles && g_ActiveConfig.backend_info.bSupportsDualSourceBlend ?
          "#extension GL_EXT_blend_func_extended : enable" :
          ""

      ,
      g_ogl_config.bSupportsImageLoadStore &&
              ((!is_glsles && v < GLSL_430) || (is_glsles && v < GLSLES_310)) ?
          "#extension GL_ARB_shader_image_load_store : enable" :
          "",
      is_glsles ? "precision highp float;" : "", is_glsles ? "precision highp int;" : "",
      is_glsles ? "precision highp sampler2DArray;" : "",
      (is_glsles && g_ActiveConfig.backend_info.bSupportsPaletteConversion) ?
          "precision highp usamplerBuffer;" :
          "",
      v > GLSLES_300 ? "precision highp sampler2DMS;" : "",
      v >= GLSLES_310 ? "precision highp image2DArray;" : "");
}

void ProgramShaderCache::ProgramShaderCacheInserter::Read(const SHADERUID& key, const u8* value,
                                                          u32 value_size)
{
  const u8* binary = value + sizeof(GLenum);
  GLenum* prog_format = (GLenum*)value;
  GLint binary_size = value_size - sizeof(GLenum);

  PCacheEntry entry;
  entry.in_cache = 1;
  entry.shader.glprogid = glCreateProgram();
  glProgramBinary(entry.shader.glprogid, *prog_format, binary, binary_size);

  GLint success;
  glGetProgramiv(entry.shader.glprogid, GL_LINK_STATUS, &success);

  if (success)
  {
    pshaders[key] = entry;
    entry.shader.SetProgramVariables();
  }
  else
  {
    glDeleteProgram(entry.shader.glprogid);
  }
}

}  // namespace OGL
