// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Vulkan/StateTracker.h"

#include <cstring>

#include "Common/Align.h"
#include "Common/Assert.h"

#include "VideoBackends/Vulkan/CommandBufferManager.h"
#include "VideoBackends/Vulkan/Constants.h"
#include "VideoBackends/Vulkan/FramebufferManager.h"
#include "VideoBackends/Vulkan/ObjectCache.h"
#include "VideoBackends/Vulkan/StreamBuffer.h"
#include "VideoBackends/Vulkan/Util.h"
#include "VideoBackends/Vulkan/VertexFormat.h"
#include "VideoBackends/Vulkan/VulkanContext.h"

#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace Vulkan
{
static std::unique_ptr<StateTracker> s_state_tracker;

StateTracker* StateTracker::GetInstance()
{
  return s_state_tracker.get();
}

bool StateTracker::CreateInstance()
{
  _assert_(!s_state_tracker);
  s_state_tracker = std::make_unique<StateTracker>();
  if (!s_state_tracker->Initialize())
  {
    s_state_tracker.reset();
    return false;
  }
  return true;
}

void StateTracker::DestroyInstance()
{
  s_state_tracker.reset();
}

bool StateTracker::Initialize()
{
  // Set some sensible defaults
  m_pipeline_state.rasterization_state.cull_mode = VK_CULL_MODE_NONE;
  m_pipeline_state.rasterization_state.per_sample_shading = VK_FALSE;
  m_pipeline_state.rasterization_state.depth_clamp = VK_FALSE;
  m_pipeline_state.depth_stencil_state.test_enable = VK_TRUE;
  m_pipeline_state.depth_stencil_state.write_enable = VK_TRUE;
  m_pipeline_state.depth_stencil_state.compare_op = VK_COMPARE_OP_LESS;
  m_pipeline_state.blend_state.hex = 0;
  m_pipeline_state.blend_state.blendenable = false;
  m_pipeline_state.blend_state.srcfactor = BlendMode::ONE;
  m_pipeline_state.blend_state.srcfactoralpha = BlendMode::ONE;
  m_pipeline_state.blend_state.dstfactor = BlendMode::ZERO;
  m_pipeline_state.blend_state.dstfactoralpha = BlendMode::ZERO;
  m_pipeline_state.blend_state.colorupdate = true;
  m_pipeline_state.blend_state.alphaupdate = true;

  // Enable depth clamping if supported by driver.
  if (g_ActiveConfig.backend_info.bSupportsDepthClamp)
    m_pipeline_state.rasterization_state.depth_clamp = VK_TRUE;

  // BBox is disabled by default.
  m_pipeline_state.pipeline_layout = g_object_cache->GetPipelineLayout(PIPELINE_LAYOUT_STANDARD);
  m_num_active_descriptor_sets = NUM_GX_DRAW_DESCRIPTOR_SETS;
  m_bbox_enabled = false;

  // Initialize all samplers to point by default
  for (size_t i = 0; i < NUM_PIXEL_SHADER_SAMPLERS; i++)
  {
    m_bindings.ps_samplers[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_bindings.ps_samplers[i].imageView = VK_NULL_HANDLE;
    m_bindings.ps_samplers[i].sampler = g_object_cache->GetPointSampler();
  }

  // Create the streaming uniform buffer
  m_uniform_stream_buffer =
      StreamBuffer::Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, INITIAL_UNIFORM_STREAM_BUFFER_SIZE,
                           MAXIMUM_UNIFORM_STREAM_BUFFER_SIZE);
  if (!m_uniform_stream_buffer)
  {
    PanicAlert("Failed to create uniform stream buffer");
    return false;
  }

  // The validation layer complains if max(offsets) + max(ubo_ranges) >= ubo_size.
  // To work around this we reserve the maximum buffer size at all times, but only commit
  // as many bytes as we use.
  m_uniform_buffer_reserve_size = sizeof(PixelShaderConstants);
  m_uniform_buffer_reserve_size = Common::AlignUp(m_uniform_buffer_reserve_size,
                                                  g_vulkan_context->GetUniformBufferAlignment()) +
                                  sizeof(VertexShaderConstants);
  m_uniform_buffer_reserve_size = Common::AlignUp(m_uniform_buffer_reserve_size,
                                                  g_vulkan_context->GetUniformBufferAlignment()) +
                                  sizeof(GeometryShaderConstants);

  // Default dirty flags include all descriptors
  InvalidateDescriptorSets();
  SetPendingRebind();

  // Set default constants
  UploadAllConstants();
  return true;
}

void StateTracker::LoadPipelineUIDCache()
{
  class PipelineInserter final : public LinearDiskCacheReader<SerializedPipelineUID, u32>
  {
  public:
    explicit PipelineInserter(StateTracker* this_ptr_) : this_ptr(this_ptr_) {}
    void Read(const SerializedPipelineUID& key, const u32* value, u32 value_size)
    {
      this_ptr->PrecachePipelineUID(key);
    }

  private:
    StateTracker* this_ptr;
  };

  std::string filename = g_object_cache->GetDiskCacheFileName("pipeline-uid");
  PipelineInserter inserter(this);

  // OpenAndRead calls Close() first, which will flush all data to disk when reloading.
  // This assertion must hold true, otherwise data corruption will result.
  m_uid_cache.OpenAndRead(filename, inserter);
}

void StateTracker::AppendToPipelineUIDCache(const PipelineInfo& info)
{
  SerializedPipelineUID sinfo;
  sinfo.blend_state_bits = info.blend_state.hex;
  sinfo.rasterizer_state_bits = info.rasterization_state.bits;
  sinfo.depth_stencil_state_bits = info.depth_stencil_state.bits;
  sinfo.vertex_decl = m_pipeline_state.vertex_format->GetVertexDeclaration();
  sinfo.vs_uid = m_vs_uid;
  sinfo.gs_uid = m_gs_uid;
  sinfo.ps_uid = m_ps_uid;
  sinfo.primitive_topology = info.primitive_topology;

  u32 dummy_value = 0;
  m_uid_cache.Append(sinfo, &dummy_value, 1);
}

bool StateTracker::PrecachePipelineUID(const SerializedPipelineUID& uid)
{
  PipelineInfo pinfo = {};

  // Need to create the vertex declaration first, rather than deferring to when a game creates a
  // vertex loader that uses this format, since we need it to create a pipeline.
  pinfo.vertex_format = VertexFormat::GetOrCreateMatchingFormat(uid.vertex_decl);
  pinfo.pipeline_layout = uid.ps_uid.GetUidData()->bounding_box ?
                              g_object_cache->GetPipelineLayout(PIPELINE_LAYOUT_BBOX) :
                              g_object_cache->GetPipelineLayout(PIPELINE_LAYOUT_STANDARD);
  pinfo.vs = g_object_cache->GetVertexShaderForUid(uid.vs_uid);
  if (pinfo.vs == VK_NULL_HANDLE)
  {
    WARN_LOG(VIDEO, "Failed to get vertex shader from cached UID.");
    return false;
  }
  if (g_vulkan_context->SupportsGeometryShaders() && !uid.gs_uid.GetUidData()->IsPassthrough())
  {
    pinfo.gs = g_object_cache->GetGeometryShaderForUid(uid.gs_uid);
    if (pinfo.gs == VK_NULL_HANDLE)
    {
      WARN_LOG(VIDEO, "Failed to get geometry shader from cached UID.");
      return false;
    }
  }
  pinfo.ps = g_object_cache->GetPixelShaderForUid(uid.ps_uid);
  if (pinfo.ps == VK_NULL_HANDLE)
  {
    WARN_LOG(VIDEO, "Failed to get pixel shader from cached UID.");
    return false;
  }
  pinfo.render_pass = m_load_render_pass;
  pinfo.rasterization_state.bits = uid.rasterizer_state_bits;
  pinfo.depth_stencil_state.bits = uid.depth_stencil_state_bits;
  pinfo.blend_state.hex = uid.blend_state_bits;
  pinfo.primitive_topology = uid.primitive_topology;

  VkPipeline pipeline = g_object_cache->GetPipeline(pinfo);
  if (pipeline == VK_NULL_HANDLE)
  {
    WARN_LOG(VIDEO, "Failed to get pipeline from cached UID.");
    return false;
  }

  // We don't need to do anything with this pipeline, just make sure it exists.
  return true;
}

void StateTracker::SetVertexBuffer(VkBuffer buffer, VkDeviceSize offset)
{
  if (m_vertex_buffer == buffer && m_vertex_buffer_offset == offset)
    return;

  m_vertex_buffer = buffer;
  m_vertex_buffer_offset = offset;
  m_dirty_flags |= DIRTY_FLAG_VERTEX_BUFFER;
}

void StateTracker::SetIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
  if (m_index_buffer == buffer && m_index_buffer_offset == offset && m_index_type == type)
    return;

  m_index_buffer = buffer;
  m_index_buffer_offset = offset;
  m_index_type = type;
  m_dirty_flags |= DIRTY_FLAG_INDEX_BUFFER;
}

