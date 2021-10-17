#include "sys.h"
#include "Swapchain.h"
#include "utils/AIAlert.h"
#include <algorithm>
#ifdef CWDEBUG
#include "debug_ostream_operators.h"
#else
#include "../vulkan/vk_utils/print_flags.h"
#endif

namespace vulkan {

//static
uint32_t Swapchain::get_number_of_images(vk::SurfaceCapabilitiesKHR const& surface_capabilities, uint32_t selected_image_count)
{
  uint32_t max_image_count = surface_capabilities.maxImageCount > 0 ? surface_capabilities.maxImageCount : std::numeric_limits<uint32_t>::max();
  uint32_t image_count = std::clamp(selected_image_count, surface_capabilities.minImageCount, max_image_count);

  return image_count;
}

//static
vk::SurfaceFormatKHR Swapchain::choose_format(std::vector<vk::SurfaceFormatKHR> const& surface_formats)
{
  // FIXME: shouldn't we prefer B8G8R8A8Srgb ?

  // If the list contains only one entry with undefined format
  // it means that there are no preferred surface formats and any can be chosen
  if (surface_formats.size() == 1 && surface_formats[0].format == vk::Format::eUndefined)
    return { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };

  for (auto const& surface_format : surface_formats)
  {
    if (surface_format.format == vk::Format::eB8G8R8A8Unorm && surface_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
      return surface_format;
  }

  // This should have thrown an exception before we got here.
  ASSERT(!surface_formats.empty());

  return surface_formats[0];
}

//static
vk::Extent2D Swapchain::choose_swap_extent(vk::SurfaceCapabilitiesKHR const& surface_capabilities, vk::Extent2D actual_extent)
{
  // The value {-1, -1} is special.
  if (surface_capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max())
    return { std::clamp(actual_extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
             std::clamp(actual_extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height) };

  // Most of the cases we define size of the swapchain images equal to current window's size.
  return surface_capabilities.currentExtent;
}

//static
vk::ImageUsageFlags Swapchain::choose_usage_flags(vk::SurfaceCapabilitiesKHR const& surface_capabilities, vk::ImageUsageFlags const selected_usage)
{
  // Color attachment flag must always be supported.
  // We can define other usage flags but we always need to check if they are supported.
  vk::ImageUsageFlags available_flags = surface_capabilities.supportedUsageFlags & selected_usage;

  if (!available_flags || ((selected_usage & vk::ImageUsageFlagBits::eColorAttachment) && !(available_flags & vk::ImageUsageFlagBits::eColorAttachment)))
    THROW_ALERT("Unsupported swapchain image usage flags requested ([REQUESTED]). Supported swapchain image usages include [SUPPORTED].",
        AIArgs("[REQUESTED]", selected_usage)("[SUPPORTED]", surface_capabilities.supportedUsageFlags));

  return available_flags;
}

//static
vk::SurfaceTransformFlagBitsKHR Swapchain::get_transform(vk::SurfaceCapabilitiesKHR const& surface_capabilities)
{
  // Sometimes images must be transformed before they are presented (i.e. due to device's orienation being other than default orientation).
  // If the specified transform is other than current transform, presentation engine will transform image during presentation operation;
  // this operation may hit performance on some platforms.
  // Here we don't want any transformations to occur so if the identity transform is supported use it otherwise just use the same
  // transform as current transform.
  if ((surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity))
    return vk::SurfaceTransformFlagBitsKHR::eIdentity;

  return surface_capabilities.currentTransform;
}

//static
vk::PresentModeKHR Swapchain::choose_present_mode(std::vector<vk::PresentModeKHR> const& available_present_modes, vk::PresentModeKHR selected_present_mode)
{
  auto have_present_mode = [&](vk::PresentModeKHR requested_present_mode){
    for (auto const& available_present_mode : available_present_modes)
      if (available_present_mode == requested_present_mode)
      {
        Dout(dc::vulkan, "Present mode: " << requested_present_mode);
        return true;
      }
    return false;
  };

  if (have_present_mode(selected_present_mode))
    return selected_present_mode;

  Dout(dc::vulkan|dc::warning, "Requested present mode " << selected_present_mode << " not available!");

  if (have_present_mode(vk::PresentModeKHR::eImmediate))
    return vk::PresentModeKHR::eImmediate;

  if (have_present_mode(vk::PresentModeKHR::eMailbox))
    return vk::PresentModeKHR::eMailbox;

  if (have_present_mode(vk::PresentModeKHR::eFifoRelaxed))
    return vk::PresentModeKHR::eFifoRelaxed;

  if (!have_present_mode(vk::PresentModeKHR::eFifo))
    THROW_ALERT("FIFO present mode is not supported by the swap chain!");

  return vk::PresentModeKHR::eFifo;
}

void Swapchain::prepare(
    Device const* device,
    vk::Extent2D window_extent,
    Queue graphics_queue,
    Queue present_queue,
    vk::SurfaceKHR vh_surface,
    vk::ImageUsageFlags const selected_usage,
    vk::PresentModeKHR const selected_present_mode)
{
  DoutEntering(dc::vulkan, "Swapchain::prepare(" << device << ", " << window_extent << ", " <<
      graphics_queue << ", " << present_queue << ", " << vh_surface << ", " << selected_usage << ", " << selected_present_mode << ")");

  vk::PhysicalDevice vh_physical_device = device->vh_physical_device();

  // Query supported surface details.
  vk::SurfaceCapabilitiesKHR        surface_capabilities = vh_physical_device.getSurfaceCapabilitiesKHR(vh_surface);
  std::vector<vk::SurfaceFormatKHR> surface_formats = vh_physical_device.getSurfaceFormatsKHR(vh_surface);
  std::vector<vk::PresentModeKHR>   available_present_modes = vh_physical_device.getSurfacePresentModesKHR(vh_surface);

  Dout(dc::vulkan, "Surface capabilities: " << surface_capabilities);
  Dout(dc::vulkan, "Supported surface formats: " << surface_formats);
  Dout(dc::vulkan, "Available present modes: " << available_present_modes);

  // In case of re-use, m_can_render might be true.
  m_can_render = false;

  m_device = device;
  m_graphics_queue = graphics_queue;
  m_present_queue = present_queue;

  vk::Extent2D                    desired_extent = choose_swap_extent(surface_capabilities, m_window_extent);
  vk::SurfaceFormatKHR            desired_image_format = choose_format(surface_formats);
  vk::ImageUsageFlags             desired_image_usage_flags = choose_usage_flags(surface_capabilities, selected_usage);
  vk::PresentModeKHR              desired_present_mode = choose_present_mode(available_present_modes, selected_present_mode);
  uint32_t const                  desired_image_count = get_number_of_images(surface_capabilities, 2);
  vk::SurfaceTransformFlagBitsKHR desired_transform = get_transform(surface_capabilities);

  Dout(dc::vulkan, "Requesting " << desired_image_count << " swap chain images (with extent " << desired_extent << ")");
  Dout(dc::vulkan, "Chosen format: " << desired_image_format);
  Dout(dc::vulkan, "Chosen usage: " << desired_image_usage_flags);
  Dout(dc::vulkan, "Chosen present mode: " << desired_present_mode);
  Dout(dc::vulkan, "Used transform: " << desired_transform);

  m_create_info
    .setSurface(vh_surface)
    .setMinImageCount(desired_image_count)
    .setImageFormat(desired_image_format.format)
    .setImageColorSpace(desired_image_format.colorSpace)
    .setImageExtent(desired_extent)
    .setImageArrayLayers(1)
    .setImageUsage(desired_image_usage_flags)
    .setPreTransform(desired_transform)
    .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
    .setPresentMode(desired_present_mode)
    .setClipped(true)
    .setImageSharingMode(graphics_queue.queue_family() == present_queue.queue_family() ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent)
    ;

  // Create for the first time, or recreate if prepare has been called before on this object.
  recreate(desired_extent);
}

void Swapchain::recreate(vk::Extent2D window_extent)
{
  m_can_render = false;

  if ((window_extent.width == 0) || (window_extent.height == 0))
  {
    // Current surface size is (0, 0) so we can't create a swapchain or render anything (m_can_render == false).
    // But we don't want to kill the application as this situation may occur i.e. when window gets minimized.
    return;
  }

  Device const& device = *m_device;
  device->waitIdle();
  m_vhv_images.clear();
  m_image_views.clear();

  m_window_extent = window_extent;
  vk::UniqueSwapchainKHR old_handle(std::move(m_handle));

  m_create_info
    .setImageExtent(m_window_extent)
    .setOldSwapchain(*old_handle)
    ;
  if (m_graphics_queue.queue_family() != m_present_queue.queue_family())
  {
    std::array<uint32_t, 2> queue_family_indices = {
      static_cast<uint32_t>(m_graphics_queue.queue_family().get_value()),
      static_cast<uint32_t>(m_present_queue.queue_family().get_value()) };
    m_create_info.setQueueFamilyIndices(queue_family_indices);
  }

  Dout(dc::vulkan, "Calling Device::createSwapchainKHRUnique(" << m_create_info << ")");
  m_handle = device->createSwapchainKHRUnique(m_create_info);
  m_vhv_images = device->getSwapchainImagesKHR(*m_handle);

  m_swapchain_end = m_vhv_images.iend();
  Dout(dc::vulkan, "Actual number of swap chain images: " << m_swapchain_end);

  vk::ImageSubresourceRange image_subresource_range;
  image_subresource_range
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1)
    ;

  for (SwapchainIndex i{0}; i != m_swapchain_end; ++i)
  {
    vk::ImageViewCreateInfo image_view_create_info;
    image_view_create_info
      .setImage(m_vhv_images[i])
      .setViewType(vk::ImageViewType::e2D)
      .setFormat(m_create_info.imageFormat)
      .setSubresourceRange(image_subresource_range)
      ;

    m_image_views.emplace_back(device->createImageViewUnique(image_view_create_info));
  }
}

} // namespace vulkan
