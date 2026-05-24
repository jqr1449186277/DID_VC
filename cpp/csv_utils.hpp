#pragma once

#include "did_app_types.hpp"

#include <string>

void ensure_csv_header(const std::string& csvPath);
std::string csv_escape(const std::string& s);
void append_csv_row(const std::string& csvPath, const RunRow& row);
