#include "url_utils.hpp"

#include "text_utils.hpp"

#include <string>

UrlParts parse_url(const std::string& url) {
  UrlParts u;
  std::string s = didzk::trim_copy(url);
  if (didzk::starts_with(s, "http://")) {
    u.scheme = "http";
    s = s.substr(7);
    u.port = 80;
  } else if (didzk::starts_with(s, "https://")) {
    u.scheme = "https";
    s = s.substr(8);
    u.port = 443;
  }

  const auto slash = s.find('/');
  std::string hostport = s;
  if (slash != std::string::npos) {
    hostport = s.substr(0, slash);
    u.basePath = s.substr(slash);
  }

  const auto colon = hostport.find(':');
  if (colon == std::string::npos) {
    u.host = hostport.empty() ? "127.0.0.1" : hostport;
  } else {
    u.host = hostport.substr(0, colon);
    u.port = std::stoi(hostport.substr(colon + 1));
  }
  if (u.host.empty()) u.host = "127.0.0.1";
  return u;
}

std::string url_encode(const std::string& s) {
  static const char* hexd = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hexd[c >> 4]);
      out.push_back(hexd[c & 0x0f]);
    }
  }
  return out;
}

std::unordered_map<std::string, std::string> parse_semicolon_kv(const std::string& s) {
  std::unordered_map<std::string, std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t semi = s.find(';', i);
    const std::string kv = (semi == std::string::npos) ? s.substr(i) : s.substr(i, semi - i);
    const std::size_t eq = kv.find('=');
    if (eq != std::string::npos) {
      out[didzk::trim_copy(kv.substr(0, eq))] = didzk::trim_copy(kv.substr(eq + 1));
    }
    if (semi == std::string::npos) break;
    i = semi + 1;
  }
  return out;
}
