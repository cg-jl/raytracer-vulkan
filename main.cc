#include "application.h"
#include "image.h"
#include "log.h"
#include "renderer.h"
#include "resize_enabled_array.h"
#include "types.h"
#include <concepts>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <random>

using vec3 = glm::highp_dvec3;

utils::Log vklog("vulkan");

utils::Log renderlog("main");

class RendererLayer : public vulkan::Layer {
private:
  std::unique_ptr<vulkan::utils::Image> image = nullptr;
  u32 viewport_width = 0, viewport_height = 0;
  renderer::MainRenderThread renderer;

public:
  void on_ui_render() {

    ImGui::Begin("Settings");
    if (ImGui::Button("Render")) {
      if (!image || viewport_width != image->get_width() ||
          viewport_height != image->get_height()) {
        renderlog.info() << "Viewport resized to " << viewport_width << 'x'
                         << viewport_height << '\n';
        // reallocate image
        image = std::make_unique<vulkan::utils::Image>(
            viewport_width, viewport_height, nullptr);
      }
      renderer.on_resize(viewport_width, viewport_height);
      image->set_data(renderer.get_data());
    }
    ImGui::End();

    // update the image
    if (renderer.on_frame_update()) {
      image->set_data(renderer.get_data());
    }

    // remove ugly border
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    // update the viewport width/height
    viewport_width = static_cast<u32>(ImGui::GetContentRegionAvail().x);
    viewport_height = static_cast<u32>(ImGui::GetContentRegionAvail().y);
    if (image) {
      ImGui::Image(image->get_descriptor_set(),
                   {(float)viewport_width, (float)viewport_height});
    }
    // TODO: image
    ImGui::End();
    ImGui::PopStyleVar();
  }
};

std::ostream &operator<<(std::ostream &s, const vec3 &v) {
  return s << '[' << v.x << ' ' << v.y << ' ' << v.z << ']';
}

int main() {

  utils::Log::set_level(utils::Log::Level::DEBUG);
  auto &app = vulkan::Application::init(800, 600, "test");
  app.add_render_callback(std::make_unique<RendererLayer>());
  app.main_loop();
}
