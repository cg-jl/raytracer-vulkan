#include "unique_signal.h"

namespace renderer::threading {

unique_signal::unique_signal() : toggled(false) {}
bool unique_signal::is_active() {
  if (toggled.load(std::memory_order_relaxed)) {
    toggled.store(false, std::memory_order_release);
    return true;
  } else {
    return false;
  }
}

void unique_signal::activate() {
  toggled.store(true, std::memory_order_release);
}

} // namespace renderer::threading
