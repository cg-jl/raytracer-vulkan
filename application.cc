#include "application.h"
#include "log.h"
#include <imgui/backends/imgui_impl_glfw.h>

#include "RobotoRegular.embed"

extern utils::Log vklog;

namespace vulkan {

static constexpr size_t k_min_image_count = 2;

static bool setup_vulkan_window(const vulkan::Instance &vk,
                                ImGui_ImplVulkanH_Window *wd,
                                VkSurfaceKHR surface, int width, int height) {

  vklog.info() << "Setting up vulkan window...\n";
  wd->Surface = surface;

  VkBool32 res;
  vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical_device, vk.queue_family,
                                       surface, &res);

  if (res != VK_TRUE) {
    vklog.error() << "No KHR support on physical device\n";
    return false;
  }

  static constexpr VkFormat request_surface_image_format[] = {
      VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};

  static constexpr VkColorSpaceKHR request_surface_color_space =
      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

  wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
      vk.physical_device, surface, request_surface_image_format,
      (size_t)(IM_ARRAYSIZE(request_surface_image_format)),
      request_surface_color_space);

  VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};

  wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
      vk.physical_device, surface, &present_modes[0],
      IM_ARRAYSIZE(present_modes));

  static_assert(k_min_image_count >= 2, "min image count must be >= 2");

  // crete swapchin, render pass, frame buffers, etc
  ImGui_ImplVulkanH_CreateOrResizeWindow(vk.instance, vk.physical_device,
                                         vk.device, wd, vk.queue_family, NULL,
                                         width, height, k_min_image_count);

  vklog.ok() << "Window setup done.\n";
  return true;
}

static void cleanup_vulkan_window(const vulkan::Instance &vk,
                                  ImGui_ImplVulkanH_Window *wd) {
  ImGui_ImplVulkanH_DestroyWindow(vk.instance, vk.device, wd, NULL);
}

static void frame_render(const vulkan::Instance &vk,
                         ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data,
                         FrameRenderState &state) {
  const VkSemaphore image_acquired_semaphore =
      wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;

  const VkSemaphore render_complete_semaphore =
      wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

  if (const auto err = vkAcquireNextImageKHR(
          vk.device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore,
          VK_NULL_HANDLE, &wd->FrameIndex);
      err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
    state.rebuild_swapchain = true;
    return;
  } else if (err < 0) {
    vklog.error() << "Could not acquire next image";
    exit(1);
  }

  state.current_frame_index = (state.current_frame_index + 1) % wd->ImageCount;

  ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
  if (vkWaitForFences(vk.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX) < 0) {
    vklog.error() << "Could not wait for fence\n" << vklog.abort();
  }

  if (vkResetFences(vk.device, 1, &fd->Fence) < 0) {
    vklog.error() << "Could not reset fences\n" << vklog.abort();
  }

  {
    // free resources in queue
    for (auto &func : state.resource_free_queue[state.current_frame_index]) {
      func();
    }
    state.resource_free_queue[state.current_frame_index].clear();
  }

  {
    auto &allocated_command_buffers =
        state.allocated_command_buffers[wd->FrameIndex];
    if (allocated_command_buffers.size() > 0) {
      vkFreeCommandBuffers(vk.device, fd->CommandPool,
                           (u32)allocated_command_buffers.size(),
                           allocated_command_buffers.data());
      allocated_command_buffers.clear();
    }

    if (vkResetCommandPool(vk.device, fd->CommandPool, 0) < 0) {
      vklog.error() << "Could not reset command pool\n" << vklog.abort();
    }

    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(fd->CommandBuffer, &info) < 0) {
      vklog.error() << "Could not init command buffer\n" << vklog.abort();
    }
  }
  {
    VkRenderPassBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = wd->RenderPass;
    info.framebuffer = fd->Framebuffer;
    info.renderArea.extent.width = wd->Width;
    info.renderArea.extent.height = wd->Height;
    info.clearValueCount = 1;
    info.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
  }

  // record dear imgui primitives into command buffer
  ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

  vkCmdEndRenderPass(fd->CommandBuffer);

  {
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &image_acquired_semaphore;
    info.pWaitDstStageMask = &wait_stage;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &fd->CommandBuffer;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &render_complete_semaphore;

    if (vkEndCommandBuffer(fd->CommandBuffer) < 0) {
      vklog.error() << "Could not build command buffer\n" << vklog.abort();
    }

    if (vkQueueSubmit(vk.queue, 1, &info, fd->Fence) < 0) {
      vklog.error() << "Could not submit commands to the queue\n"
                    << vklog.abort();
    }
  }
}

static void present_frame(const vulkan::Instance &vk,
                          ImGui_ImplVulkanH_Window *wd,
                          FrameRenderState &state) {
  // don't present the frame if we have to rebuild the swapchain
  if (state.rebuild_swapchain)
    return;

  VkSemaphore render_complete_semaphore =
      wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
  VkPresentInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &render_complete_semaphore;
  info.swapchainCount = 1;
  info.pSwapchains = &wd->Swapchain;
  info.pImageIndices = &wd->FrameIndex;
  if (const auto err = vkQueuePresentKHR(vk.queue, &info);
      err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
    state.rebuild_swapchain = true;
    return;
  } else if (err < 0) {
    vklog.error() << "Could not present KHR\n" << vklog.abort();
  }

  // now we can use the next set of semaphores
  wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount;
}

