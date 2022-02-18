#ifndef LOGICAL_DEVICE_DECLARATION_H
#define LOGICAL_DEVICE_DECLARATION_H

#include "DispatchLoader.h"
#include "Swapchain.h"
#include "QueueReply.h"
#include "RenderPassAttachmentData.h"
#include "Queue.h"
#include "Texture.h"
#include "Attachment.h"
#include "DescriptorSetParameters.h"
#include "ImageKind.h"
#include "SamplerKind.h"
#include "BufferParameters.h"
#include "statefultask/AIStatefulTask.h"
#include "statefultask/TaskEvent.h"
#include "utils/Badge.h"
#include "utils/Vector.h"
#include <boost/intrusive_ptr.hpp>
#include <filesystem>

namespace task {
class SynchronousWindow;
} // namespace task

namespace vulkan {

// Forward declarations.
class PhysicalDeviceFeatures;
class DeviceCreateInfo;
class PresentationSurface;
class CommandBuffer;
class ImGui;
struct AmbifixOwner;
class Swapchain;
class RenderPass;
class Application;
class SamplerKind;
using SwapchainIndex = utils::VectorIndex<Swapchain>;

namespace rendergraph {
class RenderPass;
} // namespace rendergraph

// The collection of queue family properties for a given physical device.
class QueueFamilies
{
 private:
  utils::Vector<QueueFamilyProperties> m_queue_families;

 public:
  // Check that for every required feature in device_create_info, there is at least one queue family that supports it.
  bool is_compatible_with(DeviceCreateInfo const& device_create_info, utils::Vector<QueueReply, QueueRequestIndex>& queue_replies);

  QueueFamilyProperties const& operator[](QueueFamilyPropertiesIndex index) const
  {
    return m_queue_families[index];
  }

  // Construct a vector of QueueFamilyProperties for physical_device and surface (for the presentation capability bit).
  QueueFamilies(vk::PhysicalDevice physical_device, vk::SurfaceKHR surface);

  // Construct an empty vector.
  QueueFamilies() = default;
};

struct UnsupportedQueueFlagsException : std::exception {
};

class LogicalDevice
{
 private:
  vk::PhysicalDevice m_vh_physical_device;              // The underlaying physical device.
  vk::UniqueDevice m_device;                            // A handle to the logical device.
  utils::Vector<QueueReply, QueueRequestIndex> m_queue_replies;
  QueueFamilies m_queue_families;
  vk::DeviceSize m_non_coherent_atom_size;              // Allocated non-coherent memory must be a multiple of this value in size.
  float m_max_sampler_anisotropy;                       // GraphicsSettingsPOD::maxAnisotropy must be less than or equal this value.
  bool m_supports_separate_depth_stencil_layouts;       // Set if the physical device supports vk::PhysicalDeviceSeparateDepthStencilLayoutsFeatures.
  bool m_supports_sampler_anisotropy = {};

#ifdef CWDEBUG
  std::string m_debug_name;
#endif

 public:
  virtual ~LogicalDevice() = default;

  void prepare(vk::Instance vulkan_instance, DispatchLoader& dispatch_loader, task::SynchronousWindow const* window_task_ptr);

  // Accessor for underlaying physical and logical device.
  vk::PhysicalDevice vh_physical_device() const { return m_vh_physical_device; }
  vk::Device vh_logical_device(utils::Badge<ImGui>) const { return *m_device; }

  bool verify_presentation_support(vulkan::PresentationSurface const&) const;
  bool supports_separate_depth_stencil_layouts() const { return m_supports_separate_depth_stencil_layouts; }
  bool supports_sampler_anisotropy() const { return m_supports_sampler_anisotropy; }
  vk::DeviceSize non_coherent_atom_size() const { return m_non_coherent_atom_size; }
  float max_sampler_anisotropy() const { return m_max_sampler_anisotropy; }

