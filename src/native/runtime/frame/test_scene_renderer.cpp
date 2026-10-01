#include "frame/test_scene_renderer.h"

#include "frame/stereo_math.h"

// Embedded SPIR-V (generated at build time by glslang + cmake/embed_spirv.cmake).
#include "shaders/test_scene_shaders.h"
#include "shaders/overlay_shaders.h"

#include <cstring>
#include <iterator>

namespace vrclient::runtime::frame {
namespace {

struct Vertex {
  float position[3];
  float color[3];
};

// std140-compatible: mat4 (64 bytes) + vec4 (16 bytes).
struct SceneConstants {
  float view_projection[16];
  float tint[4];
};

constexpr Vertex kCubeVertices[] = {
    {{-0.35f, -0.35f, -1.25f}, {1.0f, 0.2f, 0.2f}},
    {{-0.35f, 0.35f, -1.25f}, {0.2f, 1.0f, 0.2f}},
    {{0.35f, 0.35f, -1.25f}, {0.2f, 0.3f, 1.0f}},
    {{0.35f, -0.35f, -1.25f}, {1.0f, 1.0f, 0.2f}},
    {{-0.35f, -0.35f, -1.95f}, {0.2f, 1.0f, 1.0f}},
    {{-0.35f, 0.35f, -1.95f}, {1.0f, 0.2f, 1.0f}},
    {{0.35f, 0.35f, -1.95f}, {0.8f, 0.8f, 1.0f}},
    {{0.35f, -0.35f, -1.95f}, {1.0f, 0.8f, 0.3f}},
};

constexpr uint16_t kCubeIndices[] = {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7,
};

// Per-image uniform stride, rounded to a safe 256B alignment so each image's
// SceneConstants live in their own region (avoids host-writes racing in-flight
// reads even if two images are submitted before a wait).
constexpr VkDeviceSize kUniformStride = 256;

// ---- Overlay (DIAG-03 partial) -------------------------------------------
// Position-only vertex for the overlay quad (a unit square in [0,1]^2; the vert
// shader maps it into the destination NDC rect carried in the push constant).
struct OverlayVertex {
  float pos[2];
};

// Two triangles covering the unit square [0,1]x[0,1].
constexpr OverlayVertex kOverlayQuad[6] = {
    {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{1.0f, 1.0f}},
    {{0.0f, 0.0f}}, {{1.0f, 1.0f}}, {{0.0f, 1.0f}},
};

// Push constant shared by the vertex (rect) and fragment (color) stages. Must
// stay <= 128 bytes and match the VkPushConstantRange below; 32 bytes here.
struct OverlayPush {
  float color[4];  // RGBA, straight alpha
  float rect[4];   // (x0, y0, x1, y1) in Vulkan NDC, Y-down
};

// Overlay placement (Vulkan NDC, Y-down; -1 is top, +1 is bottom). Top-left
// corner: a wider status bar plus a small square error indicator beside it.
constexpr float kStatusRect[4] = {-0.95f, -0.95f, -0.55f, -0.88f};
constexpr float kErrorRect[4] = {-0.52f, -0.95f, -0.45f, -0.88f};

// Error-indicator colors (opaque): green = no recent warnings, red = warnings.
constexpr float kErrorOk[4] = {0.10f, 0.80f, 0.20f, 1.0f};
constexpr float kErrorRed[4] = {0.90f, 0.12f, 0.12f, 1.0f};

// Status-bar color as a pure function of the OpenXR/runtime state. Opaque
// (a=1.0) so the block is solid and validation-clean. DEGRADED is mapped to
// blue (the prompt requested no specific color for it; documented choice).
void statusColorFor(VrRuntimeState state, float out_rgba[4]) {
  float r = 0.5f, g = 0.5f, b = 0.5f;  // default: STOPPED -> gray
  switch (state) {
    case VR_RUNTIME_STATE_RUNNING:
      r = 0.10f; g = 0.80f; b = 0.20f;  // green
      break;
    case VR_RUNTIME_STATE_READY:
    case VR_RUNTIME_STATE_INITIALIZING:
      r = 0.95f; g = 0.65f; b = 0.10f;  // amber
      break;
    case VR_RUNTIME_STATE_DEGRADED:
      r = 0.20f; g = 0.45f; b = 0.95f;  // blue (documented: DEGRADED)
      break;
    case VR_RUNTIME_STATE_LOSS_PENDING:
    case VR_RUNTIME_STATE_EXITING:
    case VR_RUNTIME_STATE_ERROR:
      r = 0.90f; g = 0.12f; b = 0.12f;  // red
      break;
    case VR_RUNTIME_STATE_STOPPED:
    default:
      break;  // gray
  }
  out_rgba[0] = r;
  out_rgba[1] = g;
  out_rgba[2] = b;
  out_rgba[3] = 1.0f;
}

bool vkOk(VkResult r) {
  return r == VK_SUCCESS;
}

}  // namespace

VulkanTestSceneRenderer::~VulkanTestSceneRenderer() {
  destroy();
}

bool VulkanTestSceneRenderer::findMemoryType(
    uint32_t type_filter,
    VkMemoryPropertyFlags properties,
    uint32_t* out_index) const {
  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) != 0 &&
        (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
      *out_index = i;
      return true;
    }
  }
  return false;
}

