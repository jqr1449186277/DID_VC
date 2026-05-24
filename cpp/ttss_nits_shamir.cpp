#include "ttss_nits_shamir.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace didzk {
namespace {

constexpr FieldInt kFieldMod = 65537u;  // Fermat prime; demo-friendly and boost-free.
constexpr std::size_t kSecretBytes = 32;
constexpr std::size_t kSecretChunkCount = 16;   // 16 x 16-bit chunks = 32 bytes
constexpr std::size_t kPackedYBytes = 34;       // 16 x 17-bit values = 272 bits

FieldInt add_mod(FieldInt a, FieldInt b) {
  std::uint64_t s = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b);
  s %= kFieldMod;
  return static_cast<FieldInt>(s);
}

FieldInt sub_mod(FieldInt a, FieldInt b) {
  return static_cast<FieldInt>((static_cast<std::uint64_t>(a) + kFieldMod - b) % kFieldMod);
}

FieldInt mul_mod(FieldInt a, FieldInt b) {
  return static_cast<FieldInt>((static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b)) % kFieldMod);
}

FieldInt pow_mod(FieldInt base, std::uint64_t exp) {
  FieldInt out = 1;
  while (exp > 0) {
    if (exp & 1ull) out = mul_mod(out, base);
    base = mul_mod(base, base);
    exp >>= 1ull;
  }
  return out;
}

FieldInt inv_mod(FieldInt x) {
  if (x == 0) throw std::runtime_error("inverse_of_zero");
  return pow_mod(x, kFieldMod - 2u);
}

std::string decimal_string(std::uint64_t v) {
  return std::to_string(v);
}

FieldInt hex_mod_small_field(const std::string& hex) {
  const std::string norm = ensure_0x_hex(hex);
  const std::string body = norm.substr(2);
  std::uint64_t value = 0;
  for (char c : body) {
    unsigned digit = 0;
    if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(10 + c - 'a');
    else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(10 + c - 'A');
    else throw std::runtime_error("bad_hex_digit");
    value = ((value << 4) + digit) % kFieldMod;
  }
  return static_cast<FieldInt>(value);
}

FieldInt hash_parts_to_field(const std::vector<std::string>& parts) {
  return hex_mod_small_field(sha256_joined_hex(parts));
}

FieldInt derive_coeff(const std::string& rho_seed_hex,
                      const std::string& id_hash,
                      std::uint64_t ver,
                      std::uint64_t epoch,
                      std::size_t chunk_idx,
                      std::uint32_t degree_k) {
  return hash_parts_to_field({"TTSS-COEFF", normalize_digest_hex32(rho_seed_hex),
                              normalize_digest_hex32(id_hash), decimal_string(ver),
                              decimal_string(epoch), std::to_string(chunk_idx),
                              std::to_string(degree_k)});
}

FieldInt derive_distinct_x(const std::string& rho_seed_hex,
                           const std::string& id_hash,
                           std::uint64_t ver,
                           std::uint64_t epoch,
                           std::uint32_t guardian_index,
                           const std::string& guardian_id,
                           const std::set<FieldInt>& used_x) {
  for (std::uint32_t ctr = 0; ctr < 4096; ++ctr) {
    FieldInt x = hash_parts_to_field({"TTSS-X", normalize_digest_hex32(rho_seed_hex),
                                      normalize_digest_hex32(id_hash), decimal_string(ver),
                                      decimal_string(epoch), std::to_string(guardian_index),
                                      guardian_id, std::to_string(ctr)});
    x = static_cast<FieldInt>((x % (kFieldMod - 1u)) + 1u);  // non-zero
    if (used_x.find(x) == used_x.end()) return x;
  }
  throw std::runtime_error("unable_to_derive_distinct_x");
}

FieldInt poly_eval(const std::vector<FieldInt>& coeffs, FieldInt x) {
  FieldInt acc = 0;
  FieldInt pow_x = 1;
  for (FieldInt a : coeffs) {
    acc = add_mod(acc, mul_mod(a, pow_x));
    pow_x = mul_mod(pow_x, x);
  }
  return acc;
}

