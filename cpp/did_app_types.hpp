#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "main_cli.hpp"
#include "merkle_poseidon.hpp"
#include "process_utils.hpp"
#include "share_envelope.hpp"
#include "ttss_nits_shamir.hpp"
#include "zk_backend.hpp"

namespace didzk {
std::string build_session_transcript(const std::string& ctxHash,
                                     const std::vector<std::string>& publicSignals,
                                     std::uint64_t epoch);
std::string sign_session_transcript_hex(const std::string& sessionSkHex,
                                        const std::string& transcript);
bool verify_bundle_with_session(const ZkBackendPaths& rawPaths,
                                const ZkProofBundle& bundle,
                                const std::string& currentRoot,
                                const std::string& sessionPkHex,
                                const std::string& expectedCtxHash,
                                std::uint64_t expectedEpoch,
                                std::string* err = nullptr);
}  // namespace didzk

using didzk::IdentityStateZK;
using didzk::MerklePathZK;
using didzk::PublicSignalView;
using didzk::SessionContextZK;
using didzk::VerifyResult;
using didzk::ZkBackendOptions;
using didzk::ZkBackendPaths;
using didzk::ZkProofBundle;
using didzk::ZkRunArtifacts;
using didzk::ZkWitnessInput;

struct UrlParts {
  std::string scheme{"http"};
  std::string host{"127.0.0.1"};
  int port{80};
  std::string basePath;
};

struct RootInfo {
  std::string root;
  std::uint64_t epoch{0};
  int depth{0};
};

struct RegisterResponse {
  bool ok{false};
  bool accepted{false};
  bool ready{false};
  bool ttssMerged{false};
  bool ttssDeferredScheduled{false};
  std::string opId;
  std::string requestKey;
  std::string txHash;
  std::string status;
  std::string owner;
  std::string recovery;
  std::string root;
  std::string ttssVkSetHash;
  std::string ttssMetaHash;
  std::string ttssMetaTxHash;
  std::uint64_t epoch{0};
  std::uint64_t version{0};
  std::uint64_t ttssEpoch{0};
  double submitMs{0.0};
  double confirmMs{0.0};
  std::int64_t parseMs{0};
  double clientPrepMs{0.0};
  double fieldNormalizeMs{0.0};
};

struct RecoveryRotateResponse {
  bool ok{false};
  bool accepted{false};
  bool ready{false};
  bool ttssMerged{false};
  bool snapshotReady{false};
  bool snapshotRootMatches{false};
  std::string status;
  std::string txHash;
  std::string root;
  std::string snapshotRoot;
  std::string snapshotLeaf;
  std::string snapshotPathRoot;
  std::string ttssVkSetHash;
  std::string ttssMetaHash;
  std::string ttssMetaTxHash;
  std::string snapshotDiag;
  std::uint64_t epoch{0};
  std::uint64_t version{0};
  std::uint64_t ttssEpoch{0};
  std::uint64_t snapshotEpoch{0};
  std::uint64_t snapshotVersion{0};
  double submitMs{0.0};
  double confirmMs{0.0};
  std::int64_t parseMs{0};
  double clientPrepMs{0.0};
  double fieldNormalizeMs{0.0};
};

struct RegisterStatusInfo {
  bool ok{false};
  bool accepted{false};
  bool ready{false};
  bool cacheStale{false};
  std::string opId;
  std::string requestKey;
  std::string kind;
  std::string id;
  std::string status;
  std::string txHash;
  std::string root;
  std::string lastError;
  std::uint64_t epoch{0};
  std::uint64_t version{0};
};

struct ReadySnapshotInfo {
  bool ok{false};
  bool ready{false};
  bool cacheStale{false};
  bool leafOk{false};
  bool active{false};
  bool rootMatches{false};
  bool recordObserved{false};
  bool recordActive{false};
  std::string id;
  std::string idHash;
  std::string root;
  std::string pathRoot;
  std::string leaf;
  std::string cid;
  std::string pkNormHash;
  std::string pkRecHash;
  std::string diag;
  std::uint64_t minVersion{0};
  std::uint64_t version{0};
  std::uint64_t epoch{0};
  std::uint64_t recordVersion{0};
  int depth{0};
  std::vector<std::string> pathElements;
  std::vector<std::uint8_t> pathIndex;
};

struct SessionKeyPair {
  std::string pkHex;
  std::string skHex;
};

struct SeedPair {
  std::string ownerSeedHex;
  std::string recoverySeedHex;
};

struct IdentityLocalKeys {
  SessionKeyPair normalEd25519;
  SessionKeyPair recoveryEd25519;
  SeedPair boardSeeds;
};

struct RunRow {
  std::string mode;
  std::string id;
  int depth{0};
  int bbEach{0};
  double pathFetchMs{0.0};
  double witnessMs{0.0};
  double proveMs{0.0};
  double verifyMs{0.0};
  std::size_t proofBytes{0};
  std::size_t publicBytes{0};
  std::string recoverCase{"none"};
  int oldProofValid{-1};
  int newProofValid{-1};
  int ok{0};
};

struct AuthRoundResult {
  IdentityStateZK state;
  IdentityLocalKeys localKeys;
  RootInfo root;
  MerklePathZK path;
  SessionContextZK sess;
  SessionKeyPair sessionKeys;
  ZkProofBundle bundle;
  std::string workDir;
  RunRow row;
};

struct ReadyWaitBreakdown {
  double statusPollMs{0.0};
  double readyProbeMs{0.0};
  double chooseRootMs{0.0};
  double sleepMs{0.0};
};