bool VulkanTestSceneRenderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer* out_buffer,
    VkDeviceMemory* out_memory) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!vkOk(vkCreateBuffer(device_, &buffer_info, nullptr, out_buffer))) {
    return false;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, *out_buffer, &requirements);

  uint32_t memory_type = 0;
  if (!findMemoryType(requirements.memoryTypeBits, properties, &memory_type)) {
    return false;
  }

  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = memory_type;
  if (!vkOk(vkAllocateMemory(device_, &alloc_info, nullptr, out_memory))) {
    return false;
  }
  return vkOk(vkBindBufferMemory(device_, *out_buffer, *out_memory, 0));
}

bool VulkanTestSceneRenderer::uploadViaStaging(
    VkBuffer dst, const void* data, VkDeviceSize size) {
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  if (!createBuffer(
          size,
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &staging,
          &staging_memory)) {
    return false;
  }

  void* mapped = nullptr;
  bool ok = vkOk(vkMapMemory(device_, staging_memory, 0, size, 0, &mapped));
  if (ok) {
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device_, staging_memory);
  }

  VkCommandBufferAllocateInfo cmd_alloc{};
  cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_alloc.commandPool = command_pool_;
  cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_alloc.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (ok) {
    ok = vkOk(vkAllocateCommandBuffers(device_, &cmd_alloc, &cmd));
  }

  if (ok) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ok = vkOk(vkBeginCommandBuffer(cmd, &begin));
  }
  if (ok) {
    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging, dst, 1, &copy);
    ok = vkOk(vkEndCommandBuffer(cmd));
  }
  if (ok) {
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    ok = vkOk(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE));
  }
  if (ok) {
    ok = vkOk(vkQueueWaitIdle(queue_));
  }

  if (cmd != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
  }
  vkDestroyBuffer(device_, staging, nullptr);
  vkFreeMemory(device_, staging_memory, nullptr);
  return ok;
}

