#pragma once

#include <vulkan/vulkan.hpp>
#include "QueueReply.h" // GPU_queue_family_handle

#ifdef CWDEBUG
#include <iosfwd>
#endif

namespace vulkan {

class Queue
{
  GPU_queue_family_handle m_queue_family;
  vk::Queue m_queue;

 public:
  Queue() = default;
  Queue(GPU_queue_family_handle qfh, vk::Queue queue) : m_queue_family(qfh), m_queue(queue) { }

  vk::Queue vk_handle() const { return m_queue; }
  GPU_queue_family_handle queue_family() const { return m_queue_family; }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif
};

} // namespace vulkan
