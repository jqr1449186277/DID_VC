#pragma once

#include "did_app_types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

std::string default_project_root(const Args& args);
std::string default_base_workdir(const Args& args, const std::string& mode);
std::vector<std::uint32_t> parse_guardian_indexes_csv(const std::string& s);
std::vector<std::uint32_t> default_leaked_guardian_indexes(int n, int t);
std::string join_urls_csv(const std::vector<std::string>& urls);
std::vector<std::string> committee_urls_from_setup_or_args(const nlohmann::json& setupJson, const Args& args);
std::filesystem::path resolve_vk_file_path(const nlohmann::json& setupJson, const Args& args);
std::string find_id_hash_from_setup(const nlohmann::json& setupJson);
std::string run_id_for_index(const Args& args, int i);
