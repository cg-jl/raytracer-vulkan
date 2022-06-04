#pragma once
#include <atomic>

namespace renderer::threading {

// a way to signal something to one thread, without any data.
class unique_signal {
  // avoid false sharing with other stuff
  alignas(64) std::atomic<bool> toggled;

public:
  unique_signal();
  bool is_active();
  void activate();
};
} // namespace renderer::utils
