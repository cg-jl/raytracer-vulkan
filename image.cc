#include "image.h"
#include "application.h"
#include "instance.h"
#include "vulkan_utils.h"
#include "log.h"

extern utils::Log vklog;

namespace vulkan::utils {
static u32 find_memory_type(VkMemoryPropertyFlags properties, u32 type_bits) {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(Instance::get().physical_device, &props);
  for (u32 i = 0; i != props.memoryTypeCount; ++i) {
    if ((props.memoryTypes[i].propertyFlags & properties) == properties &&
        type_bits & (1 << i)) {
      return i;
    }
  }
  return 0xffffffff;
}

void Image::allocate_memory(u64 size) {
  const auto &vk = Instance::get();
  static constexpr auto format = VK_FORMAT_R8G8B8A8_UNORM;

  // create the image
  {
    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = width;
    info.extent.height = height;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check_vkerror(vkCreateImage(vk.device, &info, nullptr, &image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, image, &req);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, req.memoryTypeBits);
    check_vkerror(vkAllocateMemory(vk.device, &alloc_info, nullptr, &memory));
    check_vkerror(vkBindImageMemory(vk.device, image, memory, 0));
  }

  // create the image view
  {
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk.device, &info, nullptr, &image_view) < 0) {
      vklog.error() << "Could not create image view\n" << vklog.abort();
    }
  }

  // create the sampler
  {
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.minLod = -1000.0f;
    info.maxLod = 1000.0f;
    info.maxAnisotropy = 1.0f;
    if (vkCreateSampler(vk.device, &info, nullptr, &sampler) < 0) {
      vklog.error() << "Could not create sampler\n" << vklog.abort();
    }
  }

  descriptor_set = (VkDescriptorSet)ImGui_ImplVulkan_AddTexture(
      sampler, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Image::set_data(const void *data) {
  const auto device = Instance::get().device;
  const size_t upload_size = width * height * 4 * 1 /* bytes per channel */;

  if (!staging_buffer) {
    // create upload buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = upload_size;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer) < 0) {
      vklog.error() << "Could not create staging buffer\n" << vklog.abort();
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, staging_buffer, &req);
    aligned_size = req.size;
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
    if (vkAllocateMemory(device, &alloc_info, nullptr, &staging_buffer_memory) <
        0) {
      vklog.error() << "Could not allocate memory for staging buffer\n"
                    << vklog.abort();
    }
    if (vkBindBufferMemory(device, staging_buffer, staging_buffer_memory, 0) <
        0) {
      vklog.error() << "Could not bind buffer memory for staging buffer\n"
                    << vklog.abort();
    }
  }

  // upload to buffer
  {
    void *map = NULL;
    if (vkMapMemory(device, staging_buffer_memory, 0, aligned_size, 0, &map) <
        0) {
      vklog.error() << "Could not map staging buffer memory to local buffer\n"
                    << vklog.abort();
    }
    memcpy(map, data, upload_size);
    VkMappedMemoryRange range[1] = {};
    range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range[0].memory = staging_buffer_memory;
    range[0].size = aligned_size;
    if (vkFlushMappedMemoryRanges(device, 1, range) < 0) {
      vklog.error() << "Could not flush memory to GPU\n" << vklog.abort();
    }
    vkUnmapMemory(device, staging_buffer_memory);
  }

  // copy to imge
  {
    VkCommandBuffer cmd = Application::get().get_command_buffer(true);

    VkImageMemoryBarrier copy_barrier = {};
    copy_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    copy_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copy_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    copy_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_barrier.image = image;
    copy_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_barrier.subresourceRange.levelCount = 1;
    copy_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &copy_barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, staging_buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier use_barrier = {};
    use_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    use_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    use_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    use_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    use_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    use_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    use_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    use_barrier.image = image;
    use_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    use_barrier.subresourceRange.levelCount = 1;
    use_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &use_barrier);

    Application::get().flush_cmd_buffer(cmd);
  }
}

Image::Image(u32 width, u32 height, const void *data)
    : width(width), height(height) {
  allocate_memory(width * height * 1);
  if (data)
    set_data(data);
}
VkDescriptorSet Image::get_descriptor_set() const noexcept {
  return descriptor_set;
}
u32 Image::get_width() const noexcept { return width; }
u32 Image::get_height() const noexcept { return height; }

Image::~Image() {
  Application::get().submit_resource_free(
      [sampler = sampler, image_view = image_view, image = image,
       memory = memory, staging_buffer = staging_buffer,
       staging_buffer_memory = staging_buffer_memory]() {
        const auto device = Instance::get().device;
        vkDestroySampler(device, sampler, nullptr);
        vkDestroyImageView(device, image_view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_buffer_memory, nullptr);
      });
}

} // namespace vulkan::utils
