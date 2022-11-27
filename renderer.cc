#include "renderer.h"
#include "log.h"
#include <cstring>
#include <glm/glm.hpp>
#include <random>
#include <sstream>

static utils::Log mainlog("renderer");

namespace renderer {
using vec3 = glm::highp_dvec3;
using color = vec3;
namespace utils::random {
namespace {
static std::uniform_int_distribution<u64> s_distribution;
} // namespace
static void init(std::mt19937 &engine) { engine.seed(std::random_device()()); }

static u64 next_u64(std::mt19937 &engine) { return s_distribution(engine); }

static double next_double(std::mt19937 &engine) {
  return double(next_u64(engine)) / double(std::numeric_limits<u64>::max());
}

static vec3 next_vec(std::mt19937 &engine) {
  return vec3{next_double(engine), next_double(engine), next_double(engine)};
}

} // namespace utils::random

namespace ray_tracer {
vec3 Ray::at(double t) const noexcept { return origin + t * direction; }

struct material_traits {
  virtual ~material_traits() {}
  virtual std::pair<vec3, vec3> scatter(vec3 ray_direction, Hit hit,
                                        std::mt19937 &rand) const noexcept = 0;
};
struct Hit {
  vec3 point = vec3(0.0);
  vec3 normal = vec3(0.0);
  double selected_t = 0;
  size_t mat_index = 0;
  bool front_face = true;

  void make_facing_outwards(const Ray &ray) {
    if (glm::dot(ray.direction, normal) < 0.0f) {
      front_face = false;
      normal *= -1.0f;
    }
  }
};

bool Sphere::intersect(Ray ray, Hit &hit) const noexcept {
  const Sphere &sphere = *this;
  const auto ca = ray.origin - sphere.center;
  const auto c = glm::dot(ca, ca) - sphere.radius * sphere.radius;
  const auto h = glm::dot(ca, ray.direction);
  const auto discriminant = h * h - c;
  // cannot solve the square root
  if (discriminant < 0.0)
    return false;

  // compute the two solutions
  const auto dsqrt = std::sqrt(discriminant);
  const auto t1 = -h + dsqrt;
  const auto t2 = -h - dsqrt;
  // return the lowest positive value
  auto t = t1;
  if (t < 0.0 || std::abs(t1) > std::abs(t2)) {
    if (t2 < 0.0)
      return false;
    t = t2;
  }
  // NOTE: the ray doesn't hit if it's tangent.
  if (t <= 0.0001)
    return false;

  const auto point = ray.at(t);
  const auto normal = glm::normalize(point - sphere.center);
  hit = Hit{point, normal, t};
  return true;
}

static vec3 random_in_unit_sphere(std::mt19937 &engine) {
  while (true) {
    const auto p = utils::random::next_vec(engine);
    if (glm::dot(p, p) >= 1.0)
      continue;
    return p;
  }
}
static vec3 random_in_hemisphere(vec3 normal, std::mt19937 &engine) {
  const auto p = random_in_unit_sphere(engine);
  return glm::dot(normal, p) < 0.0 ? -p : p;
}
static vec3 as_background(Ray ray) {
  const auto t = 0.5f * (ray.direction.y + 1.0f);
  return (1.0f - t) * vec3(1.0f, 1.0f, 1.0f) + t * vec3(0.5, 0.7, 1.0);
}

size_t World::create_material(std::unique_ptr<material_traits> mat) noexcept {
  const auto index = materials.size();
  materials.push_back(std::move(mat));
  return index;
}

const material_traits &World::material_at(size_t index) const noexcept {
  return *materials[index].get();
}

void World::add(Sphere sphere, size_t material) noexcept {
  spheres.emplace_back(std::move(sphere), material);
}

std::pair<color, vec3> World::scatter(vec3 ray_direction, Hit &record,
                                      std::mt19937 &rand) const noexcept {
  return material_at(record.mat_index)
      .scatter(ray_direction, std::move(record), rand);
}

bool World::intersect(Ray ray, Hit &hit) const noexcept {
  hit.selected_t = std::numeric_limits<double>::infinity();
  Hit temp_hit;
  bool did_hit = false;
  for (const auto &[sphere, mat_index] : spheres) {
    if (sphere.intersect(ray, temp_hit)) {
      did_hit = true;
      temp_hit.mat_index = mat_index;
      if (temp_hit.selected_t < hit.selected_t)
        hit = temp_hit;
    }
  }

  return did_hit;
}

static vec3 ray_color(Ray ray, const World &world, uint32_t max_depth,
                      std::mt19937 &rand) {
  Hit hit;

  // we multiply the colors as we go. The 'real' operation is in reverse order,
  // but since it's multiplication the order of the operation doesn't matter, so
  // we can reduce forward.
  color current(1.0);

  for (; max_depth && world.intersect(ray, hit); --max_depth) {
    ray.origin = hit.point;
    auto [attenuation, direction] =
        world.scatter(std::move(ray.direction), hit, rand);
    ray.direction = direction;
    if (attenuation == vec3(0.0)) {
      return vec3(0.0); // reducing isn't an option here. We can break and
                        // return black.
    }
    current *= attenuation;
  }

  if (max_depth == 0) {
    return vec3(0.0); // assume shadow
  }

  return current * as_background(ray);
}
static Ray ray_at(double u, double v, double viewport_width,
                  double viewport_height) noexcept {
  // middle of the screen is 0,0.
  const auto uv_origin = vec3(0.0, 0.0, 0.0);
  // u,v in [0, 1] range. We translate them to [-0.5, 0.5] range
  const auto uv_place =
      vec3((u - 0.5) * viewport_width, (v - 0.5) * viewport_height, -1.0);
  return Ray{uv_origin, glm::normalize(uv_place - uv_origin)};
}
struct lambertian : public material_traits {
  color albedo;

