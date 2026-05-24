#include "share_envelope.hpp"

#include "json_utils.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace didzk {
namespace {

struct Sha256Ctx {
  std::array<std::uint32_t, 8> state{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> block{};
  std::uint64_t bit_len{0};
  std::size_t block_len{0};
};

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

void sha256_transform(Sha256Ctx* ctx, const std::uint8_t block[64]) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<std::uint32_t>(block[i * 4 + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  std::uint32_t a = ctx->state[0];
  std::uint32_t b = ctx->state[1];
  std::uint32_t c = ctx->state[2];
  std::uint32_t d = ctx->state[3];
  std::uint32_t e = ctx->state[4];
  std::uint32_t f = ctx->state[5];
  std::uint32_t g = ctx->state[6];
  std::uint32_t h = ctx->state[7];

  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
    const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

std::vector<std::uint8_t> sha256_bytes(const std::vector<std::uint8_t>& data) {
  Sha256Ctx ctx;
  for (std::uint8_t byte : data) {
    ctx.block[ctx.block_len++] = byte;
    if (ctx.block_len == 64) {
      sha256_transform(&ctx, ctx.block.data());
      ctx.bit_len += 512;
      ctx.block_len = 0;
    }
  }

  ctx.bit_len += static_cast<std::uint64_t>(ctx.block_len) * 8ull;
  ctx.block[ctx.block_len++] = 0x80;
  if (ctx.block_len > 56) {
    while (ctx.block_len < 64) ctx.block[ctx.block_len++] = 0x00;
    sha256_transform(&ctx, ctx.block.data());
    ctx.block_len = 0;
  }
  while (ctx.block_len < 56) ctx.block[ctx.block_len++] = 0x00;
  for (int i = 7; i >= 0; --i) {
    ctx.block[ctx.block_len++] = static_cast<std::uint8_t>((ctx.bit_len >> (i * 8)) & 0xffu);
  }
  sha256_transform(&ctx, ctx.block.data());

  std::vector<std::uint8_t> out(32);
  for (int i = 0; i < 8; ++i) {
    out[i * 4 + 0] = static_cast<std::uint8_t>((ctx.state[i] >> 24) & 0xffu);
    out[i * 4 + 1] = static_cast<std::uint8_t>((ctx.state[i] >> 16) & 0xffu);
    out[i * 4 + 2] = static_cast<std::uint8_t>((ctx.state[i] >> 8) & 0xffu);
    out[i * 4 + 3] = static_cast<std::uint8_t>(ctx.state[i] & 0xffu);
  }
  return out;
}

std::string normalized_optional_digest(const std::string& v) {
  return v.empty() ? std::string() : normalize_digest_hex32(v);
}

}  // namespace


std::string sha256_hex(const std::vector<std::uint8_t>& data, bool with_prefix) {
  return bytes_to_hex(sha256_bytes(data), with_prefix);
}

std::string sha256_hex(const std::string& text, bool with_prefix) {
  return sha256_hex(std::vector<std::uint8_t>(text.begin(), text.end()), with_prefix);
}

std::string sha256_joined_hex(const std::vector<std::string>& parts,
                              const std::string& separator) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) oss << separator;
    oss << parts[i];
  }
  return sha256_hex(oss.str(), true);
}

std::string share_key(const ShareEnvelope& env) {
  std::ostringstream oss;
  oss << env.idHash << ':' << env.ver << ':' << env.epoch << ':' << env.guardianIndex;
  return oss.str();
}

bool basic_validate_share_envelope(const ShareEnvelope& env, std::string* err) {
  auto fail = [&](const std::string& e) {
    if (err) *err = e;
    return false;
  };
  if (env.scheme != kTTSSSchemeNitsShamirV1) return fail("bad_scheme");
  if (env.idHash.empty() || !is_hex_string(env.idHash, false)) return fail("bad_id_hash");
  if (env.guardianIndex == 0) return fail("bad_guardian_index");
  if (env.n < 2 || env.t < 2 || env.t > env.n) return fail("bad_threshold");
  if (env.secretType != kTTSSSecretTypeSrecSeed) return fail("bad_secret_type");
  if (env.share.xiHex.empty() || env.share.yiHex.empty()) return fail("missing_share_core");
  if (!is_hex_string(env.share.xiHex, false) || !is_hex_string(env.share.yiHex, false)) {
    return fail("bad_share_hex");
  }
  if (!is_hex_string(env.traceBinding.uiHex, false)) return fail("bad_ui_hex");
  if (!is_hex_string(env.tag.tagHex, false)) return fail("bad_tag_hex");
  if (!is_hex_string(env.dealerProof.sigmaHex, false)) return fail("bad_sigma_hex");
  if (!env.meta.vkSetHash.empty() && !is_hex_string(env.meta.vkSetHash, false)) {
    return fail("bad_vk_set_hash");
  }
  if (!env.meta.guardianSetHash.empty() && !is_hex_string(env.meta.guardianSetHash, false)) {
    return fail("bad_guardian_set_hash");
  }
  if (!env.meta.dealerPkHex.empty() && !is_hex_string(env.meta.dealerPkHex, false)) {
    return fail("bad_dealer_pk_hex");
  }
  if (!env.meta.rhoCommitHex.empty() && !is_hex_string(env.meta.rhoCommitHex, false)) {
    return fail("bad_rho_commit_hex");
  }
  return true;
}