void StateTracker::SetRenderPass(VkRenderPass load_render_pass, VkRenderPass clear_render_pass)
{
  // Should not be changed within a render pass.
  _assert_(!InRenderPass());

  // The clear and load render passes are compatible, so we don't need to change our pipeline.
  if (m_pipeline_state.render_pass != load_render_pass)
  {
    m_pipeline_state.render_pass = load_render_pass;
    m_dirty_flags |= DIRTY_FLAG_PIPELINE;
  }

  m_load_render_pass = load_render_pass;
  m_clear_render_pass = clear_render_pass;
}

void StateTracker::SetFramebuffer(VkFramebuffer framebuffer, const VkRect2D& render_area)
{
  // Should not be changed within a render pass.
  _assert_(!InRenderPass());
  m_framebuffer = framebuffer;
  m_framebuffer_size = render_area;
}

void StateTracker::SetVertexFormat(const VertexFormat* vertex_format)
{
  if (m_pipeline_state.vertex_format == vertex_format)
    return;

  m_pipeline_state.vertex_format = vertex_format;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

void StateTracker::SetPrimitiveTopology(VkPrimitiveTopology primitive_topology)
{
  if (m_pipeline_state.primitive_topology == primitive_topology)
    return;

  m_pipeline_state.primitive_topology = primitive_topology;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

void StateTracker::DisableBackFaceCulling()
{
  if (m_pipeline_state.rasterization_state.cull_mode == VK_CULL_MODE_NONE)
    return;

  m_pipeline_state.rasterization_state.cull_mode = VK_CULL_MODE_NONE;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

void StateTracker::SetRasterizationState(const RasterizationState& state)
{
  if (m_pipeline_state.rasterization_state.bits == state.bits)
    return;

  m_pipeline_state.rasterization_state.bits = state.bits;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

void StateTracker::SetDepthStencilState(const DepthStencilState& state)
{
  if (m_pipeline_state.depth_stencil_state.bits == state.bits)
    return;

  m_pipeline_state.depth_stencil_state.bits = state.bits;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

void StateTracker::SetBlendState(const BlendingState& state)
{
  if (m_pipeline_state.blend_state.hex == state.hex)
    return;

  m_pipeline_state.blend_state.hex = state.hex;
  m_dirty_flags |= DIRTY_FLAG_PIPELINE;
}

//gvx64 bool StateTracker::CheckForShaderChanges(u32 gx_primitive_type)
bool StateTracker::CheckForShaderChanges(u32 gx_primitive_type, DSTALPHA_MODE dstalpha_mode) //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass
{
  VertexShaderUid vs_uid = GetVertexShaderUid();
//gvx64  PixelShaderUid ps_uid = GetPixelShaderUid();
 PixelShaderUid ps_uid = GetPixelShaderUid(dstalpha_mode); //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass

  bool changed = false;

  if (vs_uid != m_vs_uid)
  {
    m_pipeline_state.vs = g_object_cache->GetVertexShaderForUid(vs_uid);
    m_vs_uid = vs_uid;
    changed = true;
  }

  if (g_vulkan_context->SupportsGeometryShaders())
  {
    GeometryShaderUid gs_uid = GetGeometryShaderUid(gx_primitive_type);
    if (gs_uid != m_gs_uid)
    {
      if (gs_uid.GetUidData()->IsPassthrough())
        m_pipeline_state.gs = VK_NULL_HANDLE;
      else
        m_pipeline_state.gs = g_object_cache->GetGeometryShaderForUid(gs_uid);

      m_gs_uid = gs_uid;
      changed = true;
    }
  }

  if (ps_uid != m_ps_uid)
  {
    m_pipeline_state.ps = g_object_cache->GetPixelShaderForUid(ps_uid);
    m_ps_uid = ps_uid;
    changed = true;
  }

  if (m_dstalpha_mode != dstalpha_mode) //gvx64 rollback to 5.0-1651 (reintroduce Vulkan alpha pass)
  {
    // Switching to/from alpha pass requires a pipeline change, since the blend state
    // is overridden in the destination alpha pass.
    if (m_dstalpha_mode == DSTALPHA_ALPHA_PASS || dstalpha_mode == DSTALPHA_ALPHA_PASS)
      changed = true;

    m_dstalpha_mode = dstalpha_mode;
  }  //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass

  if (changed)
    m_dirty_flags |= DIRTY_FLAG_PIPELINE;

  return changed;
}

void StateTracker::UpdateVertexShaderConstants()
{
  if (!VertexShaderManager::dirty || !ReserveConstantStorage())
    return;

  // Buffer allocation changed?
  if (m_uniform_stream_buffer->GetBuffer() !=
      m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_VS].buffer)
  {
    m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_VS].buffer =
        m_uniform_stream_buffer->GetBuffer();
    m_dirty_flags |= DIRTY_FLAG_VS_UBO;
  }

  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_VS] =
      static_cast<uint32_t>(m_uniform_stream_buffer->GetCurrentOffset());
  m_dirty_flags |= DIRTY_FLAG_DYNAMIC_OFFSETS;

  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer(), &VertexShaderManager::constants,
         sizeof(VertexShaderConstants));
  ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(VertexShaderConstants));
  m_uniform_stream_buffer->CommitMemory(sizeof(VertexShaderConstants));
  VertexShaderManager::dirty = false;
}

