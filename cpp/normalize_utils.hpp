#pragma once

#include <string>

namespace didzk {

std::string require_normalized_field_hex(const std::string& value,
                                         const std::string& projectRoot,
                                         const std::string& fieldName);

bool try_normalize_field_hex(const std::string& raw,
                             const std::string& projectRoot,
                             const std::string& context,
                             std::string* out,
                             std::string* err = nullptr);

bool try_normalize_digest_hex32(const std::string& raw,
                                const std::string& label,
                                std::string* out,
                                std::string* diag = nullptr);

}  // namespace didzk