std::string canonical_share_envelope_payload(const ShareEnvelope& env) {
  auto normalize_any_hex = [](const std::string& s) {
    std::string v = lower_copy(trim_copy(s));
    if (v.empty()) return std::string();
    if (v.rfind("0x", 0) != 0) v = "0x" + v;
    std::string body = v.substr(2);
    if (body.empty()) body = "00";
    if (body.size() % 2 != 0) body = "0" + body;
    return std::string("0x") + body;
  };

  std::ostringstream oss;
  oss << "scheme=" << env.scheme
      << "|id=" << env.id
      << "|idHash=" << normalize_digest_hex32(env.idHash)
      << "|guardianIndex=" << env.guardianIndex
      << "|guardianId=" << env.guardianId
      << "|n=" << env.n
      << "|t=" << env.t
      << "|ver=" << env.ver
      << "|epoch=" << env.epoch
      << "|secretType=" << env.secretType
      << "|xiHex=" << normalize_any_hex(env.share.xiHex)
      << "|yiHex=" << normalize_any_hex(env.share.yiHex)
      << "|uiHex=" << normalize_digest_hex32(env.traceBinding.uiHex)
      << "|tagHex=" << normalize_digest_hex32(env.tag.tagHex)
      << "|vkSetHash=" << (env.meta.vkSetHash.empty() ? std::string() : normalize_digest_hex32(env.meta.vkSetHash))
      << "|guardianSetHash=" << (env.meta.guardianSetHash.empty() ? std::string() : normalize_digest_hex32(env.meta.guardianSetHash))
      << "|dealerPkHex=" << (env.meta.dealerPkHex.empty() ? std::string() : normalize_digest_hex32(env.meta.dealerPkHex))
      << "|rhoCommitHex=" << (env.meta.rhoCommitHex.empty() ? std::string() : normalize_digest_hex32(env.meta.rhoCommitHex))
      << "|issuedAt=" << env.meta.issuedAt
      << "|expiresAt=" << env.meta.expiresAt
      << "|active=" << (env.active ? 1 : 0);
  return oss.str();
}

std::string share_envelope_digest_hex(const ShareEnvelope& env) {
  return sha256_hex(canonical_share_envelope_payload(env), true);
}

std::string compute_demo_sigma_hex(const ShareEnvelope& env,
                                   const std::string& dealer_attestation_key_hex) {
  const std::string key_hex = normalize_digest_hex32(dealer_attestation_key_hex);
  return sha256_joined_hex({"TTSS-SIG", key_hex, share_envelope_digest_hex(env)});
}

void sign_share_envelope_demo(ShareEnvelope* env,
                              const std::string& dealer_attestation_key_hex) {
  if (!env) throw std::runtime_error("null_share_envelope");
  if (env->dealerProof.sigAlg.empty()) env->dealerProof.sigAlg = kTTSSDemoSigAlg;
  env->dealerProof.sigmaHex = compute_demo_sigma_hex(*env, dealer_attestation_key_hex);
}

bool verify_share_envelope_demo(const ShareEnvelope& env, std::string* err) {
  auto fail = [&](const std::string& e) {
    if (err) *err = e;
    return false;
  };
  if (!basic_validate_share_envelope(env, err)) return false;
  if (env.dealerProof.sigAlg != kTTSSDemoSigAlg) return fail("unsupported_sig_alg");
  if (env.meta.dealerPkHex.empty()) return fail("missing_dealer_pk_hex");
  const std::string expected = compute_demo_sigma_hex(env, env.meta.dealerPkHex);
  if (lower_copy(expected) != lower_copy(normalize_digest_hex32(env.dealerProof.sigmaHex))) {
    return fail("sigma_mismatch");
  }
  return true;
}

