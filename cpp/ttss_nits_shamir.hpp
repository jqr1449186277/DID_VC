#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "share_envelope.hpp"

namespace didzk {

using FieldInt = std::uint32_t;

struct TTSSKeyEntry {
  std::uint32_t guardianIndex{0};
  std::string guardianId;
  std::string uiHex;
};

struct TTSSShareSetupResult {
  std::vector<ShareEnvelope> shareEnvelopes;
  std::vector<TTSSKeyEntry> tk;
  std::vector<TTSSKeyEntry> vk;
  std::string vkSetHash;
  std::string rhoCommitHex;
};

struct TTSSRecoverResult {
  bool ok{false};
  std::string srecSeedHex;
  std::uint32_t shareCountUsed{0};
  std::string err;
};

const FieldInt& bn254_fr_modulus();
FieldInt field_normalize(FieldInt v);
FieldInt field_from_hex(const std::string& hex);
std::string field_to_hex32(FieldInt v);
std::string normalize_field_hex_bn254(const std::string& hex);
std::string compute_ui_hex_from_x(const std::string& xi_hex);
std::string compute_tag_hex(const std::string& id_hash,
                            std::uint64_t ver,
                            std::uint64_t epoch,
                            std::uint32_t guardian_index,
                            const std::string& ui_hex);
std::string compute_vk_set_hash(const std::vector<TTSSKeyEntry>& vk_entries);

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
    const std::string& dealer_pk_hex = "",
    std::uint64_t issued_at = 0,
    std::uint64_t expires_at = 0);

TTSSRecoverResult ttss_rec_nits_shamir(const std::vector<ShareEnvelope>& shares,
                                       std::string* verify_err = nullptr);

FieldInt lagrange_interpolate_at_zero(const std::vector<std::pair<FieldInt, FieldInt>>& points);

}  // namespace didzk
