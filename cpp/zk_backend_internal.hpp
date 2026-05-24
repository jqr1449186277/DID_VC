#pragma once

#include <chrono>
#include <string>

namespace didzk::zk_backend_internal {

using Clock = std::chrono::steady_clock;

bool file_exists(const std::string& path);
double duration_ms(const Clock::time_point& start, const Clock::time_point& end);
void ensure_parent_dir(const std::string& path);
void ensure_file_exists(const std::string& path, const std::string& label);

}  // namespace didzk::zk_backend_internal