void StateTracker::UpdateGeometryShaderConstants()
{
  // Skip updating geometry shader constants if it's not in use.
  if (m_pipeline_state.gs == VK_NULL_HANDLE)
  {
    // However, if the buffer has changed, we can't skip the update, because then we'll
    // try to include the now non-existant buffer in the descriptor set.
    if (m_uniform_stream_buffer->GetBuffer() ==
        m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_GS].buffer)
    {
      return;
    }

    GeometryShaderManager::dirty = true;
  }

  if (!GeometryShaderManager::dirty || !ReserveConstantStorage())
    return;

  // Buffer allocation changed?
  if (m_uniform_stream_buffer->GetBuffer() !=
      m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_GS].buffer)
  {
    m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_GS].buffer =
        m_uniform_stream_buffer->GetBuffer();
    m_dirty_flags |= DIRTY_FLAG_GS_UBO;
  }

  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_GS] =
      static_cast<uint32_t>(m_uniform_stream_buffer->GetCurrentOffset());
  m_dirty_flags |= DIRTY_FLAG_DYNAMIC_OFFSETS;

  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer(), &GeometryShaderManager::constants,
         sizeof(GeometryShaderConstants));
  ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(GeometryShaderConstants));
  m_uniform_stream_buffer->CommitMemory(sizeof(GeometryShaderConstants));
  GeometryShaderManager::dirty = false;
}

