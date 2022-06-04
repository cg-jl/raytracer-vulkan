#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <syncstream>

namespace utils {

struct sink_ostream : private std::streambuf, public std::ostream {
  sink_ostream() : std::ostream(this) {}

private:
  int overflow(int c) override { return 0; }
};



struct Log {



  enum class Level { DEBUG, INFO, WARN, ERROR };
  static Level s_level;
  static std::mutex s_mutex; // TODO: make this mutex per log reference instance (different instances must have their own locks)
  std::string name;
  std::ostream &stream = std::cerr;
  sink_ostream sink;

  Log(std::string name)
      : name(std::move(name)) {}


  std::osyncstream ok();
  std::osyncstream info();
  std::osyncstream warn();
  std::osyncstream error(); 
  std::osyncstream debug();

  static void set_level(Level level);

  struct Abort {
    std::string_view name;

  public:
    Abort(std::string_view name) : name(name) {}
  };

  Abort abort() const { return Abort{std::string_view(name)}; }
};

std::ostream &operator<<(std::ostream &, Log::Abort);

} // namespace utils