bool VulkanTestSceneRenderer::initialize(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    uint32_t queue_family_index,
    VkQueue queue) {
  if (device == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE ||
      queue == VK_NULL_HANDLE) {
    return false;
  }
  instance_ = instance;
  physical_device_ = physical_device;
  device_ = device;
  queue_family_index_ = queue_family_index;
  queue_ = queue;

  // Command pool (RESET_COMMAND_BUFFER so per-image command buffers can be reset
  // and re-recorded each frame without reallocation).
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = queue_family_index_;
  if (!vkOk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_))) {
    return false;
  }

  // Shader modules from embedded SPIR-V.
  VkShaderModuleCreateInfo vert_info{};
  vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  vert_info.codeSize = kTestSceneVertSpv_size;
  vert_info.pCode = kTestSceneVertSpv;
  if (!vkOk(vkCreateShaderModule(device_, &vert_info, nullptr, &vertex_module_))) {
    return false;
  }
  VkShaderModuleCreateInfo frag_info{};
  frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  frag_info.codeSize = kTestSceneFragSpv_size;
  frag_info.pCode = kTestSceneFragSpv;
  if (!vkOk(vkCreateShaderModule(device_, &frag_info, nullptr, &fragment_module_))) {
    return false;
  }

  // Descriptor set layout: one uniform buffer at binding 0 (vertex stage).
  VkDescriptorSetLayoutBinding ubo_binding{};
  ubo_binding.binding = 0;
  ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  ubo_binding.descriptorCount = 1;
  ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &ubo_binding;
  if (!vkOk(vkCreateDescriptorSetLayout(
          device_, &layout_info, nullptr, &descriptor_set_layout_))) {
    return false;
  }

  // One descriptor set per eye. FREE_DESCRIPTOR_SET_BIT so destroyEye() can free
  // its set on a swapchain rebuild (dynamic resolution / format change); without
  // it the pool's 2 sets would be exhausted on the first re-prepare and any second
  // prepareEyeTargets() would hit VK_ERROR_OUT_OF_POOL_MEMORY.
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  pool_size.descriptorCount = 2;
  VkDescriptorPoolCreateInfo desc_pool_info{};
  desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  desc_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  desc_pool_info.maxSets = 2;
  desc_pool_info.poolSizeCount = 1;
  desc_pool_info.pPoolSizes = &pool_size;
  if (!vkOk(vkCreateDescriptorPool(
          device_, &desc_pool_info, nullptr, &descriptor_pool_))) {
    return false;
  }

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
  if (!vkOk(vkCreatePipelineLayout(
          device_, &pipeline_layout_info, nullptr, &pipeline_layout_))) {
    return false;
  }

  // Device-local vertex + index buffers, uploaded via staging.
  if (!createBuffer(
          sizeof(kCubeVertices),
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          &vertex_buffer_,
          &vertex_memory_)) {
    return false;
  }
  if (!uploadViaStaging(vertex_buffer_, kCubeVertices, sizeof(kCubeVertices))) {
    return false;
  }
  if (!createBuffer(
          sizeof(kCubeIndices),
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          &index_buffer_,
          &index_memory_)) {
    return false;
  }
  if (!uploadViaStaging(index_buffer_, kCubeIndices, sizeof(kCubeIndices))) {
    return false;
  }
  index_count_ = static_cast<uint32_t>(std::size(kCubeIndices));

  // ---- Overlay (DIAG-03 partial): format/extent-independent resources.
  // Shader modules from embedded SPIR-V (overlay.vert/.frag).
  VkShaderModuleCreateInfo overlay_vert_info{};
  overlay_vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  overlay_vert_info.codeSize = kOverlayVertSpv_size;
  overlay_vert_info.pCode = kOverlayVertSpv;
  if (!vkOk(vkCreateShaderModule(
          device_, &overlay_vert_info, nullptr, &overlay_vertex_module_))) {
    return false;
  }
  VkShaderModuleCreateInfo overlay_frag_info{};
  overlay_frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  overlay_frag_info.codeSize = kOverlayFragSpv_size;
  overlay_frag_info.pCode = kOverlayFragSpv;
  if (!vkOk(vkCreateShaderModule(
          device_, &overlay_frag_info, nullptr, &overlay_fragment_module_))) {
    return false;
  }

  // Pipeline layout: a SINGLE push-constant range (no descriptor sets). The
  // vert stage reads pc.rect and the frag stage reads pc.color, so the range
  // MUST cover both stages or the validation layer flags a stage mismatch at
  // vkCmdPushConstants.
  VkPushConstantRange overlay_push_range{};
  overlay_push_range.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  overlay_push_range.offset = 0;
  overlay_push_range.size = sizeof(OverlayPush);
  VkPipelineLayoutCreateInfo overlay_layout_info{};
  overlay_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  overlay_layout_info.setLayoutCount = 0;
  overlay_layout_info.pushConstantRangeCount = 1;
  overlay_layout_info.pPushConstantRanges = &overlay_push_range;
  if (!vkOk(vkCreatePipelineLayout(
          device_, &overlay_layout_info, nullptr, &overlay_pipeline_layout_))) {
    return false;
  }

  // Static device-local NDC quad (unit square), uploaded via staging.
  if (!createBuffer(
          sizeof(kOverlayQuad),
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          &overlay_vertex_buffer_,
          &overlay_vertex_memory_)) {
    return false;
  }
  if (!uploadViaStaging(
          overlay_vertex_buffer_, kOverlayQuad, sizeof(kOverlayQuad))) {
    return false;
  }

  base_initialized_ = true;
  return true;
}

