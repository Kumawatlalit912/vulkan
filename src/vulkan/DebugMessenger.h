#pragma once

#include <vulkan/vulkan.hpp>
#include "debug.h"

#ifdef CWDEBUG

namespace vulkan {

class DebugMessenger
{
 private:
  vk::Instance m_vulkan_instance;                       // Copy of the instance that was passed to setup.
  vk::UniqueDebugUtilsMessengerEXT m_debug_messenger;   // The debug messenger handle.

 public:
  DebugMessenger() = default;

  // The life time of the vulkan instance that is passed to setup must be longer
  // than the life time of this object. The reason for that is that the destructor
  // of this object uses it.
  void setup(vk::Instance vulkan_instance, vk::DebugUtilsMessengerCreateInfoEXT const& create_info);
};

} // namespace vulkan

#endif