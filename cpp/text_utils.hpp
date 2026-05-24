#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace didzk {

std::string trim_copy(const std::string& s);
std::string lower_copy(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
bool parse_boolish(const std::string& s, bool def = false);
std::vector<std::string> split_csv_nonempty(const std::string& s);
std::string read_text_file(const std::filesystem::path& path);

}  // namespace didzk