bool VulkanTestSceneRenderer::ensureRenderPass(VkFormat color_format) {
  if (render_pass_ != VK_NULL_HANDLE) {
    return color_format == color_format_;
  }
  color_format_ = color_format;

  VkAttachmentDescription color_attachment{};
  color_attachment.format = color_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // The OpenXR Vulkan runtime expects COLOR_ATTACHMENT_OPTIMAL on release; the
  // headless validation path also reads it back from that layout.
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depth_attachment{};
  depth_attachment.format = depth_format_;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_ref{};
  depth_ref.attachment = 1;
  depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  // External dependencies so the validation layer is satisfied about the
  // UNDEFINED->attachment transitions at both ends.
  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  deps[0].srcAccessMask = 0;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = 0;

  const VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};
  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 2;
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 2;
  render_pass_info.pDependencies = deps;
  return vkOk(vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_));
}

bool VulkanTestSceneRenderer::ensurePipeline() {
  if (pipeline_ != VK_NULL_HANDLE) {
    return true;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_module_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment_module_;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0].location = 0;
  attributes[0].binding = 0;
  attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset = offsetof(Vertex, position);
  attributes[1].location = 1;
  attributes[1].binding = 0;
  attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_BACK_BIT;
  // The vert shader negates clip Y to match Vulkan's Y-down convention, which
  // flips triangle winding vs the original D3D11 (CW) front face; declare CW as
  // front so back-face culling keeps the same faces visible.
  raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blend_attachment.blendEnable = VK_FALSE;
  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  const VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  return vkOk(vkCreateGraphicsPipelines(
      device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_));
}

