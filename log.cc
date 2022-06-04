#include "log.h"
#include <syncstream>

namespace utils {
std::ostream &operator<<(std::ostream &s, Log::Abort a) {
  s << "\x1b[1;38;5;1m[" << a.name << "]\x1b[m fatal error\n";
  s.flush();
  abort();
  return s;
}

std::osyncstream Log::ok() {
  return 
      std::move(std::osyncstream(static_cast<std::ostream&>(s_level <= Level::INFO ? stream << "\x1b[38;5;2m[" << name << "]\x1b[m "
                             : sink)));
}
std::osyncstream Log::info() {
  if (s_level > Level::INFO)
    return std::move(std::osyncstream(static_cast<std::ostream&>(sink)));
  return std::move(std::osyncstream(static_cast<std::ostream&>(stream << "\x1b[38;5;6m[" << name << "::info]\x1b[m ")));
}

std::osyncstream Log::error() {
  return std::move(std::osyncstream(static_cast<std::ostream&>(stream << "\x1b[38;5;1m[" << name << "::error]\x1b[m ")));
}

std::osyncstream Log::debug() {
  if (s_level > Level::DEBUG)
    return std::move(std::osyncstream(static_cast<std::ostream&>(sink)));
  return std::move(std::osyncstream(static_cast<std::ostream&>(stream << "\x1b[38;5;8m[" << name << "::debug]\x1b[m ")));
}

Log::Level Log::s_level;
std::mutex Log::s_mutex;
void Log::set_level(Log::Level level) { s_level = level; }

} // namespace utils
