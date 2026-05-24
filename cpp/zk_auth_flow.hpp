#pragma once

#include "bb_client.hpp"

void save_round_metadata(const std::filesystem::path& workDir, const IdentityStateZK& st, const IdentityLocalKeys& keys, const RootInfo& root, const MerklePathZK& path, const SessionContextZK& sess, const SessionKeyPair& sessionKeys, const ZkProofBundle& bundle);
AuthRoundResult run_one_zk_auth_round(const Args& args, const std::string& id, const std::string& baseWorkDir);
int run_zk_auth_e2e(const Args& args);
int run_zk_recovery_e2e(const Args& args);