bool VulkanTestSceneRenderer::ensureOverlayPipeline() {
  if (overlay_pipeline_ != VK_NULL_HANDLE) {
    return true;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = overlay_vertex_module_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = overlay_fragment_module_;
  stages[1].pName = "main";

  // Position-only vertex (vec2 in [0,1]^2).
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(OverlayVertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attribute{};
  attribute.location = 0;
  attribute.binding = 0;
  attribute.format = VK_FORMAT_R32G32_SFLOAT;
  attribute.offset = offsetof(OverlayVertex, pos);

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 1;
  vertex_input.pVertexAttributeDescriptions = &attribute;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // No culling: the 2D quad is authored CCW in NDC, but disabling culling avoids
  // any winding pitfalls for a screen-space overlay.
  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Depth test + write OFF so the overlay always draws on top of the cube and
  // never corrupts the depth buffer.
  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_FALSE;
  depth_stencil.depthWriteEnable = VK_FALSE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

  // Straight-alpha blend over the scene.
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blend_attachment.blendEnable = VK_TRUE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  const VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = overlay_pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  return vkOk(vkCreateGraphicsPipelines(
      device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &overlay_pipeline_));
}

bool VulkanTestSceneRenderer::prepareEyeTargets(
    VrRuntimeEye eye,
    const VkImage* images,
    uint32_t image_count,
    VkFormat color_format,
    uint32_t width,
    uint32_t height) {
  if (!base_initialized_ || images == nullptr || image_count == 0 ||
      width == 0 || height == 0) {
    return false;
  }

  if (!ensureRenderPass(color_format) || !ensurePipeline() ||
      !ensureOverlayPipeline()) {
    return false;
  }

  EyeResources& resources = eye_resources_[static_cast<size_t>(eye)];
  destroyEye(resources);
  resources.width = width;
  resources.height = height;

  // Per-image color view + per-image depth + framebuffer + command buffer + fence.
  resources.image_views.resize(image_count, VK_NULL_HANDLE);
  resources.framebuffers.resize(image_count, VK_NULL_HANDLE);
  resources.command_buffers.resize(image_count, VK_NULL_HANDLE);
  resources.fences.resize(image_count, VK_NULL_HANDLE);
  resources.depth_images.resize(image_count, VK_NULL_HANDLE);
  resources.depth_memories.resize(image_count, VK_NULL_HANDLE);
  resources.depth_views.resize(image_count, VK_NULL_HANDLE);

  for (uint32_t i = 0; i < image_count; ++i) {
    // Dedicated depth image + memory + view for THIS swapchain image. One per
    // image (not shared per-eye) so concurrent in-flight frames on different
    // image indices never write the same depth attachment (no WAW race).
    VkImageCreateInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_info.imageType = VK_IMAGE_TYPE_2D;
    depth_info.format = depth_format_;
    depth_info.extent = {width, height, 1};
    depth_info.mipLevels = 1;
    depth_info.arrayLayers = 1;
    depth_info.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vkOk(vkCreateImage(
            device_, &depth_info, nullptr, &resources.depth_images[i]))) {
      return false;
    }
    VkMemoryRequirements depth_req{};
    vkGetImageMemoryRequirements(device_, resources.depth_images[i], &depth_req);
    uint32_t depth_mem_type = 0;
    if (!findMemoryType(
            depth_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &depth_mem_type)) {
      return false;
    }
    VkMemoryAllocateInfo depth_alloc{};
    depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depth_alloc.allocationSize = depth_req.size;
    depth_alloc.memoryTypeIndex = depth_mem_type;
    if (!vkOk(vkAllocateMemory(
            device_, &depth_alloc, nullptr, &resources.depth_memories[i])) ||
        !vkOk(vkBindImageMemory(
            device_, resources.depth_images[i], resources.depth_memories[i], 0))) {
      return false;
    }
    VkImageViewCreateInfo depth_view_info{};
    depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_view_info.image = resources.depth_images[i];
    depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_info.format = depth_format_;
    depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_info.subresourceRange.levelCount = 1;
    depth_view_info.subresourceRange.layerCount = 1;
    if (!vkOk(vkCreateImageView(
            device_, &depth_view_info, nullptr, &resources.depth_views[i]))) {
      return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = images[i];
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = color_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (!vkOk(vkCreateImageView(
            device_, &view_info, nullptr, &resources.image_views[i]))) {
      return false;
    }

    const VkImageView attachments[] = {resources.image_views[i], resources.depth_views[i]};
    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass_;
    fb_info.attachmentCount = 2;
    fb_info.pAttachments = attachments;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;
    if (!vkOk(vkCreateFramebuffer(
            device_, &fb_info, nullptr, &resources.framebuffers[i]))) {
      return false;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!vkOk(vkCreateFence(device_, &fence_info, nullptr, &resources.fences[i]))) {
      return false;
    }
  }

  // Pre-allocate all command buffers in one call (no per-frame allocation).
  VkCommandBufferAllocateInfo cmd_alloc{};
  cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_alloc.commandPool = command_pool_;
  cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_alloc.commandBufferCount = image_count;
  if (!vkOk(vkAllocateCommandBuffers(
          device_, &cmd_alloc, resources.command_buffers.data()))) {
    return false;
  }

  // Host-visible uniform buffer: one kUniformStride region per image, persistently
  // mapped so the hot path only memcpy's (no map/unmap, no alloc).
  const VkDeviceSize uniform_size = kUniformStride * image_count;
  if (!createBuffer(
          uniform_size,
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &resources.uniform_buffer,
          &resources.uniform_memory)) {
    return false;
  }
  if (!vkOk(vkMapMemory(
          device_, resources.uniform_memory, 0, uniform_size, 0,
          &resources.uniform_mapped))) {
    return false;
  }

  // Descriptor set: a DYNAMIC uniform buffer. One descriptor over the whole
  // per-eye buffer; renderEye selects this image's kUniformStride region via a
  // dynamic offset, so multiple images can be in flight without a write hazard.
  VkDescriptorSetAllocateInfo desc_alloc{};
  desc_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  desc_alloc.descriptorPool = descriptor_pool_;
  desc_alloc.descriptorSetCount = 1;
  desc_alloc.pSetLayouts = &descriptor_set_layout_;
  if (!vkOk(vkAllocateDescriptorSets(
          device_, &desc_alloc, &resources.descriptor_set))) {
    return false;
  }
  VkDescriptorBufferInfo buffer_info{};
  buffer_info.buffer = resources.uniform_buffer;
  buffer_info.offset = 0;
  buffer_info.range = sizeof(SceneConstants);
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = resources.descriptor_set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  write.pBufferInfo = &buffer_info;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

  resources.initialized = true;
  return true;
}

void VulkanTestSceneRenderer::renderEye(
    const VrRuntimeFrameData& frame_data,
    VrRuntimeEye eye,
    uint32_t image_index,
    VkImage image) {
  (void)image;
  const size_t eye_index = static_cast<size_t>(eye);
  EyeResources& resources = eye_resources_[eye_index];
  if (!resources.initialized ||
      image_index >= resources.command_buffers.size()) {
    return;
  }

  // Compute the per-eye constants (CPU math; not part of the recorded hot path).
  const VrRuntimeEyeView& eye_view = frame_data.eyes[eye_index];
  // Column-vector convention: viewProjection = P * V (projection THEN view),
  // applied directly in the shader as clip = viewProjection * vec4(pos,1).
  const VrRuntimeMatrix4 view_projection =
      multiply(eye_view.projection, eye_view.view);
  SceneConstants constants{};
  for (size_t i = 0; i < std::size(constants.view_projection); ++i) {
    constants.view_projection[i] = view_projection.m[i];
  }
  constants.tint[0] = eye == VR_RUNTIME_EYE_LEFT ? 1.0f : 0.75f;
  constants.tint[1] = 0.95f;
  constants.tint[2] = eye == VR_RUNTIME_EYE_RIGHT ? 1.0f : 0.75f;
  constants.tint[3] = 1.0f;

  const float clear_left[] = {0.02f, 0.04f, 0.09f, 1.0f};
  const float clear_right[] = {0.04f, 0.025f, 0.07f, 1.0f};
  const float* clear_rgba = eye == VR_RUNTIME_EYE_LEFT ? clear_left : clear_right;

  // Overlay (DIAG-03 partial) push-constant data, computed on the CPU here —
  // BEFORE the HOT PATH markers — so the recorded hot path only does push + draw.
  // statusColorFor is a pure function of the runtime state carried in frame_data;
  // overlay_has_warning_ is a plain bool set by the runtime outside the hot path.
  OverlayPush status_push{};
  statusColorFor(frame_data.runtime_state, status_push.color);
  std::memcpy(status_push.rect, kStatusRect, sizeof(status_push.rect));
  OverlayPush error_push{};
  const float* error_color = overlay_has_warning_ ? kErrorRed : kErrorOk;
  std::memcpy(error_push.color, error_color, sizeof(error_push.color));
  std::memcpy(error_push.rect, kErrorRect, sizeof(error_push.rect));

  VkCommandBuffer cmd = resources.command_buffers[image_index];
  VkFence fence = resources.fences[image_index];

  // Ensure the previous submission using THIS command buffer + uniform region has
  // completed before we reset/rewrite them (correctness + validation cleanliness).
  vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(device_, 1, &fence);

  // Write this image's uniform region (persistently mapped; HOST_COHERENT).
  std::memcpy(
      static_cast<char*>(resources.uniform_mapped) + kUniformStride * image_index,
      &constants,
      sizeof(constants));

  // HOT PATH BEGIN: Vulkan stereo test-scene rendering (record + submit).
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);

  VkClearValue clears[2]{};
  clears[0].color = {{clear_rgba[0], clear_rgba[1], clear_rgba[2], clear_rgba[3]}};
  clears[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo pass_begin{};
  pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  pass_begin.renderPass = render_pass_;
  pass_begin.framebuffer = resources.framebuffers[image_index];
  pass_begin.renderArea.offset = {0, 0};
  pass_begin.renderArea.extent = {resources.width, resources.height};
  pass_begin.clearValueCount = 2;
  pass_begin.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &pass_begin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(resources.width);
  viewport.height = static_cast<float>(resources.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {resources.width, resources.height};
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  const VkDeviceSize vertex_offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, &vertex_offset);
  vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT16);
  const uint32_t dynamic_offset =
      static_cast<uint32_t>(kUniformStride * image_index);
  vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
      &resources.descriptor_set, 1, &dynamic_offset);
  vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);

  // Overlay (DIAG-03 partial): drawn AFTER the cube, same render pass/subpass,
  // depth-test OFF + alpha blend so it sits on top. Push-constant write + draw
  // only (no allocation, no logging, no locks) -> hot-path-audit clean. The
  // status/error push structs were computed on the CPU above the markers.
  if (overlay_enabled_) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    vkCmdBindVertexBuffers(cmd, 0, 1, &overlay_vertex_buffer_, &vertex_offset);
    vkCmdPushConstants(
        cmd, overlay_pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(status_push), &status_push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdPushConstants(
        cmd, overlay_pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(error_push), &error_push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(queue_, 1, &submit, fence);
  // HOT PATH END.
}

void VulkanTestSceneRenderer::waitIdle() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
}

