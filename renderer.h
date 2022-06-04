#pragma once
#include "resize_enabled_array.h"
#include "threading/mpsc.h"
#include "threading/unique_signal.h"
#include "log.h"
#include "log.h"
#include "types.h"
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace renderer {

class Timer {
  std::chrono::time_point<std::chrono::high_resolution_clock> start;

public:
  Timer() { reset(); }

  void reset() noexcept { start = std::chrono::high_resolution_clock::now(); }
  double millis() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - start)
               .count() *
           0.001 * 0.001;
  }
};
namespace ray_tracer {

using vec3 = glm::highp_dvec3;

struct material_traits;

struct Ray {
  vec3 origin;
  vec3 direction; // normalized
  vec3 at(double t) const noexcept;
};
struct Hit;

struct Sphere {
  vec3 center;
  double radius;

  bool intersect(Ray ray, Hit &hit) const noexcept;
};
struct World {
  std::vector<std::unique_ptr<material_traits>> materials;
  std::vector<std::pair<Sphere, size_t>> spheres;
  size_t create_material(std::unique_ptr<material_traits> mat) noexcept;
  const material_traits &material_at(size_t index) const noexcept;
  void add(Sphere sphere, size_t material) noexcept;
  bool intersect(Ray ray, Hit &hit) const noexcept;
  std::pair<vec3, vec3> scatter(vec3 direction, Hit &hit_info,
                                std::mt19937 &rand) const noexcept;
};

} // namespace ray_tracer
struct RenderResult;
// TODO: fill this lol
struct RenderRequest {
  u32 *start;
  u32 *end;
  size_t starting_index;
  size_t width;
  size_t height;
  double virtual_viewport_width;
  double virtual_viewport_height;
  const ray_tracer::World &world_view;
};

struct QuitSignal;

// object the main thread will use to manage its workers
class WorkerThread {
  threading::mpsc_queue<RenderResult>
      &results; // reference to the queue in main thread to launch
  threading::mpsc_queue<QuitSignal> quit;
  std::optional<std::thread> handle;
  utils::Log logger;
  size_t worker_id;

public:
  WorkerThread(size_t id, threading::mpsc_queue<RenderResult> &results);
  void cancel();
  void launch(RenderRequest request);
  ~WorkerThread();
};

class MainRenderThread {
  WorkerThread *threads = nullptr; // managed manually
  utils::alloc::resize_enabled_array<u32> data = nullptr;
  threading::mpsc_queue<RenderResult> results;
  double virtual_viewport_width;
  double virtual_viewport_height;
  ray_tracer::World world;
  size_t jobs_left = 0;
  Timer timer;
  double last_render_time;

public:
  MainRenderThread();
  void on_resize(size_t width, size_t height);
  // returns whether the data buffer could be updated
  bool on_frame_update();
  double get_last_render_time() const noexcept;
  const u32 *get_data() const noexcept;
  ~MainRenderThread();
};

} // namespace renderer