void StateTracker::UpdatePixelShaderConstants()
{
  if (!PixelShaderManager::dirty || !ReserveConstantStorage())
    return;

  // Buffer allocation changed?
  if (m_uniform_stream_buffer->GetBuffer() !=
      m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_PS].buffer)
  {
    m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_PS].buffer =
        m_uniform_stream_buffer->GetBuffer();
    m_dirty_flags |= DIRTY_FLAG_PS_UBO;
  }

  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_PS] =
      static_cast<uint32_t>(m_uniform_stream_buffer->GetCurrentOffset());
  m_dirty_flags |= DIRTY_FLAG_DYNAMIC_OFFSETS;

  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer(), &PixelShaderManager::constants,
         sizeof(PixelShaderConstants));
  ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(PixelShaderConstants));
  m_uniform_stream_buffer->CommitMemory(sizeof(PixelShaderConstants));
  PixelShaderManager::dirty = false;
}

bool StateTracker::ReserveConstantStorage()
{
  // Since we invalidate all constants on command buffer execution, it doesn't matter if this
  // causes the stream buffer to be resized.
  if (m_uniform_stream_buffer->ReserveMemory(m_uniform_buffer_reserve_size,
                                             g_vulkan_context->GetUniformBufferAlignment(), true,
                                             true, false))
  {
    return true;
  }

  // The only places that call constant updates are safe to have state restored.
  WARN_LOG(VIDEO, "Executing command buffer while waiting for space in uniform buffer");
  Util::ExecuteCurrentCommandsAndRestoreState(false);

  // Since we are on a new command buffer, all constants have been invalidated, and we need
  // to reupload them. We may as well do this now, since we're issuing a draw anyway.
  UploadAllConstants();
  return false;
}

void StateTracker::UploadAllConstants()
{
  // We are free to re-use parts of the buffer now since we're uploading all constants.
  size_t ub_alignment = g_vulkan_context->GetUniformBufferAlignment();
  size_t pixel_constants_offset = 0;
  size_t vertex_constants_offset =
      Common::AlignUp(pixel_constants_offset + sizeof(PixelShaderConstants), ub_alignment);
  size_t geometry_constants_offset =
      Common::AlignUp(vertex_constants_offset + sizeof(VertexShaderConstants), ub_alignment);
  size_t allocation_size = geometry_constants_offset + sizeof(GeometryShaderConstants);

  // Allocate everything at once.
  // We should only be here if the buffer was full and a command buffer was submitted anyway.
  if (!m_uniform_stream_buffer->ReserveMemory(allocation_size, ub_alignment, true, true, false))
  {
    PanicAlert("Failed to allocate space for constants in streaming buffer");
    return;
  }

  // Update bindings
  for (size_t i = 0; i < NUM_UBO_DESCRIPTOR_SET_BINDINGS; i++)
  {
    m_bindings.uniform_buffer_bindings[i].buffer = m_uniform_stream_buffer->GetBuffer();
    m_bindings.uniform_buffer_bindings[i].offset = 0;
  }
  m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_PS].range =
      sizeof(PixelShaderConstants);
  m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_VS].range =
      sizeof(VertexShaderConstants);
  m_bindings.uniform_buffer_bindings[UBO_DESCRIPTOR_SET_BINDING_GS].range =
      sizeof(GeometryShaderConstants);

  // Update dynamic offsets
  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_PS] =
      static_cast<uint32_t>(m_uniform_stream_buffer->GetCurrentOffset() + pixel_constants_offset);

  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_VS] =
      static_cast<uint32_t>(m_uniform_stream_buffer->GetCurrentOffset() + vertex_constants_offset);

  m_bindings.uniform_buffer_offsets[UBO_DESCRIPTOR_SET_BINDING_GS] = static_cast<uint32_t>(
      m_uniform_stream_buffer->GetCurrentOffset() + geometry_constants_offset);

  m_dirty_flags |= DIRTY_FLAG_ALL_DESCRIPTOR_SETS | DIRTY_FLAG_DYNAMIC_OFFSETS | DIRTY_FLAG_VS_UBO |
                   DIRTY_FLAG_GS_UBO | DIRTY_FLAG_PS_UBO;

  // Copy the actual data in
  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer() + pixel_constants_offset,
         &PixelShaderManager::constants, sizeof(PixelShaderConstants));
  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer() + vertex_constants_offset,
         &VertexShaderManager::constants, sizeof(VertexShaderConstants));
  memcpy(m_uniform_stream_buffer->GetCurrentHostPointer() + geometry_constants_offset,
         &GeometryShaderManager::constants, sizeof(GeometryShaderConstants));

  // Finally, flush buffer memory after copying
  m_uniform_stream_buffer->CommitMemory(allocation_size);

  // Clear dirty flags
  VertexShaderManager::dirty = false;
  GeometryShaderManager::dirty = false;
  PixelShaderManager::dirty = false;
}

