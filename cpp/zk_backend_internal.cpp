#include "zk_backend_internal.hpp"

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace didzk::zk_backend_internal {
namespace fs = std::filesystem;

bool file_exists(const std::string& path) {
  if (path.empty()) return false;
  std::error_code ec;
  return fs::exists(fs::path(path), ec);
}

double duration_ms(const Clock::time_point& start, const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

void ensure_parent_dir(const std::string& path) {
  const fs::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
      throw std::runtime_error("create_directories_failed: " + p.parent_path().string());
    }
  }
}

void ensure_file_exists(const std::string& path, const std::string& label) {
  if (!file_exists(path)) {
    throw std::runtime_error("missing_" + label + ": " + path);
  }
}

}  // namespace didzk::zk_backend_internal
