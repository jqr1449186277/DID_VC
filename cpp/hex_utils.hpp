#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace didzk {

bool starts_with_0x(const std::string& s);
int hex_value(char c);

bool is_hex_string(const std::string& s, bool require_prefix = true);
std::string ensure_0x_hex(const std::string& s);
std::string normalize_hex_bytes(const std::string& s, std::size_t byte_len);
std::string normalize_digest_hex32(const std::string& s);

std::vector<std::uint8_t> hex_to_bytes(const std::string& s);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes, bool with_prefix = true);
std::string bytes_to_hex(const unsigned char* data, std::size_t len, bool with_prefix = true);

}  // namespace didzk