void StateTracker::SetTexture(size_t index, VkImageView view)
{
  if (m_bindings.ps_samplers[index].imageView == view)
    return;

  m_bindings.ps_samplers[index].imageView = view;
  m_bindings.ps_samplers[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  m_dirty_flags |= DIRTY_FLAG_PS_SAMPLERS;
}

void StateTracker::SetSampler(size_t index, VkSampler sampler)
{
  if (m_bindings.ps_samplers[index].sampler == sampler)
    return;

  m_bindings.ps_samplers[index].sampler = sampler;
  m_dirty_flags |= DIRTY_FLAG_PS_SAMPLERS;
}

void StateTracker::SetBBoxEnable(bool enable)
{
  if (m_bbox_enabled == enable)
    return;

  // Change the number of active descriptor sets, as well as the pipeline layout
  if (enable)
  {
    m_pipeline_state.pipeline_layout = g_object_cache->GetPipelineLayout(PIPELINE_LAYOUT_BBOX);
    m_num_active_descriptor_sets = NUM_GX_DRAW_WITH_BBOX_DESCRIPTOR_SETS;

    // The bbox buffer never changes, so we defer descriptor updates until it is enabled.
    if (m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_STORAGE_OR_TEXEL_BUFFER] == VK_NULL_HANDLE)
      m_dirty_flags |= DIRTY_FLAG_PS_SSBO;
  }
  else
  {
    m_pipeline_state.pipeline_layout = g_object_cache->GetPipelineLayout(PIPELINE_LAYOUT_STANDARD);
    m_num_active_descriptor_sets = NUM_GX_DRAW_DESCRIPTOR_SETS;
  }

  m_dirty_flags |= DIRTY_FLAG_PIPELINE | DIRTY_FLAG_DESCRIPTOR_SET_BINDING;
  m_bbox_enabled = enable;
}

void StateTracker::SetBBoxBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
  if (m_bindings.ps_ssbo.buffer == buffer && m_bindings.ps_ssbo.offset == offset &&
      m_bindings.ps_ssbo.range == range)
  {
    return;
  }

  m_bindings.ps_ssbo.buffer = buffer;
  m_bindings.ps_ssbo.offset = offset;
  m_bindings.ps_ssbo.range = range;

  // Defer descriptor update until bbox is actually enabled.
  if (m_bbox_enabled)
    m_dirty_flags |= DIRTY_FLAG_PS_SSBO;
}

void StateTracker::UnbindTexture(VkImageView view)
{
  for (VkDescriptorImageInfo& it : m_bindings.ps_samplers)
  {
    if (it.imageView == view)
      it.imageView = VK_NULL_HANDLE;
  }
}

void StateTracker::InvalidateDescriptorSets()
{
  m_descriptor_sets.fill(VK_NULL_HANDLE);
  m_dirty_flags |= DIRTY_FLAG_ALL_DESCRIPTOR_SETS;

  // Defer SSBO descriptor update until bbox is actually enabled.
  if (!m_bbox_enabled)
    m_dirty_flags &= ~DIRTY_FLAG_PS_SSBO;
}

void StateTracker::InvalidateConstants()
{
  VertexShaderManager::dirty = true;
  GeometryShaderManager::dirty = true;
  PixelShaderManager::dirty = true;
}

void StateTracker::SetPendingRebind()
{
  m_dirty_flags |= DIRTY_FLAG_DYNAMIC_OFFSETS | DIRTY_FLAG_DESCRIPTOR_SET_BINDING |
                   DIRTY_FLAG_PIPELINE_BINDING | DIRTY_FLAG_VERTEX_BUFFER |
                   DIRTY_FLAG_INDEX_BUFFER | DIRTY_FLAG_VIEWPORT | DIRTY_FLAG_SCISSOR |
                   DIRTY_FLAG_PIPELINE;
}

void StateTracker::BeginRenderPass()
{
  if (InRenderPass())
    return;

  m_current_render_pass = m_load_render_pass;
  m_framebuffer_render_area = m_framebuffer_size;

  VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                      nullptr,
                                      m_current_render_pass,
                                      m_framebuffer,
                                      m_framebuffer_render_area,
                                      0,
                                      nullptr};

  vkCmdBeginRenderPass(g_command_buffer_mgr->GetCurrentCommandBuffer(), &begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);
}

void StateTracker::EndRenderPass()
{
  if (!InRenderPass())
    return;

  vkCmdEndRenderPass(g_command_buffer_mgr->GetCurrentCommandBuffer());
  m_current_render_pass = VK_NULL_HANDLE;
}

void StateTracker::BeginClearRenderPass(const VkRect2D& area, const VkClearValue clear_values[2])
{
  _assert_(!InRenderPass());

  m_current_render_pass = m_clear_render_pass;
  m_framebuffer_render_area = area;

  VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                      nullptr,
                                      m_current_render_pass,
                                      m_framebuffer,
                                      m_framebuffer_render_area,
                                      2,
                                      clear_values};

  vkCmdBeginRenderPass(g_command_buffer_mgr->GetCurrentCommandBuffer(), &begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);
}