  constexpr lambertian(color albedo) : albedo(std::move(albedo)) {}

  virtual std::pair<color, vec3>
  scatter(vec3 ray_direction, Hit record,
          std::mt19937 &rand) const noexcept override {
    auto direction = glm::normalize(record.normal +
                                    random_in_hemisphere(record.normal, rand));
    return {albedo, direction};
  }
};

struct metal : public material_traits {
  color albedo;
  double fuzz;

  constexpr metal(color albedo, double fuzz)
      : albedo(std::move(albedo)), fuzz(fuzz < 1 ? fuzz : 1) {}

  virtual std::pair<color, vec3>
  scatter(vec3 ray_direction, Hit record,
          std::mt19937 &rand) const noexcept override {
    const auto reflected =
        reflect(ray_direction, record.normal) +
        fuzz * glm::normalize(random_in_hemisphere(record.normal, rand));
    // only reflect if the resulting reflected ray is above the normal.
    const auto attenuation =
        glm::dot(reflected, record.normal) > 0 ? albedo : vec3(0.0);
    return {attenuation, reflected};
  }
};
static vec3 refract(const vec3 v, const vec3 n, glm::f64 refraction_ratio) {
  const auto nndotv = n * glm::dot(-v, n);
  const auto s = nndotv + v;
  const auto v_perp = s / refraction_ratio;
  const auto v_parallel = -nndotv;
  return v_perp + v_parallel;
}
struct dielectric : public material_traits {
  double refraction_index;

  constexpr dielectric(double refraction_index)
      : refraction_index(refraction_index) {}

  virtual std::pair<color, vec3>
  scatter(vec3 ray_direction, Hit record,
          std::mt19937 &rand) const noexcept override {
    const auto refraction_ratio =
        record.front_face ? 1.0 / refraction_index : refraction_index;
    const auto cos_theta =
        glm::min(glm::dot(-ray_direction, record.normal), 1.0);
    const auto sin_theta = glm::sqrt(glm::abs(1.0 - cos_theta * cos_theta));
    const auto cannot_refract = sin_theta * refraction_ratio > 1.0;
    const auto reflectance =
        dielectric::reflectance(cos_theta, refraction_index);
    if (cannot_refract || reflectance > utils::random::next_double(rand)) {
      const auto reflected = reflect(ray_direction, record.normal);
      return {vec3(1.0), reflected};
    } else {
      const auto refracted =
          refract(ray_direction, record.normal, refraction_ratio);
      return {vec3(1.0), refracted};
    }
  }

private:
  static glm::f64 reflectance(glm::f64 cosine, glm::f64 refraction_index) {
    // Use Schlick's approximation for reflectance.
    auto r0 = (1.0 - refraction_index) / (1.0 + refraction_index);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * glm::pow(1.0 - cosine, 5);
  }
};
} // namespace ray_tracer

static constexpr size_t NUM_THREADS = 12;
static constexpr size_t BLOCK_SIZE = 16;
static constexpr size_t SAMPLES_PER_PIXEL = 100;

static u8 make_channel_integer(double ch) {
  return static_cast<u8>(glm::clamp(ch * 255.999, 0.0, 255.999));
}

static u32 to_abgr(vec3 color) {
  const auto r = static_cast<u32>(make_channel_integer(color.r));
  const auto g = static_cast<u32>(make_channel_integer(color.g));
  const auto b = static_cast<u32>(make_channel_integer(color.b));

  return 0xff << 24 | u32(b) << 16 | u32(g) << 8 | u32(r);
}
struct RenderResult {
  size_t worker_id;
};

static vec3 color_at(double u, double v, double viewport_width,
                     double viewport_height, const ray_tracer::World &world,
                     std::mt19937 &random_engine) {
  // this should do the ray tracing lol
  const auto ray = ray_tracer::ray_at(u, v, viewport_width, viewport_height);
  return ray_tracer::ray_color(ray, world, 50, random_engine);
}

WorkerThread::WorkerThread(size_t id,
                           threading::mpsc_queue<RenderResult> &results,
                           bool const &cancel)
    : results(results),
      logger((std::ostringstream() << "renderer::worker{" << id << '}').str()),