  void print_on(std::ostream& os) const { char const* prefix = ""; os << '{'; print_members(os, prefix); os << '}'; }
  void print_members(std::ostream& os, char const* prefix) const;

#ifdef CWDEBUG
  // Set debug name for this device.
  void set_debug_name(std::string debug_name) { m_debug_name = std::move(debug_name); }
  std::string const& debug_name() const { return m_debug_name; }
  // Set debug name for an object created from this device.
  void set_debug_name(vk::DebugUtilsObjectNameInfoEXT const& name_info) const { m_device->setDebugUtilsObjectNameEXT(name_info); }
#endif

  // Return the (next) queue for window_cookie (as passed to Application::create_root_window).
  Queue acquire_queue(QueueFlags flags, vulkan::QueueReply::window_cookies_type window_cookie) const;

  // Wait the completion of outstanding queue operations for all queues of this logical device.
  // This is a blocking call, only intended for program termination.
  void wait_idle() const { m_device->waitIdle(); }

  // Unsorted additional functions.
  inline vk::UniqueSemaphore create_semaphore(CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  inline vk::UniqueFence create_fence(bool signaled COMMA_CWDEBUG_ONLY(bool debug_output, AmbifixOwner const& ambifix)) const;
  vk::Result wait_for_fences(vk::ArrayProxy<vk::Fence const> const& fences, vk::Bool32 wait_all, uint64_t timeout) const
  {
    DoutEntering(dc::vkframe, "LogicalDevice::wait_for_fences(" << fences << ", " << wait_all << ", " << timeout << ")");
    return m_device->waitForFences(fences, wait_all, timeout);
  }
  void reset_fences(vk::ArrayProxy<vk::Fence const> const& fences) const
  {
    DoutEntering(dc::vkframe, "LogicalDevice::reset_fences(" << fences << ")");
    m_device->resetFences(fences);
  }
  inline vk::UniqueCommandPool create_command_pool(uint32_t queue_family_index, vk::CommandPoolCreateFlags flags
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& debug_name)) const;
  inline void destroy_command_pool(vk::CommandPool command_pool) const;
  vk::Result acquire_next_image(vk::SwapchainKHR swapchain, uint64_t timeout, vk::Semaphore semaphore, vk::Fence fence, SwapchainIndex& image_index_out) const
  {
    DoutEntering(dc::vkframe, "LogicalDevice::acquire_next_image(" << swapchain << ", " << timeout << ", " << semaphore << ", " << fence << ", ...)");

    uint32_t new_image_index;
    auto result = m_device->acquireNextImageKHR(swapchain, timeout, semaphore, fence, &new_image_index);
    image_index_out = SwapchainIndex(new_image_index);
    return result;
  }