void StateTracker::SetViewport(const VkViewport& viewport)
{
  if (memcmp(&m_viewport, &viewport, sizeof(viewport)) == 0)
    return;

  m_viewport = viewport;
  m_dirty_flags |= DIRTY_FLAG_VIEWPORT;
}

void StateTracker::SetScissor(const VkRect2D& scissor)
{
  if (memcmp(&m_scissor, &scissor, sizeof(scissor)) == 0)
    return;

  m_scissor = scissor;
  m_dirty_flags |= DIRTY_FLAG_SCISSOR;
}

bool StateTracker::Bind(bool rebind_all /*= false*/)
{
  // Check the render area if we were in a clear pass.
  if (m_current_render_pass == m_clear_render_pass && !IsViewportWithinRenderArea())
    EndRenderPass();

  // Get new pipeline object if any parts have changed
  if (m_dirty_flags & DIRTY_FLAG_PIPELINE && !UpdatePipeline())
  {
    ERROR_LOG(VIDEO, "Failed to get pipeline object, skipping draw");
    return false;
  }

  // Get a new descriptor set if any parts have changed
  if (m_dirty_flags & DIRTY_FLAG_ALL_DESCRIPTOR_SETS && !UpdateDescriptorSet())
  {
    // We can fail to allocate descriptors if we exhaust the pool for this command buffer.
    WARN_LOG(VIDEO, "Failed to get a descriptor set, executing buffer");
    Util::ExecuteCurrentCommandsAndRestoreState(false, false);
    if (!UpdateDescriptorSet())
    {
      // Something strange going on.
      ERROR_LOG(VIDEO, "Failed to get descriptor set, skipping draw");
      return false;
    }
  }

  // Start render pass if not already started
  if (!InRenderPass())
    BeginRenderPass();

  // Re-bind parts of the pipeline
  VkCommandBuffer command_buffer = g_command_buffer_mgr->GetCurrentCommandBuffer();
  if (m_dirty_flags & DIRTY_FLAG_VERTEX_BUFFER || rebind_all)
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_vertex_buffer, &m_vertex_buffer_offset);

  if (m_dirty_flags & DIRTY_FLAG_INDEX_BUFFER || rebind_all)
    vkCmdBindIndexBuffer(command_buffer, m_index_buffer, m_index_buffer_offset, m_index_type);

  if (m_dirty_flags & DIRTY_FLAG_PIPELINE_BINDING || rebind_all)
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_object);

  if (m_dirty_flags & DIRTY_FLAG_DESCRIPTOR_SET_BINDING || rebind_all)
  {
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_state.pipeline_layout, 0, m_num_active_descriptor_sets,
                            m_descriptor_sets.data(), NUM_UBO_DESCRIPTOR_SET_BINDINGS,
                            m_bindings.uniform_buffer_offsets.data());
  }
  else if (m_dirty_flags & DIRTY_FLAG_DYNAMIC_OFFSETS)
  {
    vkCmdBindDescriptorSets(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_state.pipeline_layout,
        DESCRIPTOR_SET_BIND_POINT_UNIFORM_BUFFERS, 1,
        &m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_UNIFORM_BUFFERS],
        NUM_UBO_DESCRIPTOR_SET_BINDINGS, m_bindings.uniform_buffer_offsets.data());
  }

  if (m_dirty_flags & DIRTY_FLAG_VIEWPORT || rebind_all)
    vkCmdSetViewport(command_buffer, 0, 1, &m_viewport);

  if (m_dirty_flags & DIRTY_FLAG_SCISSOR || rebind_all)
    vkCmdSetScissor(command_buffer, 0, 1, &m_scissor);

  m_dirty_flags = 0;
  return true;
}

void StateTracker::OnDraw()
{
  m_draw_counter++;

  // If we didn't have any CPU access last frame, do nothing.
  if (m_scheduled_command_buffer_kicks.empty() || !m_allow_background_execution)
    return;

  // Check if this draw is scheduled to kick a command buffer.
  // The draw counters will always be sorted so a binary search is possible here.
  if (std::binary_search(m_scheduled_command_buffer_kicks.begin(),
                         m_scheduled_command_buffer_kicks.end(), m_draw_counter))
  {
    // Kick a command buffer on the background thread.
    Util::ExecuteCurrentCommandsAndRestoreState(true);
  }
}

void StateTracker::OnReadback()
{
  // Check this isn't another access without any draws inbetween.
  if (!m_cpu_accesses_this_frame.empty() && m_cpu_accesses_this_frame.back() == m_draw_counter)
    return;

  // Store the current draw counter for scheduling in OnEndFrame.
  m_cpu_accesses_this_frame.emplace_back(m_draw_counter);
}

