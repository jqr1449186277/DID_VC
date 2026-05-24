#include "text_utils.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace didzk {

std::string trim_copy(const std::string& s) {
  std::size_t first = 0;
  while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
    ++first;
  }
  std::size_t last = s.size();
  while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
    --last;
  }
  return s.substr(first, last - first);
}

std::string lower_copy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parse_boolish(const std::string& s, bool def) {
  const std::string v = lower_copy(trim_copy(s));
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return def;
}

std::vector<std::string> split_csv_nonempty(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const std::size_t comma = s.find(',', start);
    const std::string item = trim_copy(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!item.empty()) out.push_back(item);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("open_failed: " + path.string());
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

}  // namespace didzk
