#pragma once
#include <memory>

namespace utils::alloc {
struct free_deleter {
  void operator()(void *p) const { free(p); }
};

template <typename T>
class resize_enabled_array : public std::unique_ptr<T[], free_deleter> {
  using base_type = std::unique_ptr<T[], free_deleter>;

public:
  resize_enabled_array(std::nullptr_t ptr) : base_type(ptr) {}
  explicit resize_enabled_array(size_t size)
      : base_type(
            std::make_unique<T[], free_deleter>(new T[size], free_deleter{})) {}

  void resize(size_t new_capacity) {
    T *const old_ptr = ((base_type *)this)->release();
    T *const new_ptr = old_ptr == nullptr
                           ? (T *)malloc(new_capacity * sizeof(T))
                           : (T *)realloc(old_ptr, new_capacity * sizeof(T));
    if (new_ptr == nullptr)
      throw std::bad_alloc();
    ((base_type *)this)->reset(new_ptr);
  }
};
} // namespace utils::alloc