void StateTracker::OnEndFrame()
{
  m_draw_counter = 0;
  m_scheduled_command_buffer_kicks.clear();

  // If we have no CPU access at all, leave everything in the one command buffer for maximum
  // parallelism between CPU/GPU, at the cost of slightly higher latency.
  if (m_cpu_accesses_this_frame.empty())
    return;

  // In order to reduce CPU readback latency, we want to kick a command buffer roughly halfway
  // between the draw counters that invoked the readback, or every 250 draws, whichever is smaller.
  if (g_ActiveConfig.iCommandBufferExecuteInterval > 0)
  {
    u32 last_draw_counter = 0;
    u32 interval = static_cast<u32>(g_ActiveConfig.iCommandBufferExecuteInterval);
    for (u32 draw_counter : m_cpu_accesses_this_frame)
    {
      // We don't want to waste executing command buffers for only a few draws, so set a minimum.
      // Leave last_draw_counter as-is, so we get the correct number of draws between submissions.
      u32 draw_count = draw_counter - last_draw_counter;
      if (draw_count < MINIMUM_DRAW_CALLS_PER_COMMAND_BUFFER_FOR_READBACK)
        continue;

      if (draw_count <= interval)
      {
        u32 mid_point = draw_count / 2;
        m_scheduled_command_buffer_kicks.emplace_back(last_draw_counter + mid_point);
      }
      else
      {
        u32 counter = interval;
        while (counter < draw_count)
        {
          m_scheduled_command_buffer_kicks.emplace_back(last_draw_counter + counter);
          counter += interval;
        }
      }

      last_draw_counter = draw_counter;
    }
  }

#if 0
  {
    std::stringstream ss;
    std::for_each(m_cpu_accesses_this_frame.begin(), m_cpu_accesses_this_frame.end(), [&ss](u32 idx) { ss << idx << ","; });
    WARN_LOG(VIDEO, "CPU EFB accesses in last frame: %s", ss.str().c_str());
  }
  {
    std::stringstream ss;
    std::for_each(m_scheduled_command_buffer_kicks.begin(), m_scheduled_command_buffer_kicks.end(), [&ss](u32 idx) { ss << idx << ","; });
    WARN_LOG(VIDEO, "Scheduled command buffer kicks: %s", ss.str().c_str());
  }
#endif

  m_cpu_accesses_this_frame.clear();
}

void StateTracker::SetBackgroundCommandBufferExecution(bool enabled)
{
  m_allow_background_execution = enabled;
}

bool StateTracker::IsWithinRenderArea(s32 x, s32 y, u32 width, u32 height) const
{
  // Check that the viewport does not lie outside the render area.
  // If it does, we need to switch to a normal load/store render pass.
  s32 left = m_framebuffer_render_area.offset.x;
  s32 top = m_framebuffer_render_area.offset.y;
  s32 right = left + static_cast<s32>(m_framebuffer_render_area.extent.width);
  s32 bottom = top + static_cast<s32>(m_framebuffer_render_area.extent.height);
  s32 test_left = x;
  s32 test_top = y;
  s32 test_right = test_left + static_cast<s32>(width);
  s32 test_bottom = test_top + static_cast<s32>(height);
  return test_left >= left && test_right <= right && test_top >= top && test_bottom <= bottom;
}

bool StateTracker::IsViewportWithinRenderArea() const
{
  return IsWithinRenderArea(static_cast<s32>(m_viewport.x), static_cast<s32>(m_viewport.y),
                            static_cast<u32>(m_viewport.width),
                            static_cast<u32>(m_viewport.height));
}

void StateTracker::EndClearRenderPass()
{
  if (m_current_render_pass != m_clear_render_pass)
    return;

  // End clear render pass. Bind() will call BeginRenderPass() which
  // will switch to the load/store render pass.
  EndRenderPass();
}

PipelineInfo StateTracker::GetAlphaPassPipelineConfig(const PipelineInfo& info) const  //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass
{
  PipelineInfo temp_info = info;

  // Skip depth writes for this pass. The results will be the same, so no
  // point in overwriting depth values with the same value.
  temp_info.depth_stencil_state.write_enable = VK_FALSE;

  // Only allow alpha writes, and disable blending.
//  temp_info.blend_state.blend_enable = VK_FALSE;
//  temp_info.blend_state.logic_op_enable = VK_FALSE;
//  temp_info.blend_state.write_mask = VK_COLOR_COMPONENT_A_BIT;
  temp_info.blend_state.blendenable = true; //gvx update state attributes as per changes in 5.0-3404
  temp_info.blend_state.logicopenable = false;  //gvx update state attributes as per changes in 5.0-3404
  temp_info.blend_state.colorupdate = false;  //gvx update state attributes as per changes in 5.0-3404
  temp_info.blend_state.alphaupdate = true;  //gvx update state attributes as per changes in 5.0-3404, alpha_enable <==> write_mask = VK_COLOR_COMPONENT_A_BIT with colorupdate set to false

  return temp_info;
}  //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass

VkPipeline StateTracker::GetPipelineAndCacheUID(const PipelineInfo& info)
{
  auto result = g_object_cache->GetPipelineWithCacheResult(info);

  // Add to the UID cache if it is a new pipeline.
  if (!result.second)
    AppendToPipelineUIDCache(info);

  return result.first;
}

