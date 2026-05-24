#include "zk_public_signals.hpp"

#include "json_utils.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace didzk {
namespace {
namespace fs = std::filesystem;
}

std::vector<std::string> load_public_signals(const std::string& publicJsonPath) {
  const nlohmann::json j = load_json_file(fs::path(publicJsonPath));
  if (!j.is_array()) {
    throw std::runtime_error("public_json_not_array: " + publicJsonPath);
  }

  std::vector<std::string> out;
  out.reserve(j.size());
  for (const auto& item : j) {
    if (item.is_string()) {
      out.push_back(item.get<std::string>());
    } else if (item.is_number_integer() || item.is_number_unsigned()) {
      out.push_back(std::to_string(item.get<long long>()));
    } else if (item.is_number_float()) {
      out.push_back(std::to_string(static_cast<long long>(item.get<double>())));
    } else {
      throw std::runtime_error("bad_public_signal_item_type");
    }
  }
  return out;
}

PublicSignalView parse_public_signals(const std::vector<std::string>& signals) {
  PublicSignalView v;
  v.raw = signals;
  if (signals.size() < 5) {
    throw std::runtime_error("public_signal_count_too_small");
  }

  // snarkjs exports public inputs first, then public outputs.
  // Current project circuit order is:
  //   [root, ctxHash, sessPkHash, epoch, nullifier, bindHash?, leaf?]
  v.root = signals.at(0);
  v.ctxHash = signals.at(1);
  v.sessPkHash = signals.at(2);
  v.epoch = signals.at(3);
  v.nullifier = signals.at(4);
  return v;
}

std::size_t file_size_bytes(const std::string& path) {
  std::error_code ec;
  const auto size = fs::file_size(fs::path(path), ec);
  if (ec) {
    throw std::runtime_error("file_size_failed: " + path);
  }
  return static_cast<std::size_t>(size);
}

}  // namespace didzk
