
#include "vulkan_utils.h"

namespace vulkan {
void check_vkerror(VkResult result) {
  if (result == 0)
    return;
  auto &&log = vklog.error() << "Error: " << result << '\n';
  if (result < 0)
    log << vklog.abort();
}
} // namespace vulkan