void VulkanTestSceneRenderer::destroyEye(EyeResources& eye) {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  if (!eye.command_buffers.empty()) {
    vkFreeCommandBuffers(
        device_, command_pool_,
        static_cast<uint32_t>(eye.command_buffers.size()),
        eye.command_buffers.data());
    eye.command_buffers.clear();
  }
  for (VkFence fence : eye.fences) {
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(device_, fence, nullptr);
    }
  }
  eye.fences.clear();
  for (VkFramebuffer fb : eye.framebuffers) {
    if (fb != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, fb, nullptr);
    }
  }
  eye.framebuffers.clear();
  for (VkImageView view : eye.image_views) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, view, nullptr);
    }
  }
  eye.image_views.clear();
  for (VkImageView view : eye.depth_views) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, view, nullptr);
    }
  }
  eye.depth_views.clear();
  for (VkImage image : eye.depth_images) {
    if (image != VK_NULL_HANDLE) {
      vkDestroyImage(device_, image, nullptr);
    }
  }
  eye.depth_images.clear();
  for (VkDeviceMemory memory : eye.depth_memories) {
    if (memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, memory, nullptr);
    }
  }
  eye.depth_memories.clear();
  if (eye.uniform_mapped != nullptr && eye.uniform_memory != VK_NULL_HANDLE) {
    vkUnmapMemory(device_, eye.uniform_memory);
    eye.uniform_mapped = nullptr;
  }
  if (eye.uniform_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, eye.uniform_buffer, nullptr);
    eye.uniform_buffer = VK_NULL_HANDLE;
  }
  if (eye.uniform_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, eye.uniform_memory, nullptr);
    eye.uniform_memory = VK_NULL_HANDLE;
  }
  // Free the descriptor set back to the pool (pool created with
  // FREE_DESCRIPTOR_SET_BIT) so a swapchain rebuild can re-allocate without
  // exhausting the pool. Pool-wide cleanup still happens in destroy().
  if (eye.descriptor_set != VK_NULL_HANDLE &&
      descriptor_pool_ != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_, descriptor_pool_, 1, &eye.descriptor_set);
  }
  eye.descriptor_set = VK_NULL_HANDLE;
  eye.width = 0;
  eye.height = 0;
  eye.initialized = false;
}

