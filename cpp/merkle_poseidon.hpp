#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace didzk {

struct IdentityStateZK {
  std::string id;
  std::string sid;
  std::string rho;
  std::string cid;
  std::string pkNormHash;
  std::string pkRecHash;
  std::uint64_t ver{0};
  bool active{true};
};

struct MerklePathZK {
  std::vector<std::string> pathElements;
  std::vector<std::uint8_t> pathIndex;
  std::string root;
  std::uint32_t depth{0};
  std::uint64_t epoch{0};
};

struct FieldElementBytes32 {
  std::array<std::uint8_t, 32> bytes{};
};

struct IdentityPoseidonBundle {
  std::string cidHex;
  std::string leafHex;
  std::string rootHex;
  bool hasRoot{false};
  FieldElementBytes32 cidField{};
  FieldElementBytes32 leafField{};
  FieldElementBytes32 rootField{};
};

struct PoseidonBridgeStats {
  double totalMs{0.0};
  double normalizeMs{0.0};
  double cidMs{0.0};
  double leafMs{0.0};
  double hash2Ms{0.0};
  double foldPathMs{0.0};
  std::uint64_t calls{0};
  std::uint64_t cacheHits{0};
};

std::string detect_project_root(const std::string& preferred_root = "");
std::string normalize_field_hex(const std::string& value,
                                const std::string& project_root = "");

FieldElementBytes32 normalize_field_native(const std::string& value,
                                           const std::string& project_root = "");
std::vector<FieldElementBytes32> normalize_fields_native(const std::vector<std::string>& values,
                                                         const std::string& project_root = "");
std::string field_bytes_to_hex(const FieldElementBytes32& value);
bool field_equal(const FieldElementBytes32& lhs, const FieldElementBytes32& rhs);

std::string poseidon_hash2(const std::string& a,
                           const std::string& b,
                           const std::string& project_root = "");
std::string poseidon_hash5(const std::string& a,
                           const std::string& b,
                           const std::string& c,
                           const std::string& d,
                           const std::string& e,
                           const std::string& project_root = "");
std::string poseidon_cid(const std::string& sid,
                         const std::string& rho,
                         const std::string& project_root = "");
std::string poseidon_leaf(const std::string& cid,
                          const std::string& pkNormHash,
                          const std::string& pkRecHash,
                          std::uint64_t ver,
                          bool active,
                          const std::string& project_root = "");
std::string compute_leaf_from_identity(const IdentityStateZK& st,
                                       const std::string& project_root = "");

IdentityPoseidonBundle compute_identity_bundle(const IdentityStateZK& st,
                                               const std::string& project_root = "");
IdentityPoseidonBundle compute_identity_bundle_with_path(const IdentityStateZK& st,
                                                         const MerklePathZK& path,
                                                         const std::string& project_root = "");
std::vector<IdentityPoseidonBundle> compute_identity_bundles(const std::vector<IdentityStateZK>& states,
                                                             const std::string& project_root = "");
std::vector<IdentityPoseidonBundle> compute_identity_bundles_with_paths(const std::vector<IdentityStateZK>& states,
                                                                        const std::vector<MerklePathZK>& paths,
                                                                        const std::string& project_root = "");

std::string fold_merkle_level(const std::string& current,
                              const std::string& sibling,
                              std::uint8_t path_index,
                              const std::string& project_root = "");
std::string compute_root_from_path(const std::string& leaf,
                                   const MerklePathZK& path,
                                   const std::string& project_root = "");
bool verify_merkle_path_local(const std::string& leaf,
                              const MerklePathZK& path,
                              const std::string& project_root = "");

void reset_poseidon_bridge_stats();
PoseidonBridgeStats get_poseidon_bridge_stats();

}  // namespace didzk
