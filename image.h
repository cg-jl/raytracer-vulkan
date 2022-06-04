#pragma once
#include "types.h"
#include <vulkan/vulkan.h>

namespace vulkan::utils {
// RGBA image.
class Image {
  u32 width, height;
  VkImage image = nullptr;
  VkDescriptorSet descriptor_set = nullptr;
  VkDeviceMemory memory = nullptr, staging_buffer_memory = nullptr;
  VkSampler sampler = nullptr;
  VkBuffer staging_buffer = nullptr;
  VkImageView image_view = nullptr;
  VkDeviceSize aligned_size = 0;

  void allocate_memory(u64 size);

public:
  Image(u32 width, u32 height, const void *data);
  ~Image();
  void set_data(const void *data);
  VkDescriptorSet get_descriptor_set() const noexcept;
  u32 get_width() const noexcept;
  u32 get_height() const noexcept;
};
} // namespace vulkan::utils
