#pragma once

#include <string>

#include <nlohmann/json.hpp>

bool http_get_text(const std::string& baseUrl,
                   const std::string& pathAndQuery,
                   std::string& outBody,
                   int timeoutMs);
bool http_post_json(const std::string& baseUrl,
                    const std::string& path,
                    const nlohmann::json& body,
                    std::string& outBody,
                    int timeoutMs);
bool http_post_json_url(const std::string& fullUrl,
                        const nlohmann::json& body,
                        std::string& outBody,
                        int timeoutMs);