  // Create a Sampler.
  vk::UniqueSampler create_sampler(SamplerKind const& sampler_kind, GraphicsSettingsPOD const& graphics_settings
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  // Create a Sampler, allowing to pass an initializer list to construct the SamplerKind (from temporary SamplerKindPOD).
  vk::UniqueSampler create_sampler(SamplerKindPOD&& sampler_kind, GraphicsSettingsPOD const& graphics_settings
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const { return create_sampler({this, std::move(sampler_kind)}, graphics_settings COMMA_CWDEBUG_ONLY(ambifix)); }

  vk::UniqueImage create_image(vk::Extent2D extent, vulkan::ImageKind const& image_kind
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;

  // Create all texture objects at once.
  // Use sampler as-is.
  Texture create_texture(uint32_t width, uint32_t height, vulkan::ImageViewKind const& image_view_kind,
      vk::MemoryPropertyFlagBits property, vk::UniqueSampler&& sampler
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  // Create sampler too.
  Texture create_texture(uint32_t width, uint32_t height, vulkan::ImageViewKind const& image_view_kind,
      vk::MemoryPropertyFlagBits property, SamplerKind const& sampler_kind, GraphicsSettingsPOD const& graphics_settings
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const
      { return create_texture(width, height, image_view_kind, property, create_sampler(sampler_kind, graphics_settings COMMA_CWDEBUG_ONLY(ambifix)) COMMA_CWDEBUG_ONLY(ambifix)); }
  // Create sampler too, allowing to pass an initializer list to construct the SamplerKind (from temporary SamplerKindPOD).
  Texture create_texture(uint32_t width, uint32_t height, vulkan::ImageViewKind const& image_view_kind,
      vk::MemoryPropertyFlagBits property, SamplerKindPOD const&& sampler_kind, GraphicsSettingsPOD const& graphics_settings
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const
      { return create_texture(width, height, image_view_kind, property, { this, std::move(sampler_kind) }, graphics_settings COMMA_CWDEBUG_ONLY(ambifix)); }
  // Create all attachment objects at once.
  Attachment create_attachment(uint32_t width, uint32_t height, vulkan::ImageViewKind const& image_view_kind, vk::MemoryPropertyFlagBits property
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;

  vk::UniqueImageView create_image_view(vk::Image vh_image, ImageViewKind const& image_view_kind
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;

  vk::UniqueShaderModule create_shader_module(uint32_t const* spirv_code, size_t spirv_size
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueDeviceMemory allocate_image_memory(vk::Image image, vk::MemoryPropertyFlagBits property
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueRenderPass create_render_pass(rendergraph::RenderPass const& render_graph_pass
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueFramebuffer create_imageless_framebuffer(RenderPass const& render_graph_pass, vk::Extent2D extent, uint32_t layers
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueDescriptorPool create_descriptor_pool(std::vector<vk::DescriptorPoolSize> const& pool_sizes, uint32_t max_sets
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  DescriptorSetParameters create_descriptor_resources(std::vector<vk::DescriptorSetLayoutBinding> const& layout_bindings, std::vector<vk::DescriptorPoolSize> const& pool_sizes
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueDescriptorSetLayout create_descriptor_set_layout(std::vector<vk::DescriptorSetLayoutBinding> const& layout_bindings
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  std::vector<vk::UniqueDescriptorSet> allocate_descriptor_sets(std::vector<vk::DescriptorSetLayout> const& descriptor_set_layout, vk::DescriptorPool descriptor_pool
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  void allocate_command_buffers(vk::CommandPool const& pool, vk::CommandBufferLevel level, uint32_t count, vk::CommandBuffer* command_buffers_out
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix, bool is_array = true)) const;
  void update_descriptor_set(vk::DescriptorSet descriptor_set, vk::DescriptorType descriptor_type, uint32_t binding, uint32_t array_element,
      std::vector<vk::DescriptorImageInfo> const& image_infos = {}, std::vector<vk::DescriptorBufferInfo> const& buffer_infos = {},
      std::vector<vk::BufferView> const& buffer_views = {}) const;
  vk::UniquePipelineLayout create_pipeline_layout(std::vector<vk::DescriptorSetLayout> const& descriptor_set_layouts, std::vector<vk::PushConstantRange> const& push_constant_ranges
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueBuffer create_buffer(uint32_t size, vk::BufferUsageFlags usage
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueDeviceMemory allocate_buffer_memory(vk::Buffer buffer, vk::MemoryPropertyFlagBits property
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  BufferParameters create_buffer(uint32_t size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlagBits memoryProperty
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniqueSwapchainKHR create_swapchain(vk::Extent2D extent, uint32_t min_image_count, PresentationSurface const& presentation_surface,
      SwapchainKind const& swapchain_kind, vk::SwapchainKHR vh_old_swapchain
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  vk::UniquePipeline create_graphics_pipeline(vk::PipelineCache vh_pipeline_cache, vk::GraphicsPipelineCreateInfo const& graphics_pipeline_create_info
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  Swapchain::images_type get_swapchain_images(task::SynchronousWindow const* owning_window, vk::SwapchainKHR vh_swapchain
      COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix)) const;
  void* map_memory(vk::DeviceMemory vh_memory, vk::DeviceSize offset, vk::DeviceSize size) const;
  void flush_mapped_memory_ranges(vk::ArrayProxy<vk::MappedMemoryRange const> const& mapped_memory_ranges) const;
  void unmap_memory(vk::DeviceMemory vh_memory) const;

 private:
  // Override this function to change the default physical device features.
  virtual void prepare_physical_device_features(vk::PhysicalDeviceFeatures& features10, vk::PhysicalDeviceVulkan11Features& features11, vk::PhysicalDeviceVulkan12Features& features12) const { }

  // Override this function to add QueueRequest objects. The default will create a graphics and presentation queue.
  virtual void prepare_logical_device(DeviceCreateInfo& device_create_info) const { }
};

} // namespace vulkan

namespace task {

class LogicalDevice : public AIStatefulTask
{
 protected:
  vulkan::Application* m_application;
 private:
  boost::intrusive_ptr<task::SynchronousWindow> m_root_window;          // The root window that we have to support presentation to (if any). Only used during initialization.
                                                                        // This is reset as soon as we added ourselves to m_application; so don't use it.
  std::unique_ptr<vulkan::LogicalDevice> m_logical_device;              // Temporary storage of a pointer to the logical device object.
                                                                        // This is moved away to the Application and becomes nullptr; so don't use it.
  int m_index = -1;                                                     // Logical device index into Application::m_registered_tasks.

 public:
  statefultask::TaskEvent m_logical_device_index_available_event;       // Triggered when m_root_window has its logical device index set.

 protected:
  /// The base class of this task.
  using direct_base_type = AIStatefulTask;

  /// The different states of the stateful task.
  enum logical_device_state_type {
    LogicalDevice_wait_for_window = direct_base_type::state_end,
    LogicalDevice_create,
    LogicalDevice_done
  };

 public:
  /// One beyond the largest state of this task.
  static constexpr state_type state_end = LogicalDevice_done + 1;
  static constexpr condition_type window_available_condition = 1;

 public:
  LogicalDevice(vulkan::Application* application COMMA_CWDEBUG_ONLY(bool debug = false));

  void set_logical_device(std::unique_ptr<vulkan::LogicalDevice>&& logical_device) { m_logical_device = std::move(logical_device); }
  void set_root_window(boost::intrusive_ptr<task::SynchronousWindow const>&& root_window);

  int get_index() const { ASSERT(m_index != -1); return m_index; }

 protected:
  /// Call finish() (or abort()), not delete.
  ~LogicalDevice() override;

  /// Implemenation of state_str for run states.
  char const* state_str_impl(state_type run_state) const override final;

  /// Handle mRunState.
  void multiplex_impl(state_type run_state) override;
};

} // namespace task

#include "debug/DebugSetName.h"
#endif // LOGICAL_DEVICE_DECLARATION_H

#ifndef LOGICAL_DEVICE_DEFINITIONS_H
#define LOGICAL_DEVICE_DEFINITIONS_H

namespace vulkan {

// Define inlined functions that use DebugSetName (see https://stackoverflow.com/a/69873866/1487069).
vk::UniqueSemaphore LogicalDevice::create_semaphore(CWDEBUG_ONLY(AmbifixOwner const& debug_name)) const
{
  vk::UniqueSemaphore semaphore = m_device->createSemaphoreUnique({});
  DebugSetName(semaphore, debug_name);
  return semaphore;
}

vk::UniqueFence LogicalDevice::create_fence(bool signaled COMMA_CWDEBUG_ONLY(bool debug_output, AmbifixOwner const& debug_name)) const
{
  DoutEntering(dc::vulkan(debug_output)|continued_cf, "LogicalDevice::create_fence(" << signaled << ") = ");
  vk::UniqueFence fence = m_device->createFenceUnique({ .flags = signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlagBits(0u) });
  DebugSetName(fence, debug_name);
  Dout(dc::finish, *fence);
  return fence;
}

vk::UniqueCommandPool LogicalDevice::create_command_pool(uint32_t queue_family_index, vk::CommandPoolCreateFlags flags COMMA_CWDEBUG_ONLY(AmbifixOwner const& debug_name)) const
{
  vk::UniqueCommandPool command_pool = m_device->createCommandPoolUnique({ .flags = flags, .queueFamilyIndex = queue_family_index });
  DebugSetName(command_pool, debug_name);
  return command_pool;
}

void LogicalDevice::destroy_command_pool(vk::CommandPool command_pool) const
{
  m_device->destroyCommandPool(command_pool);
}

} // namespace vulkan

#endif // LOGICAL_DEVICE_DEFINITIONS_H