std::array<FieldInt, kSecretChunkCount> split_seed_hex_to_chunks(const std::string& seed_hex) {
  const auto seed_bytes = hex_to_bytes(normalize_digest_hex32(seed_hex));
  std::array<FieldInt, kSecretChunkCount> chunks{};
  for (std::size_t i = 0; i < kSecretChunkCount; ++i) {
    chunks[i] = static_cast<FieldInt>((static_cast<unsigned>(seed_bytes[2 * i]) << 8) |
                                      static_cast<unsigned>(seed_bytes[2 * i + 1]));
  }
  return chunks;
}

std::string chunks_to_seed_hex(const std::array<FieldInt, kSecretChunkCount>& chunks) {
  std::vector<std::uint8_t> bytes(kSecretBytes);
  for (std::size_t i = 0; i < kSecretChunkCount; ++i) {
    if (chunks[i] > 0xFFFFu) throw std::runtime_error("recovered_chunk_out_of_16bit_range");
    bytes[2 * i] = static_cast<std::uint8_t>((chunks[i] >> 8) & 0xffu);
    bytes[2 * i + 1] = static_cast<std::uint8_t>(chunks[i] & 0xffu);
  }
  return bytes_to_hex(bytes, true);
}

std::string pack_y_chunks_hex(const std::array<FieldInt, kSecretChunkCount>& values) {
  std::vector<std::uint8_t> out(kPackedYBytes, 0);
  std::size_t bit_pos = 0;
  for (FieldInt v : values) {
    if (v >= kFieldMod) throw std::runtime_error("y_chunk_out_of_field_range");
    for (int bit = 16; bit >= 0; --bit) {
      const std::size_t byte_idx = bit_pos / 8;
      const int bit_in_byte = 7 - static_cast<int>(bit_pos % 8);
      const std::uint8_t b = static_cast<std::uint8_t>((v >> bit) & 1u);
      out[byte_idx] |= static_cast<std::uint8_t>(b << bit_in_byte);
      ++bit_pos;
    }
  }
  return bytes_to_hex(out, true);
}

std::array<FieldInt, kSecretChunkCount> unpack_y_chunks_hex(const std::string& packed_hex) {
  const auto in = hex_to_bytes(ensure_0x_hex(packed_hex));
  if (in.size() != kPackedYBytes) throw std::runtime_error("bad_packed_y_size");
  std::array<FieldInt, kSecretChunkCount> out{};
  std::size_t bit_pos = 0;
  for (std::size_t i = 0; i < kSecretChunkCount; ++i) {
    FieldInt v = 0;
    for (int bit = 16; bit >= 0; --bit) {
      const std::size_t byte_idx = bit_pos / 8;
      const int bit_in_byte = 7 - static_cast<int>(bit_pos % 8);
      const FieldInt b = static_cast<FieldInt>((in[byte_idx] >> bit_in_byte) & 1u);
      v = static_cast<FieldInt>((v << 1) | b);
      ++bit_pos;
    }
    out[i] = v;
  }
  return out;
}

}  // namespace

const FieldInt& bn254_fr_modulus() {
  return kFieldMod;
}

FieldInt field_normalize(FieldInt v) {
  return static_cast<FieldInt>(v % kFieldMod);
}

FieldInt field_from_hex(const std::string& hex) {
  return hex_mod_small_field(hex);
}

std::string field_to_hex32(FieldInt v) {
  std::vector<std::uint8_t> bytes(32, 0);
  v = field_normalize(v);
  bytes[28] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
  bytes[29] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
  bytes[30] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
  bytes[31] = static_cast<std::uint8_t>(v & 0xffu);
  return bytes_to_hex(bytes, true);
}

std::string normalize_field_hex_bn254(const std::string& hex) {
  return field_to_hex32(field_from_hex(hex));
}

std::string compute_ui_hex_from_x(const std::string& xi_hex) {
  return sha256_joined_hex({"NITS-UI", normalize_field_hex_bn254(xi_hex)});
}

std::string compute_tag_hex(const std::string& id_hash,
                            std::uint64_t ver,
                            std::uint64_t epoch,
                            std::uint32_t guardian_index,
                            const std::string& ui_hex) {
  return sha256_joined_hex({"TTSS-TAG", normalize_digest_hex32(id_hash),
                            decimal_string(ver), decimal_string(epoch),
                            std::to_string(guardian_index), normalize_digest_hex32(ui_hex)});
}

