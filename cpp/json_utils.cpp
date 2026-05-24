#include "json_utils.hpp"

#include "text_utils.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace didzk {

const nlohmann::json* json_object_field(const nlohmann::json& v, const std::string& key) {
  if (!v.is_object()) return nullptr;
  auto it = v.find(key);
  if (it == v.end()) return nullptr;
  return &(*it);
}

std::string json_string_field(const nlohmann::json& v,
                              const std::string& key,
                              const std::string& def) {
  const nlohmann::json* p = json_object_field(v, key);
  if (!p || p->is_null()) return def;
  if (p->is_string()) return p->get<std::string>();
  if (p->is_boolean()) return p->get<bool>() ? "true" : "false";
  if (p->is_number_integer() || p->is_number_unsigned()) return p->dump();
  if (p->is_number_float()) {
    std::ostringstream oss;
    oss << std::setprecision(17) << p->get<double>();
    return oss.str();
  }
  return def;
}

std::uint64_t json_u64_field(const nlohmann::json& v,
                             const std::string& key,
                             std::uint64_t def) {
  const nlohmann::json* p = json_object_field(v, key);
  if (!p || p->is_null()) return def;
  try {
    if (p->is_number_unsigned()) return p->get<std::uint64_t>();
    if (p->is_number_integer()) {
      const auto value = p->get<long long>();
      return value < 0 ? def : static_cast<std::uint64_t>(value);
    }
    if (p->is_string() && !p->get<std::string>().empty()) {
      return static_cast<std::uint64_t>(std::stoull(p->get<std::string>()));
    }
  } catch (const std::exception&) {
  }
  return def;
}

std::uint32_t json_u32_field(const nlohmann::json& v,
                             const std::string& key,
                             std::uint32_t def) {
  const std::uint64_t value = json_u64_field(v, key, def);
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) return def;
  return static_cast<std::uint32_t>(value);
}

bool json_bool_field(const nlohmann::json& v, const std::string& key, bool def) {
  const nlohmann::json* p = json_object_field(v, key);
  if (!p || p->is_null()) return def;
  if (p->is_boolean()) return p->get<bool>();
  if (p->is_number()) return p->get<double>() != 0.0;
  if (p->is_string()) return parse_boolish(p->get<std::string>(), def);
  return def;
}

nlohmann::json load_json_file(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) throw std::runtime_error("open_json_failed: " + path.string());
  return nlohmann::json::parse(ifs);
}

void save_json_pretty(const std::filesystem::path& path, const nlohmann::json& j) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) throw std::runtime_error("open_write_failed: " + path.string());
  ofs << j.dump(2) << "\n";
  ofs.close();
  if (!ofs) throw std::runtime_error("write_failed: " + path.string());
}

}  // namespace didzk