void VulkanTestSceneRenderer::destroy() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  vkDeviceWaitIdle(device_);

  for (EyeResources& eye : eye_resources_) {
    destroyEye(eye);
  }

  if (pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
  if (overlay_pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, overlay_pipeline_, nullptr);
    overlay_pipeline_ = VK_NULL_HANDLE;
  }
  if (render_pass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_, render_pass_, nullptr);
    render_pass_ = VK_NULL_HANDLE;
  }
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
  if (overlay_pipeline_layout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, overlay_pipeline_layout_, nullptr);
    overlay_pipeline_layout_ = VK_NULL_HANDLE;
  }
  if (descriptor_pool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    descriptor_pool_ = VK_NULL_HANDLE;
  }
  if (descriptor_set_layout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    descriptor_set_layout_ = VK_NULL_HANDLE;
  }
  if (vertex_buffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vertex_buffer_ = VK_NULL_HANDLE;
  }
  if (vertex_memory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, vertex_memory_, nullptr);
    vertex_memory_ = VK_NULL_HANDLE;
  }
  if (index_buffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, index_buffer_, nullptr);
    index_buffer_ = VK_NULL_HANDLE;
  }
  if (index_memory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, index_memory_, nullptr);
    index_memory_ = VK_NULL_HANDLE;
  }
  if (overlay_vertex_buffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, overlay_vertex_buffer_, nullptr);
    overlay_vertex_buffer_ = VK_NULL_HANDLE;
  }
  if (overlay_vertex_memory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, overlay_vertex_memory_, nullptr);
    overlay_vertex_memory_ = VK_NULL_HANDLE;
  }
  if (vertex_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, vertex_module_, nullptr);
    vertex_module_ = VK_NULL_HANDLE;
  }
  if (fragment_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, fragment_module_, nullptr);
    fragment_module_ = VK_NULL_HANDLE;
  }
  if (overlay_vertex_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, overlay_vertex_module_, nullptr);
    overlay_vertex_module_ = VK_NULL_HANDLE;
  }
  if (overlay_fragment_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, overlay_fragment_module_, nullptr);
    overlay_fragment_module_ = VK_NULL_HANDLE;
  }
  if (command_pool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, command_pool_, nullptr);
    command_pool_ = VK_NULL_HANDLE;
  }

  // The VkDevice/VkInstance are owned by VulkanDeviceContext, not this renderer.
  device_ = VK_NULL_HANDLE;
  physical_device_ = VK_NULL_HANDLE;
  instance_ = VK_NULL_HANDLE;
  queue_ = VK_NULL_HANDLE;
  base_initialized_ = false;
}

}  // namespace vrclient::runtime::frame
