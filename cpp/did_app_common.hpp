#pragma once

#include "did_app_types.hpp"
#include "json_utils.hpp"
#include "text_utils.hpp"

#include "app_crypto.hpp"
#include "app_paths.hpp"
#include "csv_utils.hpp"
#include "identity_state.hpp"
#include "url_utils.hpp"

using AppClock = std::chrono::steady_clock;

void poseidon_debug_log(const std::string& msg);
using didzk::load_json_file;
using didzk::save_json_pretty;
using didzk::starts_with;
using didzk::trim_copy;

std::string now_stamp_compact();
double ms_between(const AppClock::time_point& a, const AppClock::time_point& b);
std::int64_t elapsed_ms_i64(const AppClock::time_point& start);
std::int64_t elapsed_ms_i64(const AppClock::time_point& start, const AppClock::time_point& end);
