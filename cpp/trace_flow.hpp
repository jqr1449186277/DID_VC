#pragma once

#include "ttss_flow.hpp"

std::string load_pirate_box_for_trace(const Args& args, const nlohmann::json& setupJson, const std::vector<didzk::ShareEnvelope>& leakedShares, const std::filesystem::path& workDir);
int run_ttss_trace(const Args& args);
int run_ttss_trace_publish(const Args& args);
