#pragma once

#include "public/vr_runtime_api.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace vrclient::runtime::frame {

// Vulkan test-scene renderer (port of the former D3D11TestSceneRenderer). Draws a
// single colored cube per eye with a per-eye tint + clear color.
//
// SEPARABILITY (HARD CONSTRAINT #4): this class touches NO xr* API. It renders
// purely to VkImages handed in via prepareEyeTargets() (which OpenXR owns at run
// time, or which a headless test owns for validation). The OpenXR runtime calls
// initialize() once, prepareEyeTargets() per eye when swapchains exist, and
// renderEye() per frame; a headless harness can drive the exact same three calls
// over an offscreen VkImage with no XrSession.
//
// Object lifetime split (see brief §1.3): format/extent-INDEPENDENT objects
// (buffers, command pool, shader modules, descriptor layout/pool) are built in
// initialize(); format/extent-DEPENDENT objects (render pass, pipeline, depth,
// per-image views + framebuffers + command buffers) are built in
// prepareEyeTargets().
class VulkanTestSceneRenderer {
 public:
  // Format-independent setup. Called once in OpenXrRuntime::start() before
  // swapchains exist. queue is used at render time for submit.
  bool initialize(
      VkInstance instance,
      VkPhysicalDevice physical_device,
      VkDevice device,
      uint32_t queue_family_index,
      VkQueue queue);

  // Builds the render pass / pipeline (matching color_format) plus the depth
  // image, and a VkImageView + VkFramebuffer + pre-allocated command buffer per
  // supplied VkImage. The images are NOT owned (OpenXR or a test owns them).
  bool prepareEyeTargets(
      VrRuntimeEye eye,
      const VkImage* images,
      uint32_t image_count,
      VkFormat color_format,
      uint32_t width,
      uint32_t height);

  // Records + submits one frame for the given eye into the prebuilt framebuffer
  // selected by image_index. Allocation-free hot path; `image` is unused beyond
  // identity (the prebuilt framebuffer for image_index is what is rendered).
  void renderEye(
      const VrRuntimeFrameData& frame,
      VrRuntimeEye eye,
      uint32_t image_index,
      VkImage image);

  // Blocks until the device is idle (for clean teardown / headless validation).
  void waitIdle();

  // Lightweight in-headset debug overlay (DIAG-03 partial): a colored OpenXR-state
  // status block plus an error indicator, drawn in an NDC corner over the scene.
  // Both setters write plain POD members OUTSIDE any hot path; renderEye only reads
  // them, so the recorded hot path stays alloc-/lock-/log-free.
  void setOverlayEnabled(bool enabled) { overlay_enabled_ = enabled; }
  // has_recent_warnings flips the error-indicator block to red. Derived by the
  // caller from the DiagnosticsOverlay snapshot (recent_warning_count > 0).
  void setOverlayState(bool has_recent_warnings) {
    overlay_has_warning_ = has_recent_warnings;
  }

  void destroy();

  ~VulkanTestSceneRenderer();

  VkDevice device() const { return device_; }
  uint32_t queueFamilyIndex() const { return queue_family_index_; }

 private:
  struct EyeResources {
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkFence> fences;
    // One depth attachment PER swapchain image (parallel to image_views), so two
    // images in flight never share a depth target -> no cross-image write-after-
    // write hazard when consecutive frames acquire different image indices.
    std::vector<VkImage> depth_images;
    std::vector<VkDeviceMemory> depth_memories;
    std::vector<VkImageView> depth_views;
    VkBuffer uniform_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_memory = VK_NULL_HANDLE;
    void* uniform_mapped = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
  };

  bool createBuffer(
      VkDeviceSize size,
      VkBufferUsageFlags usage,
      VkMemoryPropertyFlags properties,
      VkBuffer* out_buffer,
      VkDeviceMemory* out_memory);
  bool uploadViaStaging(
      VkBuffer dst, const void* data, VkDeviceSize size);
  bool findMemoryType(
      uint32_t type_filter, VkMemoryPropertyFlags properties, uint32_t* out_index) const;
  bool ensureRenderPass(VkFormat color_format);
  bool ensurePipeline();
  // Builds the overlay pipeline (depth-test OFF, alpha blend ON) against
  // render_pass_. Format/extent-independent overlay resources (shader modules,
  // push-constant-only layout, NDC quad buffer) are built in initialize();
  // this just creates the render-pass-dependent pipeline, guarded for reuse.
  bool ensureOverlayPipeline();
  void destroyEye(EyeResources& eye);

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_index_ = 0;

  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkShaderModule vertex_module_ = VK_NULL_HANDLE;
  VkShaderModule fragment_module_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkFormat color_format_ = VK_FORMAT_UNDEFINED;
  VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;

  VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
  VkBuffer index_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
  uint32_t index_count_ = 0;

  // Overlay (DIAG-03 partial) resources. The pipeline uses only a push constant
  // (color + NDC rect) and a static unit-square quad vertex buffer; no descriptor
  // sets, no UBO. Created lazily next to the cube resources and torn down in
  // destroy().
  VkShaderModule overlay_vertex_module_ = VK_NULL_HANDLE;
  VkShaderModule overlay_fragment_module_ = VK_NULL_HANDLE;
  VkPipelineLayout overlay_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
  VkBuffer overlay_vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory overlay_vertex_memory_ = VK_NULL_HANDLE;
  bool overlay_enabled_ = false;
  bool overlay_has_warning_ = false;

  std::array<EyeResources, 2> eye_resources_{};
  bool base_initialized_ = false;
};

}  // namespace vrclient::runtime::frame
