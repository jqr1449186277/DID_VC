#pragma once

#include "did_app_common.hpp"
#include "http_transport.hpp"

bool try_canonicalize_field(const Args& args, const std::string& raw, const std::string& where, const std::string& field, std::string* out, std::string* err = nullptr);
std::string canonicalize_field_or_throw(const Args& args, const std::string& raw, const std::string& where, const std::string& field);
bool is_valid_field_hex(const Args& args, const std::string& raw, const std::string& where, const std::string& field, std::string* err = nullptr);
RootInfo fetch_root(const Args& args);
MerklePathZK fetch_path(const Args& args, const std::string& id);
nlohmann::json fetch_leaf_json(const Args& args, const std::string& id);
ReadySnapshotInfo fetch_ready_snapshot(const Args& args, const std::string& id, std::uint64_t minVersion);
bool fetch_register_status(const Args& args, const std::string& opId, const std::string& requestKey, RegisterStatusInfo* out, std::string* err);
bool status_reports_ready(const RegisterStatusInfo& st);
bool status_reports_failed(const RegisterStatusInfo& st);
std::string status_error_text(const RegisterStatusInfo& st);
bool status_reports_cache_stale(const RegisterStatusInfo& st);
std::string short_hex(const std::string& s);
std::string redact_hex_secret(const std::string& s);
bool wait_for_record_active(const Args& args, const std::string& id, std::uint64_t minVersion, std::unordered_map<std::string, std::string>* outKv);
bool wait_for_path_ready(const Args& args, const std::string& id, std::uint64_t minVersion, RootInfo* outRoot, MerklePathZK* outPath, nlohmann::json* outLeaf);
bool probe_path_ready(const Args& args, const std::string& id, std::uint64_t minVersion, RootInfo* outRoot, MerklePathZK* outPath, nlohmann::json* outLeaf, std::string* outErr = nullptr);
bool wait_for_identity_ready_after_register(const Args& args, const std::string& id, const RegisterResponse& reg, std::uint64_t minVersion, RootInfo* outRoot, MerklePathZK* outPath, nlohmann::json* outLeaf, ReadyWaitBreakdown* dbg = nullptr);
RegisterResponse post_register_zk(const Args& args, const IdentityStateZK& st, const IdentityLocalKeys& keys, const nlohmann::json& extraBody = nlohmann::json::object());
RecoveryRotateResponse post_apply_recovery_rotate_zk(const Args& args, const IdentityStateZK& newState, const IdentityLocalKeys& oldKeys, const IdentityLocalKeys& newKeys, const nlohmann::json& extraBody = nlohmann::json::object());
bool wait_for_new_root(const Args& args, const RootInfo& oldRoot, RootInfo& outRoot);
