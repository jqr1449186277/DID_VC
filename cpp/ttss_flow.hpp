#pragma once

#include "bb_client.hpp"

std::vector<std::string> guardian_ids_for_n(int n);
std::string did_id_hash_for_ttss(const std::string& id);
nlohmann::json key_entries_to_json(const std::vector<didzk::TTSSKeyEntry>& entries);
nlohmann::json identity_state_to_json(const IdentityStateZK& st);
IdentityStateZK identity_state_from_json(const nlohmann::json& j);
nlohmann::json session_keypair_to_json(const SessionKeyPair& kp);
SessionKeyPair session_keypair_from_json(const nlohmann::json& j);
nlohmann::json identity_local_keys_to_json(const IdentityLocalKeys& k);
IdentityLocalKeys identity_local_keys_from_json(const nlohmann::json& j);
nlohmann::json root_info_to_json(const RootInfo& r);
RootInfo root_info_from_json(const nlohmann::json& j);
struct LeafPathRefreshStats {
  double leafFetchMs{0.0};
  double pathFetchMs{0.0};
};
bool committee_set_share_envelope(const std::string& url, const std::string& token, const didzk::ShareEnvelope& env, std::string* err);
bool committee_fetch_share(const std::string& url, const std::string& path, const std::string& token, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, std::uint32_t guardianIndex, didzk::ShareEnvelope* out, std::string* err);
bool committee_invalidate(const std::string& url, const std::string& token, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, const std::string& reason, std::string* err);
nlohmann::json committee_get_share_meta(const std::string& url, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, std::uint32_t guardianIndex);
std::vector<didzk::ShareEnvelope> recover_shares_from_committees(const nlohmann::json& setupJson, const Args& args);
LeafPathRefreshStats refresh_leaf_and_path_best_effort(const Args& args, const std::string& id, nlohmann::json* leafInfo, MerklePathZK* path, const char* logPrefix);
void distribute_share_envelopes_to_committees(const std::vector<std::string>& committeeUrls, const std::string& token, const std::vector<didzk::ShareEnvelope>& envelopes, int n, const std::string& failurePrefix);
void verify_committee_share_meta_active(const std::vector<std::string>& committeeUrls, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, int n, const std::string& failureMessage);
void invalidate_committee_shares(const std::vector<std::string>& committeeUrls, const std::string& token, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, const std::string& reason);
std::string compute_ttss_meta_hash(const std::string& id, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, int n, int t, const std::string& guardianSetHash, const std::string& vkSetHash, const std::string& dealerPkHex, const std::string& rhoCommitHex);
nlohmann::json post_ttss_meta_endpoint(const Args& args, const std::string& endpoint, const nlohmann::json& body, bool tolerateTransportTimeout = false);
nlohmann::json fetch_ttss_meta_json(const Args& args, const std::string& idHash);
bool wait_for_ttss_meta_ready(const Args& args, const std::string& idHash, std::uint64_t ver, std::uint64_t epoch, const std::string& vkSetHash, const std::string& metaHash, nlohmann::json* outMeta, std::string* outDiag);
void save_ttss_setup_artifacts(const std::filesystem::path& dir, const nlohmann::json& setupJson, const didzk::TTSSShareSetupResult& shareSetup);
int run_ttss_setup(const Args& args);
nlohmann::json load_ttss_setup_json(const std::string& path);
int run_ttss_recover(const Args& args);
int run_ttss_recover_and_rotate(const Args& args);
