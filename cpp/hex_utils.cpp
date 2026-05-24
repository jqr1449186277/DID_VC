#include "hex_utils.hpp"

#include "text_utils.hpp"

#include <stdexcept>

namespace didzk {

bool starts_with_0x(const std::string& s) {
  return s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
  if (c >= 'a' && c <= 'f') return 10 + static_cast<int>(c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + static_cast<int>(c - 'A');
  return -1;
}

bool is_hex_string(const std::string& s, bool require_prefix) {
  const std::string v = trim_copy(s);
  std::size_t pos = 0;
  if (starts_with_0x(v)) {
    pos = 2;
  } else if (require_prefix) {
    return false;
  }

  if (pos >= v.size()) return false;
  for (std::size_t i = pos; i < v.size(); ++i) {
    if (hex_value(v[i]) < 0) return false;
  }
  return true;
}

std::string ensure_0x_hex(const std::string& s) {
  std::string v = lower_copy(trim_copy(s));
  if (!is_hex_string(v, false)) {
    throw std::runtime_error("invalid_hex_string");
  }
  if (!starts_with_0x(v)) v = "0x" + v;
  return v;
}

std::string normalize_hex_bytes(const std::string& s, std::size_t byte_len) {
  const std::string v = ensure_0x_hex(s);
  std::string body = v.substr(2);
  if (body.size() > byte_len * 2) {
    throw std::runtime_error("hex_too_long");
  }
  if (body.size() < byte_len * 2) {
    body = std::string(byte_len * 2 - body.size(), '0') + body;
  }
  return "0x" + body;
}

std::string normalize_digest_hex32(const std::string& s) {
  return normalize_hex_bytes(s, 32);
}

std::vector<std::uint8_t> hex_to_bytes(const std::string& s) {
  const std::string v = ensure_0x_hex(s);
  const std::string body = v.substr(2);
  if (body.size() % 2 != 0) throw std::runtime_error("odd_hex_length");

  std::vector<std::uint8_t> out(body.size() / 2, 0);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = hex_value(body[2 * i]);
    const int lo = hex_value(body[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      throw std::runtime_error("invalid_hex_digit");
    }
    out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return out;
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes, bool with_prefix) {
  return bytes_to_hex(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), with_prefix);
}

std::string bytes_to_hex(const unsigned char* data, std::size_t len, bool with_prefix) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve((with_prefix ? 2 : 0) + len * 2);
  if (with_prefix) out += "0x";
  for (std::size_t i = 0; i < len; ++i) {
    const unsigned char b = data[i];
    out.push_back(kHexDigits[b >> 4]);
    out.push_back(kHexDigits[b & 0x0f]);
  }
  return out;
}

}  // namespace didzk
