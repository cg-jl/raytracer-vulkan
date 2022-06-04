#pragma once
#include <vulkan/vulkan.h>

#include "types.h"


namespace vulkan {
  // TODO: convert back to a class when management is done and I know all public APIs
struct Instance {
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  u32 queue_family;
  VkQueue queue;
  VkDescriptorPool descriptor_pool;

  static Instance s_instance;
  static bool s_init_instance;

  Instance() = default;
  Instance(const char **extensions, u32 extension_count);
 ~Instance();
 static bool setup_vulkan(Instance &i, const char **extensions, u32 extension_count);

  public:
  static Instance &get_or_init(const char **extensions, u32 extension_count);
  static const Instance &get();
};
}
