#pragma once
#include "instance.h"
#include "types.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <memory>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace vulkan {


struct FrameRenderState {
  bool rebuild_swapchain = true;
  u32 current_frame_index = 0;
  static std::vector<std::vector<VkCommandBuffer>> allocated_command_buffers;
  static std::vector<std::vector<std::function<void()>>> resource_free_queue;
};

struct Layer {
  virtual void on_ui_render() {}
  virtual ~Layer() {}
};

class Application {
  const vulkan::Instance *vk;
  ImGui_ImplVulkanH_Window window;
  GLFWwindow *window_handle;
  FrameRenderState state;
  std::vector<std::shared_ptr<Layer>> layers;
  static Application s_instance;
  static bool s_is_init;
  Application(u32 width, u32 height, std::string_view name);
  Application() = default;


public:
  static Application &init(u32 width, u32 height, std::string_view name);
  static Application &get();
  VkCommandBuffer get_command_buffer(bool begin);
  void flush_cmd_buffer(VkCommandBuffer buffer);
  ~Application();
  void main_loop();
  void add_render_callback(std::unique_ptr<Layer> layer);
  void submit_resource_free(std::function<void()> &&func);



  VkPhysicalDevice get_device() const noexcept;
};
} // namespace app
