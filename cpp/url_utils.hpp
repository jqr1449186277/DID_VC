#pragma once

#include "did_app_types.hpp"

#include <string>
#include <unordered_map>

UrlParts parse_url(const std::string& url);
std::string url_encode(const std::string& s);
std::unordered_map<std::string, std::string> parse_semicolon_kv(const std::string& s);