bool StateTracker::UpdatePipeline()
{
  // We need at least a vertex and fragment shader
  if (m_pipeline_state.vs == VK_NULL_HANDLE || m_pipeline_state.ps == VK_NULL_HANDLE)
    return false;

  // Grab a new pipeline object, this can fail.
//gvx64  m_pipeline_object = GetPipelineAndCacheUID(m_pipeline_state);
  // We have to use a different blend state for the alpha pass of the dstalpha fallback.
  if (m_dstalpha_mode == DSTALPHA_ALPHA_PASS)  //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass
  {
    // We need to retain the existing state, since we don't want to break the next draw.
    PipelineInfo temp_info = GetAlphaPassPipelineConfig(m_pipeline_state);
    m_pipeline_object = GetPipelineAndCacheUID(temp_info);
  }
  else
  {
    m_pipeline_object = GetPipelineAndCacheUID(m_pipeline_state);
  }  //gvx64 rollback to 5.0-1651 - reintroduce Vulkan alpha pass

  m_dirty_flags |= DIRTY_FLAG_PIPELINE_BINDING;
  return m_pipeline_object != VK_NULL_HANDLE;
}

bool StateTracker::UpdateDescriptorSet()
{
  const size_t MAX_DESCRIPTOR_WRITES = NUM_UBO_DESCRIPTOR_SET_BINDINGS +  // UBO
                                       NUM_PIXEL_SHADER_SAMPLERS +        // Samplers
                                       1;                                 // SSBO
  std::array<VkWriteDescriptorSet, MAX_DESCRIPTOR_WRITES> writes;
  u32 num_writes = 0;

  if (m_dirty_flags & (DIRTY_FLAG_VS_UBO | DIRTY_FLAG_GS_UBO | DIRTY_FLAG_PS_UBO) ||
      m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_UNIFORM_BUFFERS] == VK_NULL_HANDLE)
  {
    VkDescriptorSetLayout layout =
        g_object_cache->GetDescriptorSetLayout(DESCRIPTOR_SET_LAYOUT_UNIFORM_BUFFERS);
    VkDescriptorSet set = g_command_buffer_mgr->AllocateDescriptorSet(layout);
    if (set == VK_NULL_HANDLE)
      return false;

    for (size_t i = 0; i < NUM_UBO_DESCRIPTOR_SET_BINDINGS; i++)
    {
      writes[num_writes++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              nullptr,
                              set,
                              static_cast<uint32_t>(i),
                              0,
                              1,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                              nullptr,
                              &m_bindings.uniform_buffer_bindings[i],
                              nullptr};
    }

    m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_UNIFORM_BUFFERS] = set;
    m_dirty_flags |= DIRTY_FLAG_DESCRIPTOR_SET_BINDING;
  }

  if (m_dirty_flags & DIRTY_FLAG_PS_SAMPLERS ||
      m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_PIXEL_SHADER_SAMPLERS] == VK_NULL_HANDLE)
  {
    VkDescriptorSetLayout layout =
        g_object_cache->GetDescriptorSetLayout(DESCRIPTOR_SET_LAYOUT_PIXEL_SHADER_SAMPLERS);
    VkDescriptorSet set = g_command_buffer_mgr->AllocateDescriptorSet(layout);
    if (set == VK_NULL_HANDLE)
      return false;

    for (size_t i = 0; i < NUM_PIXEL_SHADER_SAMPLERS; i++)
    {
      const VkDescriptorImageInfo& info = m_bindings.ps_samplers[i];
      if (info.imageView != VK_NULL_HANDLE && info.sampler != VK_NULL_HANDLE)
      {
        writes[num_writes++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                nullptr,
                                set,
                                static_cast<uint32_t>(i),
                                0,
                                1,
                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                &info,
                                nullptr,
                                nullptr};
      }
    }

    m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_PIXEL_SHADER_SAMPLERS] = set;
    m_dirty_flags |= DIRTY_FLAG_DESCRIPTOR_SET_BINDING;
  }

  if (m_bbox_enabled &&
      (m_dirty_flags & DIRTY_FLAG_PS_SSBO ||
       m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_STORAGE_OR_TEXEL_BUFFER] == VK_NULL_HANDLE))
  {
    VkDescriptorSetLayout layout =
        g_object_cache->GetDescriptorSetLayout(DESCRIPTOR_SET_LAYOUT_SHADER_STORAGE_BUFFERS);
    VkDescriptorSet set = g_command_buffer_mgr->AllocateDescriptorSet(layout);
    if (set == VK_NULL_HANDLE)
      return false;

    writes[num_writes++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            nullptr,
                            set,
                            0,
                            0,
                            1,
                            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            nullptr,
                            &m_bindings.ps_ssbo,
                            nullptr};

    m_descriptor_sets[DESCRIPTOR_SET_BIND_POINT_STORAGE_OR_TEXEL_BUFFER] = set;
    m_dirty_flags |= DIRTY_FLAG_DESCRIPTOR_SET_BINDING;
  }

  if (num_writes > 0)
    vkUpdateDescriptorSets(g_vulkan_context->GetDevice(), num_writes, writes.data(), 0, nullptr);

  return true;
}

}  // namespace Vulkan