      cancel(cancel), worker_id(id) {}

void WorkerThread::launch(RenderRequest request) {
  logger.info() << "Received render request!\n";
  auto &sender = results;
  auto const &signal = this->cancel;
  handle = std::thread([request, id = worker_id, &signal, &sender,
                        &workerlog = logger]() {
    std::mt19937 rand;
    utils::random::init(rand);
    for (auto *block_start = request.start; block_start < request.end;
         block_start += BLOCK_SIZE * NUM_THREADS) {
      const auto block_end = std::min(&block_start[BLOCK_SIZE], request.end);
      for (auto *current = block_start; current != block_end; ++current) {
        const auto index = current - request.start + request.starting_index;
        const auto i = index % request.width;
        const auto j = request.height - (index / request.width);
        vec3 color(0.0);
        for (size_t sample = 0; sample != SAMPLES_PER_PIXEL; ++sample) {
          const auto u =
              (i + utils::random::next_double(rand)) / (request.width - 1);
          const auto v =
              (j + utils::random::next_double(rand)) / (request.height - 1);
          color += color_at(u, v, request.virtual_viewport_width,
                            request.virtual_viewport_height, request.world_view,
                            rand);
        }
        *current = to_abgr(color / static_cast<double>(SAMPLES_PER_PIXEL));
      }
      if (signal) {
        workerlog.debug() << "Cancelling job!\n";
        return;
      }
    }
    // we've finished. Send a signal and spin until we're given a quit
    // message
    sender.blocking_emplace(RenderResult{id});
    workerlog.debug() << "Emplaced result. Quitting...\n";
  });
}

void WorkerThread::drop_thread() {
  if (handle) {
    handle->join();
    handle.reset();
  }
}
WorkerThread::~WorkerThread() { drop_thread(); }

MainRenderThread::MainRenderThread() : results(NUM_THREADS) {
  // initialize workers in idle state
  threads = (WorkerThread *)operator new[](sizeof(WorkerThread) * NUM_THREADS);
  for (size_t i = 0; i != NUM_THREADS; ++i) {
    new (&threads[i]) WorkerThread(i, results, this->cancel_signal);
  }
  virtual_viewport_width = 2.0;

  // world
  const auto sphere_mat = world.create_material(
      std::make_unique<ray_tracer::lambertian>(color(0.1, 0.3, 0.5)));
  const auto floor = world.create_material(
      std::make_unique<ray_tracer::lambertian>(color(0.5)));
  world.add(ray_tracer::Sphere{vec3(0.0, 0.0, -1.0), 0.5}, sphere_mat);
  world.add(ray_tracer::Sphere{vec3(0.0, -100.5, -1.0), 100.0}, floor);
}

void MainRenderThread::stop_pipeline() {
  mainlog.debug() << "Stopping pipeline, waiting for threads to join...\n";
  cancel_signal = true;
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    threads[i].drop_thread();
  }
  cancel_signal = false;
}

void MainRenderThread::on_resize(size_t width, size_t height) {
  virtual_viewport_height = virtual_viewport_width * height / width;
  mainlog.debug() << "Resized virtual viewport to " << virtual_viewport_width
                  << 'x' << virtual_viewport_height << '\n';
  mainlog.debug() << "Resized viewport to " << width << 'x' << height << '\n';
  // cancel the pipeline because I'm going to allocate the data array
  stop_pipeline();

  // do the resizing
  data.resize(width * height);

  // fill with zero
  std::memset(data.get(), 0, width * height * sizeof(u32));

  // launch the threads
  auto *const end = data.get() + width * height;
  for (size_t i = 0; i != NUM_THREADS; ++i) {
    threads[i].launch(RenderRequest{&data[i * BLOCK_SIZE], end, i * BLOCK_SIZE,
                                    width, height, virtual_viewport_width,
                                    virtual_viewport_height, world});
  }
  jobs_left = NUM_THREADS;
  timer.reset();
}

bool MainRenderThread::on_frame_update() {
  if (jobs_left) {
    for (const RenderResult *res = nullptr;
         jobs_left && (res = results.try_pop()); --jobs_left) {
      // Wait for thread to quit.
      threads[res->worker_id].drop_thread();
    }
    if (!jobs_left) {
      last_render_time = timer.millis();
      mainlog.info() << "Render finished after " << last_render_time << "ms\n";
    }
    return true;
  }
  // nothing running.
  return false;
}

const u32 *MainRenderThread::get_data() const noexcept { return data.get(); }
double MainRenderThread::get_last_render_time() const noexcept {
  return last_render_time;
}

MainRenderThread::~MainRenderThread() {
  if (jobs_left)
    stop_pipeline();
  for (size_t i = 0; i != NUM_THREADS; ++i) {
    threads[i].~WorkerThread();
  }
  operator delete[](threads);
}

} // namespace renderer
