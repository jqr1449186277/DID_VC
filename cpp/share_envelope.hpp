#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "hex_utils.hpp"
#include "text_utils.hpp"

namespace didzk {

inline constexpr const char* kTTSSSchemeNitsShamirV1 = "NITS-Shamir-v1";
inline constexpr const char* kTTSSSecretTypeSrecSeed = "srec_seed";
inline constexpr const char* kTTSSDemoSigAlg = "digest-attestation-v1";

struct ShareCore {
  std::string xiHex;
  std::string yiHex;
};

struct TraceBinding {
  std::string uiHex;
};

struct TagBinding {
  std::string tagHex;
};

struct DealerProof {
  std::string sigAlg{kTTSSDemoSigAlg};
  std::string sigmaHex;
};

struct ShareMeta {
  std::string vkSetHash;
  std::string guardianSetHash;
  std::string dealerPkHex;
  std::string rhoCommitHex;
  std::uint64_t issuedAt{0};
  std::uint64_t expiresAt{0};
};

struct ShareEnvelope {
  std::string scheme{kTTSSSchemeNitsShamirV1};
  std::string id;
  std::string idHash;
  std::uint32_t guardianIndex{0};
  std::string guardianId;
  std::uint32_t n{0};
  std::uint32_t t{0};
  std::uint64_t ver{0};
  std::uint64_t epoch{0};
  std::string secretType{kTTSSSecretTypeSrecSeed};
  ShareCore share;
  TraceBinding traceBinding;
  TagBinding tag;
  DealerProof dealerProof;
  ShareMeta meta;
  bool active{true};
};

std::string sha256_hex(const std::vector<std::uint8_t>& data, bool with_prefix = true);
std::string sha256_hex(const std::string& text, bool with_prefix = true);
std::string sha256_joined_hex(const std::vector<std::string>& parts,
                              const std::string& separator = "|");

std::string share_key(const ShareEnvelope& env);

bool basic_validate_share_envelope(const ShareEnvelope& env, std::string* err = nullptr);
std::string canonical_share_envelope_payload(const ShareEnvelope& env);
std::string share_envelope_digest_hex(const ShareEnvelope& env);
std::string compute_demo_sigma_hex(const ShareEnvelope& env,
                                   const std::string& dealer_attestation_key_hex);
void sign_share_envelope_demo(ShareEnvelope* env,
                              const std::string& dealer_attestation_key_hex);
bool verify_share_envelope_demo(const ShareEnvelope& env, std::string* err = nullptr);

nlohmann::json share_envelope_to_njson(const ShareEnvelope& env);
bool parse_share_envelope_from_njson(const nlohmann::json& j,
                                     ShareEnvelope* out,
                                     std::string* err = nullptr);
ShareEnvelope share_envelope_from_njson(const nlohmann::json& j);
void to_json(nlohmann::json& j, const ShareEnvelope& env);
void from_json(const nlohmann::json& j, ShareEnvelope& env);

}  // namespace didzk
