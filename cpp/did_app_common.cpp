#include "did_app_common.hpp"

#include <cstdlib>
#include <ctime>
#include <iostream>

namespace {
using Clock = AppClock;
}

bool poseidon_debug_enabled() {
  const char* v = std::getenv("DIDZK_DEBUG_POSEIDON");
  return v != nullptr && std::string(v) != "0" && std::string(v) != "false" && std::string(v) != "FALSE";
}

void poseidon_debug_log(const std::string& msg) {
  if (poseidon_debug_enabled()) {
    std::cerr << "[didzk_poseidon] " << msg << std::endl;
  }
}

std::string now_stamp_compact() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buf[32] = {0};
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
  return std::string(buf);
}

double ms_between(const Clock::time_point& a, const Clock::time_point& b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

std::int64_t elapsed_ms_i64(const Clock::time_point& start, const Clock::time_point& end) {
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
}

std::int64_t elapsed_ms_i64(const Clock::time_point& start) {
  return elapsed_ms_i64(start, Clock::now());
}
