#include "sys.h"
#include "FrameResourceIndex.h"

namespace vulkan {

std::string to_string(FrameResourceIndex index)
{
  std::string result = "f";
  result += std::to_string(index.get_value());
  return result;
}

} // namespace vulkan