std::string compute_vk_set_hash(const std::vector<TTSSKeyEntry>& vk_entries) {
  std::vector<std::string> canon;
  canon.reserve(vk_entries.size());
  for (const auto& e : vk_entries) {
    canon.push_back(std::to_string(e.guardianIndex) + ":" + normalize_digest_hex32(e.uiHex));
  }
  std::sort(canon.begin(), canon.end());
  return sha256_joined_hex(canon);
}

TTSSShareSetupResult ttss_share_nits_shamir(
    const std::string& srec_seed_hex,
    std::uint32_t n,
    std::uint32_t t,
    const std::string& rho_seed_hex,
    const std::string& id,
    const std::string& id_hash,
    std::uint64_t ver,
    std::uint64_t epoch,
    const std::vector<std::string>& guardian_ids,
    const std::string& guardian_set_hash,
    const std::string& dealer_attestation_key_hex,
    const std::string& dealer_pk_hex,
    std::uint64_t issued_at,
    std::uint64_t expires_at) {
  if (n < 2 || t < 2 || t > n) throw std::runtime_error("bad_threshold");
  if (!guardian_ids.empty() && guardian_ids.size() != n) throw std::runtime_error("guardian_count_mismatch");

  TTSSShareSetupResult out;
  out.rhoCommitHex = sha256_joined_hex({"TTSS-RHO-COMMIT", normalize_digest_hex32(rho_seed_hex)});

  const auto secret_chunks = split_seed_hex_to_chunks(srec_seed_hex);
  std::vector<std::vector<FieldInt>> coeffs(kSecretChunkCount, std::vector<FieldInt>(t));
  for (std::size_t c = 0; c < kSecretChunkCount; ++c) {
    coeffs[c][0] = secret_chunks[c];
    for (std::uint32_t k = 1; k < t; ++k) {
      coeffs[c][k] = derive_coeff(rho_seed_hex, id_hash, ver, epoch, c, k);
    }
  }

  std::set<FieldInt> used_x;
  out.shareEnvelopes.reserve(n);
  out.tk.reserve(n);
  out.vk.reserve(n);

  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint32_t guardian_index = i + 1;
    const std::string guardian_id = guardian_ids.empty() ? ("G" + std::to_string(guardian_index)) : guardian_ids[i];
    const FieldInt x = derive_distinct_x(rho_seed_hex, id_hash, ver, epoch, guardian_index, guardian_id, used_x);
    used_x.insert(x);

    std::array<FieldInt, kSecretChunkCount> y_chunks{};
    for (std::size_t c = 0; c < kSecretChunkCount; ++c) {
      y_chunks[c] = poly_eval(coeffs[c], x);
    }

    const std::string xi_hex = field_to_hex32(x);
    const std::string yi_hex = pack_y_chunks_hex(y_chunks);
    const std::string ui_hex = compute_ui_hex_from_x(xi_hex);

    TTSSKeyEntry key_entry;
    key_entry.guardianIndex = guardian_index;
    key_entry.guardianId = guardian_id;
    key_entry.uiHex = ui_hex;
    out.tk.push_back(key_entry);
    out.vk.push_back(key_entry);

    ShareEnvelope env;
    env.scheme = kTTSSSchemeNitsShamirV1;
    env.id = id;
    env.idHash = normalize_digest_hex32(id_hash);
    env.guardianIndex = guardian_index;
    env.guardianId = guardian_id;
    env.n = n;
    env.t = t;
    env.ver = ver;
    env.epoch = epoch;
    env.secretType = kTTSSSecretTypeSrecSeed;
    env.share.xiHex = xi_hex;
    env.share.yiHex = yi_hex;
    env.traceBinding.uiHex = ui_hex;
    env.tag.tagHex = compute_tag_hex(id_hash, ver, epoch, guardian_index, ui_hex);
    env.meta.guardianSetHash = guardian_set_hash.empty() ? sha256_joined_hex({"TTSS-GSET", std::to_string(n), id})
                                                         : normalize_digest_hex32(guardian_set_hash);
    env.meta.dealerPkHex = dealer_pk_hex.empty() ? normalize_digest_hex32(dealer_attestation_key_hex)
                                                 : normalize_digest_hex32(dealer_pk_hex);
    env.meta.rhoCommitHex = out.rhoCommitHex;
    env.meta.issuedAt = issued_at;
    env.meta.expiresAt = expires_at;
    env.active = true;
    out.shareEnvelopes.push_back(env);
  }

  out.vkSetHash = compute_vk_set_hash(out.vk);
  for (auto& env : out.shareEnvelopes) {
    env.meta.vkSetHash = out.vkSetHash;
    sign_share_envelope_demo(&env, env.meta.dealerPkHex);
  }
  return out;
}

