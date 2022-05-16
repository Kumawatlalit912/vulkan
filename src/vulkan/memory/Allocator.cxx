#include "sys.h"
#include "Allocator.h"
#include "debug/DebugSetName.h"
#include "utils/AIAlert.h"

namespace vulkan::memory {

void Allocator::create(VmaAllocatorCreateInfo const& vma_allocator_create_info)
{
  // Only call create() once.
  ASSERT(!m_handle);
  vk::Result res = static_cast<vk::Result>(vmaCreateAllocator(&vma_allocator_create_info, &m_handle));
  if (res != vk::Result::eSuccess)
    THROW_ALERTC(res, "vmaCreateAllocator");
}

vk::Buffer Allocator::create_buffer(vk::BufferCreateInfo const& buffer_create_info, VmaAllocationCreateInfo const& vma_allocation_create_info, VmaAllocation* vh_allocation
    COMMA_CWDEBUG_ONLY(Ambifix const& allocation_name)) const
{
  VkBuffer vh_buffer;
  vk::Result res = static_cast<vk::Result>(
      vmaCreateBuffer(m_handle, &static_cast<VkBufferCreateInfo const&>(buffer_create_info), &vma_allocation_create_info, &vh_buffer, vh_allocation, nullptr)
      );
  if (res != vk::Result::eSuccess)
    THROW_ALERTC(res, "vmaCreateBuffer");
  Debug(vmaSetAllocationName(m_handle, *vh_allocation, allocation_name.object_name().c_str()));
  return vh_buffer;
}

} // namespace vulkan::memory
