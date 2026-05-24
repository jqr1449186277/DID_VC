#include "app_paths.hpp"

#include "did_app_common.hpp"
#include "text_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

std::string default_project_root(const Args& args) {
  if (!didzk::trim_copy(args.projectRoot).empty()) return args.projectRoot;
  return didzk::detect_project_root("");
}

std::string default_base_workdir(const Args& args, const std::string& mode) {
  if (!didzk::trim_copy(args.workDir).empty()) return args.workDir;
  fs::path root(default_project_root(args));
  fs::path out = root / "results" / (now_stamp_compact() + "_" + mode);
  return out.string();
}

std::vector<std::uint32_t> parse_guardian_indexes_csv(const std::string& s) {
  std::vector<std::uint32_t> out;
  for (const std::string& item : didzk::split_csv_nonempty(s)) {
    const int v = std::stoi(item);
    if (v <= 0) throw std::runtime_error("guardian_index_must_be_positive");
    const std::uint32_t idx = static_cast<std::uint32_t>(v);
    if (std::find(out.begin(), out.end(), idx) == out.end()) out.push_back(idx);
  }
  return out;
}

std::vector<std::uint32_t> default_leaked_guardian_indexes(int n, int t) {
  const int target = std::max(1, t - 1);
  std::vector<std::uint32_t> out;
  out.reserve(static_cast<std::size_t>(target));
  for (int i = 2; i <= n && static_cast<int>(out.size()) < target; i += 2) {
    out.push_back(static_cast<std::uint32_t>(i));
  }
  for (int i = 1; i <= n && static_cast<int>(out.size()) < target; ++i) {
    const std::uint32_t idx = static_cast<std::uint32_t>(i);
    if (std::find(out.begin(), out.end(), idx) == out.end()) out.push_back(idx);
  }
  return out;
}

std::string join_urls_csv(const std::vector<std::string>& urls) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < urls.size(); ++i) {
    if (i) oss << ',';
    oss << urls[i];
  }
  return oss.str();
}

std::vector<std::string> committee_urls_from_setup_or_args(const nlohmann::json& setupJson,
                                                           const Args& args) {
  if (!didzk::trim_copy(args.committeeUrls).empty()) {
    return didzk::split_csv_nonempty(args.committeeUrls);
  }
  if (!setupJson.contains("committeeUrls") || !setupJson.at("committeeUrls").is_array()) {
    throw std::runtime_error("committeeUrls_missing_from_ttss_state");
  }
  return setupJson.at("committeeUrls").get<std::vector<std::string>>();
}

fs::path resolve_vk_file_path(const nlohmann::json&, const Args& args) {
  if (!didzk::trim_copy(args.vkFile).empty()) return fs::path(args.vkFile);
  const fs::path candidate = fs::path(args.ttssStatePath).parent_path() / "ttss_vk.json";
  if (fs::exists(candidate)) return candidate;
  throw std::runtime_error("vk_file_not_found_next_to_ttss_state");
}

std::string find_id_hash_from_setup(const nlohmann::json& setupJson) {
  const std::string idHash = setupJson.value("idHash", std::string());
  if (didzk::trim_copy(idHash).empty()) throw std::runtime_error("idHash_missing_from_ttss_state");
  return idHash;
}

std::string run_id_for_index(const Args& args, int i) {
  if (args.runs <= 1) return args.id;
  std::ostringstream oss;
  oss << args.id << "_" << std::setw(4) << std::setfill('0') << i;
  return oss.str();
}