nlohmann::json share_envelope_to_njson(const ShareEnvelope& env) {
  return {
      {"scheme", env.scheme},
      {"id", env.id},
      {"idHash", normalize_digest_hex32(env.idHash)},
      {"guardianIndex", env.guardianIndex},
      {"guardianId", env.guardianId},
      {"n", env.n},
      {"t", env.t},
      {"ver", env.ver},
      {"epoch", env.epoch},
      {"secretType", env.secretType},
      {"share", {{"xiHex", normalize_digest_hex32(env.share.xiHex)},
                  {"yiHex", ensure_0x_hex(env.share.yiHex)}}},
      {"traceBinding", {{"uiHex", normalize_digest_hex32(env.traceBinding.uiHex)}}},
      {"tag", {{"tagHex", normalize_digest_hex32(env.tag.tagHex)}}},
      {"dealerProof", {{"sigAlg", env.dealerProof.sigAlg},
                       {"sigmaHex", normalize_digest_hex32(env.dealerProof.sigmaHex)}}},
      {"meta",
       {{"vkSetHash", normalized_optional_digest(env.meta.vkSetHash)},
        {"guardianSetHash", normalized_optional_digest(env.meta.guardianSetHash)},
        {"dealerPkHex", normalized_optional_digest(env.meta.dealerPkHex)},
        {"rhoCommitHex", normalized_optional_digest(env.meta.rhoCommitHex)},
        {"issuedAt", env.meta.issuedAt},
        {"expiresAt", env.meta.expiresAt}}},
      {"active", env.active}};
}

bool parse_share_envelope_from_njson(const nlohmann::json& j,
                                     ShareEnvelope* out,
                                     std::string* err) {
  auto fail = [&](const std::string& e) {
    if (err) *err = e;
    return false;
  };
  if (!out) return fail("null_output");
  if (!j.is_object()) return fail("share_envelope_not_object");

  ShareEnvelope env;
  env.scheme = json_string_field(j, "scheme", kTTSSSchemeNitsShamirV1);
  env.id = json_string_field(j, "id");
  env.idHash = json_string_field(j, "idHash");
  env.guardianIndex = json_u32_field(j, "guardianIndex", 0);
  env.guardianId = json_string_field(j, "guardianId");
  env.n = json_u32_field(j, "n", 0);
  env.t = json_u32_field(j, "t", 0);
  env.ver = json_u64_field(j, "ver", 0);
  env.epoch = json_u64_field(j, "epoch", 0);
  env.secretType = json_string_field(j, "secretType", kTTSSSecretTypeSrecSeed);
  env.active = json_bool_field(j, "active", true);

  if (const nlohmann::json* share = json_object_field(j, "share")) {
    env.share.xiHex = json_string_field(*share, "xiHex");
    env.share.yiHex = json_string_field(*share, "yiHex");
  }
  if (const nlohmann::json* trace = json_object_field(j, "traceBinding")) {
    env.traceBinding.uiHex = json_string_field(*trace, "uiHex");
  }
  if (const nlohmann::json* tag = json_object_field(j, "tag")) {
    env.tag.tagHex = json_string_field(*tag, "tagHex");
  }
  if (const nlohmann::json* proof = json_object_field(j, "dealerProof")) {
    env.dealerProof.sigAlg = json_string_field(*proof, "sigAlg", kTTSSDemoSigAlg);
    env.dealerProof.sigmaHex = json_string_field(*proof, "sigmaHex");
  }
  if (const nlohmann::json* meta = json_object_field(j, "meta")) {
    env.meta.vkSetHash = json_string_field(*meta, "vkSetHash");
    env.meta.guardianSetHash = json_string_field(*meta, "guardianSetHash");
    env.meta.dealerPkHex = json_string_field(*meta, "dealerPkHex");
    env.meta.rhoCommitHex = json_string_field(*meta, "rhoCommitHex");
    env.meta.issuedAt = json_u64_field(*meta, "issuedAt", 0);
    env.meta.expiresAt = json_u64_field(*meta, "expiresAt", 0);
  }

  std::string validate_err;
  if (!basic_validate_share_envelope(env, &validate_err)) return fail(validate_err);
  *out = std::move(env);
  return true;
}

ShareEnvelope share_envelope_from_njson(const nlohmann::json& j) {
  ShareEnvelope env;
  std::string err;
  if (!parse_share_envelope_from_njson(j, &env, &err)) {
    throw std::runtime_error("bad_share_envelope_json: " + err);
  }
  return env;
}

void to_json(nlohmann::json& j, const ShareEnvelope& env) {
  j = share_envelope_to_njson(env);
}

void from_json(const nlohmann::json& j, ShareEnvelope& env) {
  env = share_envelope_from_njson(j);
}

}  // namespace didzk
