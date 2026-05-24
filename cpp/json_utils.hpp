#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace didzk {

const nlohmann::json* json_object_field(const nlohmann::json& v, const std::string& key);
std::string json_string_field(const nlohmann::json& v,
                              const std::string& key,
                              const std::string& def = std::string());
std::uint64_t json_u64_field(const nlohmann::json& v,
                             const std::string& key,
                             std::uint64_t def = 0);
std::uint32_t json_u32_field(const nlohmann::json& v,
                             const std::string& key,
                             std::uint32_t def = 0);
bool json_bool_field(const nlohmann::json& v, const std::string& key, bool def = false);

nlohmann::json load_json_file(const std::filesystem::path& path);
void save_json_pretty(const std::filesystem::path& path, const nlohmann::json& j);

}  // namespace didzk