static utils::Log glfwlog("GLFW");
static utils::Log applog("app");

static void glfw_error_callback(int error, const char *description) {
  glfwlog.error() << "Error " << error << ": " << description << '\n';
}

Application::Application(u32 width, u32 height, std::string_view name) {
  auto &app = *this;
  applog.info() << "Initializing GLFW\n";
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    applog.error() << "Could not init GLFW\n" << applog.abort();
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (!glfwVulkanSupported()) {
    glfwlog.error() << "Sorry, I don't support Vulkan!\n" << applog.abort();
  }
  app.window_handle = glfwCreateWindow(width, height, name.data(), NULL, NULL);

  glfwlog.ok() << "GLFW initialized\n";

  // setup vulkan
  applog.info() << "Setting up vulkan...\n";
  {
    u32 extensions_count = 0;
    const char **extensions =
        glfwGetRequiredInstanceExtensions(&extensions_count);

    app.vk = &vulkan::Instance::get_or_init(extensions, extensions_count);
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(app.vk->instance, app.window_handle, NULL,
                                &surface) < 0) {
      glfwlog.error() << "Could not create window surface\n" << applog.abort();
    }
    glfwlog.ok() << "Window surface created\n";

    // create frame buffers
    int width, height;
    glfwGetFramebufferSize(app.window_handle, &width, &height);
    setup_vulkan_window(*app.vk, &app.window, surface, width, height);
  }
  app.state.allocated_command_buffers.resize(app.window.ImageCount);
  app.state.resource_free_queue.resize(app.window.ImageCount);
  applog.ok() << "Vulkan setup\n";

  applog.info() << "Setting up ImGui...\n";

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                    ImGuiConfigFlags_DockingEnable |
                    ImGuiConfigFlags_ViewportsEnable;
  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForVulkan(app.window_handle, true);
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = app.vk->instance;
  init_info.PhysicalDevice = app.vk->physical_device;
  init_info.Device = app.vk->device;
  init_info.QueueFamily = app.vk->queue_family;
  init_info.Queue = app.vk->queue;
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = app.vk->descriptor_pool;
  init_info.Subpass = 0;
  init_info.MinImageCount = k_min_image_count;
  init_info.ImageCount = app.window.ImageCount;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = NULL;
  init_info.CheckVkResultFn = NULL;
  ImGui_ImplVulkan_Init(&init_info, app.window.RenderPass);

  applog.info() << "Setting up font...\n";

  // Load default font
  ImFontConfig fontConfig;
  fontConfig.FontDataOwnedByAtlas = false;
  ImFont *robotoFont = io.Fonts->AddFontFromMemoryTTF(
      (void *)g_RobotoRegular, sizeof(g_RobotoRegular), 20.0f, &fontConfig);
  io.FontDefault = robotoFont;

  // Upload Fonts
  {
    // Use any command queue
    VkCommandPool command_pool =
        app.window.Frames[app.window.FrameIndex].CommandPool;
    VkCommandBuffer command_buffer =
        app.window.Frames[app.window.FrameIndex].CommandBuffer;

    if (vkResetCommandPool(app.vk->device, command_pool, 0) < 0) {
      vklog.error() << "could not reset command pool\n" << vklog.abort();
    };
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) < 0) {
      vklog.error() << "could not setup command buffer\n" << vklog.abort();
    }

    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

    VkSubmitInfo end_info = {};
    end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    end_info.commandBufferCount = 1;
    end_info.pCommandBuffers = &command_buffer;
    if (vkEndCommandBuffer(command_buffer) < 0) {
      vklog.error() << "could not build command\n" << vklog.abort();
    }
    if (vkQueueSubmit(app.vk->queue, 1, &end_info, VK_NULL_HANDLE) < 0) {
      vklog.error() << "could not submit queue\n" << vklog.abort();
    }

    if (vkDeviceWaitIdle(app.vk->device) < 0) {
      vklog.error() << "cannot wait for device\n" << vklog.abort();
    }
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }
  applog.ok() << "Application setup finished.\n";
}

