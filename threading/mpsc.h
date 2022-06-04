#pragma once
#include <atomic>

namespace renderer::threading {

// an mpsc queue
template <typename T> class mpsc_queue {
  T *buffer; // self-managed buffer
  size_t read_handle;
  size_t cap;
  std::atomic<size_t> write_handle;
  std::atomic<size_t> elem_count;

public:
  mpsc_queue(size_t capacity)
      : buffer((T *)operator new[](sizeof(T) * capacity)), read_handle(0),
        cap(capacity), write_handle(0), elem_count(0) {}

  ~mpsc_queue() { operator delete[](buffer); }

  T *try_pop() noexcept {
    const auto write = write_handle.load(std::memory_order_relaxed);
    if (write == read_handle) {
      return nullptr; // queue is full or empty.
    }
    T *ptr = &buffer[read_handle];
    read_handle = (read_handle + 1) % cap;
    elem_count.fetch_sub(1, std::memory_order_release);
    return ptr;
  }

  template <typename... Args> bool try_emplace(Args &&...args) {

    // queue is full.
    if (cap == elem_count.load(std::memory_order_acquire)) {
      return false;
    }
    elem_count.fetch_add(1, std::memory_order_acquire);

    // add 1 to ensure we get 'ownership' over that index (no one else can store
    // there)
    const auto write = write_handle.fetch_add(1, std::memory_order_acq_rel);

    new (&buffer[write]) T(std::forward<Args &&...>(args...));

    write_handle.store((write + 1) % cap, std::memory_order_release);

    return true;
  }

  template <typename... Args> void blocking_emplace(Args &&...args) {
    // block until queue is not empty
    while (cap == elem_count.load(std::memory_order_acquire))
      ;
    const auto write = write_handle.fetch_add(1, std::memory_order_acq_rel);
    new (&buffer[write]) T(std::forward<Args &&...>(args...));
  }

  bool try_push(T &&value) { return try_emplace(std::move(value)); }
  bool try_push(const T &value) { return try_emplace(value); }
};

} // namespace renderer::threading
