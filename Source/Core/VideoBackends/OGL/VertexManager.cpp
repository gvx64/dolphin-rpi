// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/OGL/VertexManager.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/GL/GLExtensions/GLExtensions.h"
#include "Common/StringUtil.h"
#include "Common/GL/GLInterfaceBase.h" //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

#include "VideoBackends/OGL/BoundingBox.h"
#include "VideoBackends/OGL/ProgramShaderCache.h"
#include "VideoBackends/OGL/Render.h"
#include "VideoBackends/OGL/StreamBuffer.h"
#include "VideoCommon/BoundingBox.h"

#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace OGL
{
// This are the initially requested size for the buffers expressed in bytes
const u32 MAX_IBUFFER_SIZE = 2 * 1024 * 1024;
const u32 MAX_VBUFFER_SIZE = 32 * 1024 * 1024;

static std::unique_ptr<StreamBuffer> s_vertexBuffer;
static std::unique_ptr<StreamBuffer> s_indexBuffer;
static size_t s_baseVertex;
static size_t s_index_offset;

VertexManager::VertexManager() : m_cpu_v_buffer(MAX_VBUFFER_SIZE), m_cpu_i_buffer(MAX_IBUFFER_SIZE)
{
  CreateDeviceObjects();
}

VertexManager::~VertexManager()
{
  DestroyDeviceObjects();
}

void VertexManager::CreateDeviceObjects()
{
  s_vertexBuffer = StreamBuffer::Create(GL_ARRAY_BUFFER, MAX_VBUFFER_SIZE);
  m_vertex_buffers = s_vertexBuffer->m_buffer;

  s_indexBuffer = StreamBuffer::Create(GL_ELEMENT_ARRAY_BUFFER, MAX_IBUFFER_SIZE);
  m_index_buffers = s_indexBuffer->m_buffer;

  m_last_vao = 0;
}

void VertexManager::DestroyDeviceObjects()
{
  s_vertexBuffer.reset();
  s_indexBuffer.reset();
}

void VertexManager::PrepareDrawBuffers(u32 stride)
{
  u32 vertex_data_size = IndexGenerator::GetNumVerts() * stride;
  u32 index_data_size = IndexGenerator::GetIndexLen() * sizeof(u16);

  s_vertexBuffer->Unmap(vertex_data_size);
  s_indexBuffer->Unmap(index_data_size);

  ADDSTAT(stats.thisFrame.bytesVertexStreamed, vertex_data_size);
  ADDSTAT(stats.thisFrame.bytesIndexStreamed, index_data_size);
}

void VertexManager::ResetBuffer(u32 stride)
{
  if (m_cull_all)
  {
    // This buffer isn't getting sent to the GPU. Just allocate it on the cpu.
    m_cur_buffer_pointer = m_base_buffer_pointer = m_cpu_v_buffer.data();
    m_end_buffer_pointer = m_base_buffer_pointer + m_cpu_v_buffer.size();

    IndexGenerator::Start((u16*)m_cpu_i_buffer.data());
  }
  else
  {
    auto buffer = s_vertexBuffer->Map(MAXVBUFFERSIZE, stride);
    m_cur_buffer_pointer = m_base_buffer_pointer = buffer.first;
    m_end_buffer_pointer = buffer.first + MAXVBUFFERSIZE;
    s_baseVertex = buffer.second / stride;

    buffer = s_indexBuffer->Map(MAXIBUFFERSIZE * sizeof(u16));
    IndexGenerator::Start((u16*)buffer.first);
    s_index_offset = buffer.second;
  }
}

void VertexManager::Draw(u32 stride)
{
  u32 index_size = IndexGenerator::GetIndexLen();
  u32 max_index = IndexGenerator::GetNumVerts();
  GLenum primitive_mode = 0;

  switch (m_current_primitive_type)
  {
  case PRIMITIVE_POINTS:
    primitive_mode = GL_POINTS;
    glDisable(GL_CULL_FACE);
    break;
  case PRIMITIVE_LINES:
    primitive_mode = GL_LINES;
    glDisable(GL_CULL_FACE);
    break;
  case PRIMITIVE_TRIANGLES:
    primitive_mode =
        g_ActiveConfig.backend_info.bSupportsPrimitiveRestart ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
    break;
  }

  if (g_ogl_config.bSupportsGLBaseVertex)
  {
    glDrawRangeElementsBaseVertex(primitive_mode, 0, max_index, index_size, GL_UNSIGNED_SHORT,
                                  (u8*)nullptr + s_index_offset, (GLint)s_baseVertex);
  }
  else
  {
    glDrawRangeElements(primitive_mode, 0, max_index, index_size, GL_UNSIGNED_SHORT,
                        (u8*)nullptr + s_index_offset);
  }

  INCSTAT(stats.thisFrame.numDrawCalls);

  if (m_current_primitive_type != PRIMITIVE_TRIANGLES)
    static_cast<Renderer*>(g_renderer.get())->SetGenerationMode();
}

//gvx64 void VertexManager::vFlush()
void VertexManager::vFlush(bool useDstAlpha) //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
{
  GLVertexFormat* nativeVertexFmt = (GLVertexFormat*)VertexLoaderManager::GetCurrentVertexFormat();
  u32 stride = nativeVertexFmt->GetVertexStride();

  if (m_last_vao != nativeVertexFmt->VAO)
  {
    glBindVertexArray(nativeVertexFmt->VAO);
    m_last_vao = nativeVertexFmt->VAO;
  }

  PrepareDrawBuffers(stride);

  // Makes sure we can actually do Dual source blending
  bool dualSourcePossible = g_ActiveConfig.backend_info.bSupportsDualSourceBlend; //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

  // If host supports GL_ARB_blend_func_extended, we can do dst alpha in
  // the same pass as regular rendering.
  if (useDstAlpha && dualSourcePossible)  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  { 
    ProgramShaderCache::SetShader(DSTALPHA_DUAL_SOURCE_BLEND, m_current_primitive_type);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  }
  else
  {
    ProgramShaderCache::SetShader(DSTALPHA_NONE, m_current_primitive_type);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  }

  // upload global constants
  ProgramShaderCache::UploadConstants();

  // setup the pointers
  nativeVertexFmt->SetupVertexPointers();

  if (::BoundingBox::active && !g_Config.BBoxUseFragmentShaderImplementation())
  {
    glEnable(GL_STENCIL_TEST);
  }

  Draw(stride);

  // If the GPU does not support dual-source blending, we can approximate the effect by drawing
  // the object a second time, with the write mask set to alpha only using a shader that outputs
  // the destination/constant alpha value (which would normally be SRC_COLOR.a).
  //
  // This is also used when logic ops and destination alpha is enabled, since we can't enable
  // blending and logic ops concurrently.
  bool logic_op_enabled = (bpmem.blendmode.logicopenable && !bpmem.blendmode.blendenable &&
                           GLInterface->GetMode() == GLInterfaceMode::MODE_OPENGL);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  if (useDstAlpha && (!dualSourcePossible || logic_op_enabled))  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  {
    ProgramShaderCache::SetShader(DSTALPHA_ALPHA_PASS, m_current_primitive_type);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    // only update alpha
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    glDisable(GL_BLEND);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    if (logic_op_enabled)  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
      glDisable(GL_COLOR_LOGIC_OP);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    Draw(stride);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    // restore color mask
    g_renderer->SetColorMask();  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

    if (bpmem.blendmode.blendenable || bpmem.blendmode.subtract)  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
      glEnable(GL_BLEND); //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
 
    if (logic_op_enabled)  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
      glEnable(GL_COLOR_LOGIC_OP);  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass
  }  //gvx64 - Rollback to 5.0-1651 - Reintroduce Vulkan Alpha Pass

  if (::BoundingBox::active && !g_Config.BBoxUseFragmentShaderImplementation())
  {
    OGL::BoundingBox::StencilWasUpdated();
    glDisable(GL_STENCIL_TEST);
  }

#if defined(_DEBUG) || defined(DEBUGFAST)
  if (g_ActiveConfig.iLog & CONF_SAVESHADERS)
  {
    // save the shaders
    ProgramShaderCache::PCacheEntry prog = ProgramShaderCache::GetShaderProgram();
    std::string filename = StringFromFormat(
        "%sps%.3d.txt", File::GetUserPath(D_DUMPFRAMES_IDX).c_str(), g_ActiveConfig.iSaveTargetId);
    std::ofstream fps;
    File::OpenFStream(fps, filename, std::ios_base::out);
    fps << prog.shader.strpprog;

    filename = StringFromFormat("%svs%.3d.txt", File::GetUserPath(D_DUMPFRAMES_IDX).c_str(),
                                g_ActiveConfig.iSaveTargetId);
    std::ofstream fvs;
    File::OpenFStream(fvs, filename, std::ios_base::out);
    fvs << prog.shader.strvprog;
  }
#endif
  g_Config.iSaveTargetId++;
  ClearEFBCache();
}

}  // namespace