Application::~Application() {
  if (s_is_init) {
    applog.info() << "shutting down...\n";
    vkDeviceWaitIdle(vk->device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    cleanup_vulkan_window(*vk, &window);

    glfwDestroyWindow(window_handle);
    glfwTerminate();
  }
}

void Application::main_loop() {
  applog.info() << "Beginning main loop...\n";

  bool is_running = true;

  static constexpr ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  ImGuiIO &io = ImGui::GetIO();

  while (!glfwWindowShouldClose(window_handle) && is_running) {
    glfwPollEvents();

    if (state.rebuild_swapchain) {
      int width, height;
      glfwGetFramebufferSize(window_handle, &width, &height);
      if (width > 0 && height > 0) {
        ImGui_ImplVulkan_SetMinImageCount(k_min_image_count);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            vk->instance, vk->physical_device, vk->device, &window,
            vk->queue_family, NULL, width, height, k_min_image_count);
        window.FrameIndex = 0;
        state.allocated_command_buffers.clear();
        state.allocated_command_buffers.resize(window.ImageCount);
        state.rebuild_swapchain = false;
      }
    }

    // Start imgui's frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      static constexpr ImGuiDockNodeFlags dockspace_flags =
          ImGuiDockNodeFlags_None;

      // make the parent window not dockable into
      ImGuiWindowFlags window_flags =
          ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;

      const ImGuiViewport *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);
      ImGui::SetNextWindowViewport(viewport->ID);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      window_flags |= ImGuiWindowFlags_NoTitleBar |
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove;

      window_flags |=
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

      // when using Imguidocknodeflgs_pssthruCentralNode, Dockspace() will
      // render our background and handle handle the pass-htru hole, so we ask
      // begin() to not render a background
      if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

      // we proceed even if begin() returns false because if a dockspace() is
      // inactive, all active windows docked into it will lose their parent and
      // become undocked. we
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::Begin("DockSpace Demo", nullptr, window_flags);
      ImGui::PopStyleVar(3);

      // submit the dockspace
      if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        const auto dockspace_id = ImGui::GetID("VulkanAppDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
      }


      // menu bar, hardcoded
      if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
          if (ImGui::MenuItem("Exit"))
            is_running = false;
          ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
      }

      // TODO: render here.
      for (auto &layer : layers) {
        layer->on_ui_render();
      }

      // NOTE: this call results in an assertion error
      ImGui::End();
    }

    ImGui::Render();
    auto *main_draw_data = ImGui::GetDrawData();
    const auto main_is_minimized = main_draw_data->DisplaySize.x <= 0.0f ||
                                   main_draw_data->DisplaySize.y <= 0.0f;
    window.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
    window.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
    window.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
    window.ClearValue.color.float32[3] = clear_color.w;

    if (!main_is_minimized)
      frame_render(*vk, &window, main_draw_data, state);

    // update and render additional platform windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }

    if (!main_is_minimized)
      present_frame(*vk, &window, state);
  }

  applog.info() << "Main loop finished\n";
}

void Application::add_render_callback(std::unique_ptr<Layer> layer) {
  layers.emplace_back(std::move(layer));
}

void Application::submit_resource_free(std::function<void()> &&func) {
  state.resource_free_queue[state.current_frame_index].emplace_back(func);
}

std::vector<std::vector<VkCommandBuffer>>
    FrameRenderState::allocated_command_buffers;
std::vector<std::vector<std::function<void()>>>
    FrameRenderState::resource_free_queue;

VkPhysicalDevice Application::get_device() const noexcept {
  return vk->physical_device;
}

Application Application::s_instance;
bool Application::s_is_init = false;

Application &Application::init(u32 width, u32 height, std::string_view name) {
  if (s_is_init)
    throw std::logic_error("application has already been initialized");
  s_instance = Application(width, height, name);
  s_is_init = true;
  return s_instance;
}

Application &Application::get() {
  if (!s_is_init)
    throw std::logic_error("application has not been initialized");
  return s_instance;
}

VkCommandBuffer Application::get_command_buffer(bool begin) {
  VkCommandPool command_pool = window.Frames[window.FrameIndex].CommandPool;
  VkCommandBufferAllocateInfo cmd_alloc_info = {};
  cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_alloc_info.commandPool = command_pool;
  cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_alloc_info.commandBufferCount = 1;

  VkCommandBuffer &cmd =
      state.allocated_command_buffers[window.FrameIndex].emplace_back();
  vkAllocateCommandBuffers(vk->device, &cmd_alloc_info, &cmd);

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &begin_info) < 0) {
    vklog.error() << "Could not begin command buffer\n" << vklog.abort();
  }

  return cmd;
}
void Application::flush_cmd_buffer(VkCommandBuffer buffer) {
  const uint64_t DEFAULT_FENCE_TIMEOUT = 100000000000;

  VkSubmitInfo end_info = {};
  end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  end_info.commandBufferCount = 1;
  end_info.pCommandBuffers = &buffer;
  if (vkEndCommandBuffer(buffer) < 0) {
    vklog.error() << "Could not build command\n" << vklog.abort();
  }

  // Create fence to ensure that the command buffer has finished executing
  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = 0;
  VkFence fence;
  if (vkCreateFence(vk->device, &fenceCreateInfo, nullptr, &fence) < 0) {
    vklog.error() << "Could not create fence\n" << vklog.abort();
  }

  if (vkQueueSubmit(vk->queue, 1, &end_info, fence) < 0) {
    vklog.error() << "Could not submit queue\n" << vklog.abort();
  }

  if (vkWaitForFences(vk->device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT) <
      0) {
    vklog.error() << "Could not wait for fences\n" << vklog.abort();
  }

  vkDestroyFence(vk->device, fence, nullptr);
}

} // namespace app
