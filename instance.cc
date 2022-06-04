#include "instance.h"
#include "log.h"
#include <array>
#include <memory>

extern utils::Log vklog;

namespace vulkan {

bool Instance::setup_vulkan(Instance &vk, const char **extensions,
                            u32 extensions_count) {
  vklog.info() << "Initializing with extensions:\n";
  for (u32 i = 0; i != extensions_count; ++i) {
    vklog.stream << ' ' << extensions[i] << "\n";
  }
  VkInstanceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.enabledExtensionCount = extensions_count;
  info.ppEnabledExtensionNames = extensions;

  vklog.debug() << "Creating instance\n";

  if (const VkResult err = vkCreateInstance(&info, NULL, &vk.instance); err) {
    vklog.error() << "Could not create instance: " << err << '\n';
    return false;
  }
  vklog.ok() << "Instance created\n";

  vklog.debug() << "Selecting GPU\n";

  // select gpu
  {
    u32 gpu_count;
    if (const auto err =
            vkEnumeratePhysicalDevices(vk.instance, &gpu_count, NULL);
        err || gpu_count == 0) {
      vklog.error() << "Could not get a physical device that supports Vulkan\n";
      return false;
    }
    const auto gpus = std::make_unique<VkPhysicalDevice[]>(gpu_count);
    if (const auto err =
            vkEnumeratePhysicalDevices(vk.instance, &gpu_count, gpus.get());
        err) {
      vklog.error() << "Error when enumerating physical devices\n";
    }
    vklog.info() << "Selecting the best GPU I can out of " << gpu_count
                 << " GPU(s)\n";
    size_t use_gpu = 0;
    // if  anumber >1 of GPUs got reported, try to find a discrete GPU if
    // present
    for (size_t i = 0; i != gpu_count; ++i) {
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(gpus[i], &properties);
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        use_gpu = i;
        break;
      }
    }

    vk.physical_device = gpus[use_gpu];
  }
  {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    vklog.ok() << "Selected physical device " << properties.deviceName << '\n';
  }

  // select graphics queue family
  u32 family = 0;
  {
    u32 count;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &count, NULL);
    const auto queues = std::make_unique<VkQueueFamilyProperties[]>(count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &count,
                                             queues.get());
    bool got_family = false;
    for (size_t i = 0; i != count; ++i) {
      if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        family = i;
        got_family = true;
        break;
      }
    }


    if (!got_family) {
      vklog.error() << "Could not find a queue family that supports graphics\n";
      return false;
    }

    vk.queue_family = family;
  }

  // create logical device with 1 queue
  {
    std::array<const char *, 1> device_extensions{"VK_KHR_swapchain"};
    std::array<float, 1> queue_priority{1.0f};
    VkDeviceQueueCreateInfo queue_info[1] = {};
    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = family;
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = queue_priority.begin();
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = queue_info;
    create_info.enabledExtensionCount = device_extensions.size();
    create_info.ppEnabledExtensionNames = device_extensions.begin();
    if (const auto err =
            vkCreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
        err) {
      vklog.error() << "Could not create logical device\n";
      return false;
    }

    vkGetDeviceQueue(vk.device, family, 0, &vk.queue);
  }

  vklog.ok() << "Created logical device\n";
  vklog.info() << "Creating descriptor pool...\n";

  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * 11;
    pool_info.poolSizeCount = 11;
    pool_info.pPoolSizes = pool_sizes;
    if (const auto err = vkCreateDescriptorPool(vk.device, &pool_info, NULL,
                                                &vk.descriptor_pool);
        err) {
      vklog.error() << "Could not create descriptor pool\n";
      return false;
    }
  }

  vklog.ok() << "Setup done\n";

  return true;
}
Instance::Instance(const char **extensions, u32 extension_count) {
  if (!setup_vulkan(*this, extensions, extension_count)) {
    throw std::runtime_error("Could not initialize a Vulkan instance.");
  }
}

Instance::~Instance() {
  if (s_init_instance) {
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    s_init_instance = false;
  }
}

bool Instance::s_init_instance = false;
Instance Instance::s_instance;

Instance &Instance::get_or_init(const char **extensions, u32 extension_count) {
  if (!s_init_instance) {
    s_instance = Instance(extensions, extension_count);
    s_init_instance = true;
  }
  return s_instance;
}

const Instance &Instance::get() {
  if (!s_init_instance) throw std::logic_error("instance must be initialized");
  return s_instance;
}

} // namespace vulkan