FieldInt lagrange_interpolate_at_zero(const std::vector<std::pair<FieldInt, FieldInt>>& points) {
  if (points.empty()) throw std::runtime_error("no_points");
  FieldInt secret = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const FieldInt xi = points[i].first;
    const FieldInt yi = points[i].second;
    FieldInt num = 1;
    FieldInt den = 1;
    for (std::size_t j = 0; j < points.size(); ++j) {
      if (j == i) continue;
      const FieldInt xj = points[j].first;
      num = mul_mod(num, sub_mod(0, xj));
      den = mul_mod(den, sub_mod(xi, xj));
    }
    const FieldInt li0 = mul_mod(num, inv_mod(den));
    secret = add_mod(secret, mul_mod(yi, li0));
  }
  return secret;
}

TTSSRecoverResult ttss_rec_nits_shamir(const std::vector<ShareEnvelope>& shares,
                                       std::string* verify_err) {
  TTSSRecoverResult out;
  if (shares.empty()) {
    out.err = "no_shares";
    if (verify_err) *verify_err = out.err;
    return out;
  }

  std::string err;
  const ShareEnvelope& first = shares.front();
  if (!verify_share_envelope_demo(first, &err)) {
    out.err = err;
    if (verify_err) *verify_err = err;
    return out;
  }
  const std::uint32_t t = first.t;
  if (shares.size() < t) {
    out.err = "insufficient_shares";
    if (verify_err) *verify_err = out.err;
    return out;
  }

  std::set<std::string> seen_x;
  std::vector<std::pair<FieldInt, std::array<FieldInt, kSecretChunkCount>>> points;
  points.reserve(t);

  for (const auto& env : shares) {
    if (!verify_share_envelope_demo(env, &err)) {
      out.err = err;
      if (verify_err) *verify_err = err;
      return out;
    }
    if (env.scheme != first.scheme || env.idHash != first.idHash || env.ver != first.ver ||
        env.epoch != first.epoch || env.n != first.n || env.t != first.t ||
        env.secretType != first.secretType) {
      out.err = "share_header_mismatch";
      if (verify_err) *verify_err = out.err;
      return out;
    }
    if (!env.active) {
      out.err = "inactive_share_present";
      if (verify_err) *verify_err = out.err;
      return out;
    }
    const std::string xi_norm = normalize_field_hex_bn254(env.share.xiHex);
    if (!seen_x.insert(xi_norm).second) {
      out.err = "duplicate_x";
      if (verify_err) *verify_err = out.err;
      return out;
    }
    try {
      points.push_back({field_from_hex(env.share.xiHex), unpack_y_chunks_hex(env.share.yiHex)});
    } catch (const std::exception& ex) {
      out.err = std::string("bad_y_encoding:") + ex.what();
      if (verify_err) *verify_err = out.err;
      return out;
    }
    if (points.size() == t) break;
  }

  if (points.size() < t) {
    out.err = "not_enough_distinct_points";
    if (verify_err) *verify_err = out.err;
    return out;
  }

  std::array<FieldInt, kSecretChunkCount> recovered{};
  for (std::size_t c = 0; c < kSecretChunkCount; ++c) {
    std::vector<std::pair<FieldInt, FieldInt>> chunk_points;
    chunk_points.reserve(t);
    for (const auto& p : points) {
      chunk_points.push_back({p.first, p.second[c]});
    }
    recovered[c] = lagrange_interpolate_at_zero(chunk_points);
    if (recovered[c] > 0xFFFFu) {
      out.err = "recovered_chunk_out_of_range";
      if (verify_err) *verify_err = out.err;
      return out;
    }
  }

  out.ok = true;
  out.srecSeedHex = chunks_to_seed_hex(recovered);
  out.shareCountUsed = t;
  if (verify_err) verify_err->clear();
  return out;
}

}  // namespace didzk
