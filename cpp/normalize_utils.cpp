#include "normalize_utils.hpp"

#include "hex_utils.hpp"
#include "merkle_poseidon.hpp"
#include "text_utils.hpp"

#include <stdexcept>

namespace didzk {

std::string require_normalized_field_hex(const std::string& value,
                                         const std::string& projectRoot,
                                         const std::string& fieldName) {
  const std::string v = trim_copy(value);
  if (v.empty()) {
    throw std::runtime_error("missing_field: " + fieldName);
  }
  return normalize_field_hex(v, projectRoot);
}

bool try_normalize_field_hex(const std::string& raw,
                             const std::string& projectRoot,
                             const std::string& context,
                             std::string* out,
                             std::string* err) {
  const std::string t = trim_copy(raw);
  if (t.empty()) {
    if (err) *err = context + ":empty_field_value";
    return false;
  }
  try {
    if (out) *out = normalize_field_hex(t, projectRoot);
    return true;
  } catch (const std::exception& e) {
    if (err) {
      *err = context + ":invalid_hex_string len=" + std::to_string(t.size()) +
             " raw='" + t + "' err=" + e.what();
    }
    return false;
  }
}

bool try_normalize_digest_hex32(const std::string& raw,
                                const std::string& label,
                                std::string* out,
                                std::string* diag) {
  const std::string t = trim_copy(raw);
  if (t.empty()) return false;
  try {
    if (out) *out = normalize_digest_hex32(t);
    return true;
  } catch (const std::exception& e) {
    if (diag) *diag = label + "_invalid: " + e.what();
    return false;
  }
}

}  // namespace didzk
