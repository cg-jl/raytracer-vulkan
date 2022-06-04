
#pragma once

#include "log.h"
#include <vulkan/vulkan.h>

extern utils::Log vklog;

namespace vulkan {
void check_vkerror(VkResult result);
}
