#include "ttss_flow.hpp"
#include "ttss_meta_registrar.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using Clock = AppClock;

namespace {

struct RotateReadyState {
  RootInfo root;
  MerklePathZK path;
  nlohmann::json leafInfo;
  bool recordObserved{false};
  double waitMs{0.0};
  double probeMs{0.0};
};

RotateReadyState wait_for_rotate_ready_state(const Args& args,
                                             const RecoveryRotateResponse& rotateResp,
                                             const IdentityStateZK& oldState,
                                             const IdentityStateZK& newState,
                                             const RootInfo& oldRoot) {
  RotateReadyState out;
  const auto waitStart = Clock::now();
  bool fastRotateReady = false;
  const bool inlineSnapshotReady = rotateResp.ready &&
      rotateResp.snapshotReady &&
      rotateResp.snapshotVersion >= newState.ver &&
      rotateResp.snapshotRootMatches &&
      !trim_copy(rotateResp.snapshotRoot).empty();

  if (inlineSnapshotReady) {
    out.root.root = rotateResp.snapshotRoot;
    out.root.epoch = rotateResp.snapshotEpoch;
    out.root.depth = oldRoot.depth;
    out.path.root = trim_copy(rotateResp.snapshotPathRoot).empty() ? rotateResp.snapshotRoot : rotateResp.snapshotPathRoot;
    out.path.epoch = rotateResp.snapshotEpoch;
    out.path.depth = oldRoot.depth;
    out.leafInfo = {
        {"ok", 1},
        {"active", 1},
        {"version", rotateResp.snapshotVersion},
        {"leaf", rotateResp.snapshotLeaf},
        {"root", rotateResp.snapshotRoot},
        {"epoch", rotateResp.snapshotEpoch},
    };
    fastRotateReady = true;
    out.recordObserved = true;
    std::cerr << "[ttss_recover_and_rotate] trusted inline ready snapshot"
              << " id=" << oldState.id
              << " root=" << short_hex(rotateResp.snapshotRoot)
              << " epoch=" << rotateResp.snapshotEpoch
              << " version=" << rotateResp.snapshotVersion
              << " diag=" << rotateResp.snapshotDiag
              << "\n";
  } else if (rotateResp.ready && !trim_copy(rotateResp.root).empty()) {
    const auto fastDeadline = Clock::now() + std::chrono::milliseconds(std::min(args.rootWaitTimeoutMs, 2500));
    while (Clock::now() < fastDeadline) {
      std::string pathErr;
      const auto fastProbeStart = Clock::now();
      RootInfo snapRoot;
      if (probe_path_ready(args, oldState.id, newState.ver, &snapRoot, &out.path, &out.leafInfo, &pathErr)) {
        out.probeMs += ms_between(fastProbeStart, Clock::now());
        out.root = snapRoot;
        fastRotateReady = true;
        out.recordObserved = true;
        break;
      }
      out.probeMs += ms_between(fastProbeStart, Clock::now());
      std::this_thread::sleep_for(std::chrono::milliseconds(args.rootPollMs));
    }
  }

  if (!fastRotateReady) {
    std::unordered_map<std::string, std::string> recoveryKv;
    out.recordObserved = wait_for_record_active(args, oldState.id, newState.ver, &recoveryKv);
    if (!out.recordObserved) {
      std::cerr << "[ttss_recover_and_rotate] warning: record not observed active in time; continuing to wait for ready snapshot\n";
    }
    RootInfo pathReadyRoot;
    if (!wait_for_path_ready(args, oldState.id, newState.ver, &pathReadyRoot, &out.path, &out.leafInfo)) {
      throw std::runtime_error("timed_out_waiting_for_ttss_ready_snapshot");
    }
    out.root = pathReadyRoot;
  }
  out.waitMs = ms_between(waitStart, Clock::now());
  return out;
}

struct RotateShareValidation {
  bool oldShareInactiveObserved{false};
  bool newSharesRecoverOk{false};
  double ms{0.0};
  didzk::TTSSRecoverResult newRec;
};

RotateShareValidation validate_rotated_shares(const Args& args,
                                              const std::vector<std::string>& committeeUrls,
                                              const std::string& idHash,
                                              const IdentityStateZK& oldState,
                                              const IdentityStateZK& newState,
                                              const RootInfo& oldRoot,
                                              const RootInfo& newRoot,
                                              const IdentityLocalKeys& newKeys,
                                              int ttssT,
                                              std::string* verifyErr) {
  RotateShareValidation out;
  const auto start = Clock::now();
  if (!committeeUrls.empty()) {
    didzk::ShareEnvelope oldEnvProbe;
    std::string oldProbeErr;
    if (!committee_fetch_share(committeeUrls.front(), "/shareForRecover", args.committeeToken,
                               idHash, oldState.ver, oldRoot.epoch, 1u, &oldEnvProbe, &oldProbeErr)) {
      out.oldShareInactiveObserved = true;
    }
  }

  std::vector<didzk::ShareEnvelope> newShares;
  newShares.reserve(static_cast<std::size_t>(ttssT));
  for (int i = 0; i < ttssT; ++i) {
    didzk::ShareEnvelope env;
    std::string err;
    if (!committee_fetch_share(committeeUrls.at(static_cast<std::size_t>(i)), "/shareForRecover", args.committeeToken,
                               idHash, newState.ver, newRoot.epoch, static_cast<std::uint32_t>(i + 1), &env, &err)) {
      throw std::runtime_error("committee_fetch_new_share_failed: " + err);
    }
    newShares.push_back(std::move(env));
  }
  out.newRec = didzk::ttss_rec_nits_shamir(newShares, verifyErr);
  if (!out.newRec.ok) {
    throw std::runtime_error("ttss_rec_new_failed: " + out.newRec.err + ";verify_err=" + (verifyErr ? *verifyErr : std::string()));
  }
  out.newSharesRecoverOk =
      didzk::normalize_digest_hex32(out.newRec.srecSeedHex) ==
      didzk::normalize_digest_hex32(newKeys.boardSeeds.recoverySeedHex);
  out.ms = ms_between(start, Clock::now());
  if (!out.newSharesRecoverOk) {
    throw std::runtime_error("new_shares_recovery_seed_mismatch");
  }
  return out;
}

nlohmann::json build_next_setup_json(const Args& args,
                                     const std::vector<std::string>& committeeUrls,
                                     const std::vector<std::string>& guardianIds,
                                     const IdentityStateZK& newState,
                                     const IdentityLocalKeys& newKeys,
                                     const RootInfo& oldRoot,
                                     const RootInfo& newRoot,
                                     const nlohmann::json& newLeafInfo,
                                     const std::string& idHash,
                                     const std::string& newLeafLocal,
                                     const std::string& newRhoSeedHex,
                                     const std::string& guardianSetHash,
                                     const std::string& newDealerKeyHex,
                                     const std::string& newDealerPkHex,
                                     const didzk::TTSSShareSetupResult& newShareSetup,
                                     const std::string& newMetaHash,
                                     int ttssN,
                                     int ttssT,
                                     double newShareGenMs,
                                     double newShareDistributeMs,
                                     double setNewTTSSMetaMs,
                                     double newRootWaitMs,
                                     const nlohmann::json& ttssMetaRotateResp,
                                     const nlohmann::json& ttssMetaRotateObserved) {
  return {
      {"mode", "ttss_setup"},
      {"id", newState.id},
      {"idHash", idHash},
      {"ver", newState.ver},
      {"epoch", newRoot.epoch},
      {"n", ttssN},
      {"t", ttssT},
      {"committeeUrls", committeeUrls},
      {"committeeToken", args.committeeToken},
      {"state", identity_state_to_json(newState)},
      {"localKeys", identity_local_keys_to_json(newKeys)},
      {"root", root_info_to_json(newRoot)},
      {"rootBefore", root_info_to_json(oldRoot)},
      {"rootAfter", root_info_to_json(newRoot)},
      {"localLeaf", newLeafLocal},
      {"serviceLeaf", newLeafInfo.value("leaf", std::string())},
      {"ttss",
       {{"rhoSeedHex", newRhoSeedHex},
        {"guardianIds", guardianIds},
        {"guardianSetHash", guardianSetHash},
        {"dealerAttestationKeyHex", newDealerKeyHex},
        {"dealerPkHex", newDealerPkHex},
        {"vkSetHash", newShareSetup.vkSetHash},
        {"rhoCommitHex", newShareSetup.rhoCommitHex},
        {"metaHash", newMetaHash}}},
      {"timings",
       {{"share_gen_ms", newShareGenMs},
        {"committee_distribute_ms", newShareDistributeMs},
        {"register_ttss_meta_ms", setNewTTSSMetaMs},
        {"root_wait_ms", newRootWaitMs}}},
      {"ttssMetaResponse", ttssMetaRotateResp},
      {"ttssMetaObserved", ttssMetaRotateObserved}};
}

nlohmann::json build_rotate_result_json(
    const fs::path& workDir,
    const IdentityStateZK& oldState,
    const IdentityStateZK& newState,
    const RootInfo& oldRoot,
    const RootInfo& newRoot,
    const std::string& idHash,
    const std::string& oldLeafLocal,
    const std::string& newLeafLocal,
    const std::string& currentLeaf,
    bool currentLeafActive,
    bool currentLeafMatchesOld,
    bool currentLeafMatchesNew,
    const didzk::TTSSRecoverResult& rec,
    const std::string& expectedOldSeed,
    const IdentityLocalKeys& newKeys,
    bool oldRecoveredMatches,
    bool rootChanged,
    const RotateShareValidation& shareValidation,
    const RecoveryRotateResponse& rotateResp,
    std::int64_t recoverStageMs,
    double applyRecoveryRotateMs,
    std::int64_t invalidateOldSharesMs,
    double newShareGenMs,
    double newShareDistributeMs,
    double setNewTTSSMetaMs,
    double postRotateStateWaitMs,
    double postRotateLeafFetchMs,
    double postRotatePathFetchMs,
    double postRotateChecksMs,
    double newRootWaitMs,
    double rotateTotalInnerMs,
    double preRotatePrepareMs,
    double rotateMetaWaitMs,
    double rotateReadyProbeMs,
    double rotateClientPreRotateMs,
    double rotateFieldNormalizeMs,
    double rotateLocalLeafComputeMs,
    double rotateRootCompareNormalizeMs) {
  return {
      {"ok", (oldRecoveredMatches && rootChanged && shareValidation.newSharesRecoverOk) ? 1 : 0},
      {"id", oldState.id},
      {"idHash", idHash},
      {"oldVer", oldState.ver},
      {"newVer", newState.ver},
      {"oldEpoch", oldRoot.epoch},
      {"newEpoch", newRoot.epoch},
      {"oldRoot", oldRoot.root},
      {"newRoot", newRoot.root},
      {"rootBefore", oldRoot.root},
      {"rootAfter", newRoot.root},
      {"oldLeaf", oldLeafLocal},
      {"newLeaf", newLeafLocal},
      {"currentLeaf", currentLeaf},
      {"currentLeafActive", currentLeafActive},
      {"currentLeafMatchesOld", currentLeafMatchesOld},
      {"currentLeafMatchesNew", currentLeafMatchesNew},
      {"recoveredOldSeedHex", rec.srecSeedHex},
      {"expectedOldSeedHex", expectedOldSeed},
      {"newRecoverySeedHex", newKeys.boardSeeds.recoverySeedHex},
      {"oldRecoveredMatches", oldRecoveredMatches},
      {"rootChanged", rootChanged},
      {"oldSharesInvalidated", true},
      {"oldShareInactiveObserved", shareValidation.oldShareInactiveObserved},
      {"newSharesRecoverOk", shareValidation.newSharesRecoverOk},
      {"nextSetupJson", (workDir / "next_setup" / "ttss_setup.json").string()},
      {"timings",
       {{"recover_ms", recoverStageMs},
        {"apply_recovery_rotate_ms", applyRecoveryRotateMs},
        {"rotate_response_parse_ms", rotateResp.parseMs},
        {"invalidate_old_shares_ms", invalidateOldSharesMs},
        {"new_share_gen_ms", newShareGenMs},
        {"new_share_distribute_ms", newShareDistributeMs},
        {"set_new_ttss_meta_ms", setNewTTSSMetaMs},
        {"post_rotate_state_wait_ms", postRotateStateWaitMs},
        {"post_rotate_leaf_fetch_ms", postRotateLeafFetchMs},
        {"post_rotate_path_fetch_ms", postRotatePathFetchMs},
        {"old_new_share_validation_ms", shareValidation.ms},
        {"post_rotate_checks_ms", postRotateChecksMs},
        {"new_root_wait_ms", newRootWaitMs},
        {"rotate_total_inner_ms", rotateTotalInnerMs},
        {"pre_rotate_prepare_ms", preRotatePrepareMs},
        {"rotate_meta_wait_ms", rotateMetaWaitMs},
        {"rotate_ready_probe_ms", rotateReadyProbeMs},
        {"client_pre_rotate_ms", rotateClientPreRotateMs},
        {"field_normalize_ms", rotateFieldNormalizeMs},
        {"local_leaf_compute_ms", rotateLocalLeafComputeMs},
        {"root_compare_normalize_ms", rotateRootCompareNormalizeMs}}}};
}

}  // namespace

int run_ttss_recover_and_rotate(const Args& args) {
  const nlohmann::json setupJson = load_ttss_setup_json(args.ttssStatePath);
  const std::string projectRoot = default_project_root(args);
  const fs::path baseWorkDir(default_base_workdir(args, "ttss_phase3"));
  const fs::path workDir = baseWorkDir / (args.id + "_ttss_recover_and_rotate");
  std::error_code ec;
  fs::create_directories(workDir, ec);

  const auto rotateFunctionStart = Clock::now();
  std::int64_t recoverStageMs = 0;
  double applyRecoveryRotateMs = 0.0;
  std::int64_t invalidateOldSharesMs = 0;
  double newShareGenMs = 0.0;
  double newShareDistributeMs = 0.0;
  double setNewTTSSMetaMs = 0.0;
  double newRootWaitMs = 0.0;
  double preRotatePrepareMs = 0.0;
  double rotateMetaWaitMs = 0.0;
  double rotateReadyProbeMs = 0.0;

  IdentityStateZK oldState = identity_state_from_json(setupJson.at("state"));
  IdentityLocalKeys oldKeys = identity_local_keys_from_json(setupJson.at("localKeys"));
  RootInfo oldRoot = root_info_from_json(setupJson.at("root"));
  const std::string idHash = setupJson.value("idHash", std::string());
  const auto committeeUrls = setupJson.at("committeeUrls").get<std::vector<std::string>>();
  const auto guardianIds = setupJson.at("ttss").at("guardianIds").get<std::vector<std::string>>();
  const std::string guardianSetHash = setupJson.at("ttss").value("guardianSetHash", std::string());
  const int ttssN = setupJson.value("n", args.ttssN);
  const int ttssT = setupJson.value("t", args.ttssT);
  if (static_cast<int>(committeeUrls.size()) < ttssN) {
    throw std::runtime_error("committee_url_count_lt_setup_n");
  }

  const auto recoverStageStart = Clock::now();
  std::vector<didzk::ShareEnvelope> oldShares = recover_shares_from_committees(setupJson, args);
  std::string verifyErr;
  didzk::TTSSRecoverResult rec = didzk::ttss_rec_nits_shamir(oldShares, &verifyErr);
  recoverStageMs = elapsed_ms_i64(recoverStageStart);
  if (!rec.ok) {
    throw std::runtime_error("ttss_rec_failed: " + rec.err + ";verify_err=" + verifyErr);
  }
  const std::string expectedOldSeed = oldKeys.boardSeeds.recoverySeedHex;
  const bool oldRecoveredMatches =
      didzk::normalize_digest_hex32(rec.srecSeedHex) == didzk::normalize_digest_hex32(expectedOldSeed);
  if (!oldRecoveredMatches) {
    throw std::runtime_error("recovered_old_seed_mismatch");
  }

  IdentityLocalKeys recoveredOldKeys = oldKeys;
  recoveredOldKeys.boardSeeds.recoverySeedHex = rec.srecSeedHex;
  IdentityLocalKeys newKeys = gen_identity_local_keys();
  IdentityStateZK newState = rotate_identity_state(oldState, newKeys, projectRoot);
  const auto rotateLocalLeafComputeStart = Clock::now();
  const auto rotateBundles = didzk::compute_identity_bundles({oldState, newState}, projectRoot);
  poseidon_debug_log("rotate local bundles computed old_id=" + oldState.id + " old_leaf=" + rotateBundles.at(0).leafHex + " new_leaf=" + rotateBundles.at(1).leafHex);
  const auto& oldBundle = rotateBundles.at(0);
  const auto& newBundle = rotateBundles.at(1);
  const std::string oldLeafLocal = oldBundle.leafHex;
  const std::string newLeafLocal = newBundle.leafHex;
  const double rotateLocalLeafComputeMs = ms_between(rotateLocalLeafComputeStart, Clock::now());
  const std::uint64_t predictedEpoch = oldRoot.epoch + 1;

  const auto preRotatePrepareStart = Clock::now();
  const std::string newRhoSeedHex = random_hex32();
  const std::string newDealerKeyHex = random_hex32();
  const std::string newDealerPkHex = didzk::normalize_digest_hex32(newDealerKeyHex);
  const std::uint64_t issuedAt = static_cast<std::uint64_t>(std::time(nullptr));
  const auto newShareGenStart = Clock::now();
  didzk::TTSSShareSetupResult newShareSetup = didzk::ttss_share_nits_shamir(
      newKeys.boardSeeds.recoverySeedHex,
      static_cast<std::uint32_t>(ttssN),
      static_cast<std::uint32_t>(ttssT),
      newRhoSeedHex,
      oldState.id,
      idHash,
      newState.ver,
      predictedEpoch,
      guardianIds,
      guardianSetHash,
      newDealerKeyHex,
      newDealerPkHex,
      issuedAt,
      0);
  newShareGenMs = ms_between(newShareGenStart, Clock::now());
  std::string newMetaHash = compute_ttss_meta_hash(oldState.id, idHash, newState.ver, predictedEpoch,
                                                   ttssN, ttssT, guardianSetHash,
                                                   newShareSetup.vkSetHash, newDealerPkHex,
                                                   newShareSetup.rhoCommitHex);
  preRotatePrepareMs = ms_between(preRotatePrepareStart, Clock::now());

  std::cerr << "[ttss_recover_and_rotate] applying rotate id=" << oldState.id
            << " oldVer=" << oldState.ver
            << " newVer=" << newState.ver
            << " oldRoot=" << short_hex(oldRoot.root)
            << " oldEpoch=" << oldRoot.epoch << "\n";

  const auto applyRecoveryRotateStart = Clock::now();
  nlohmann::json rotateExtras = {
      {"ttssVkSetHash", newShareSetup.vkSetHash},
      {"ttssMetaHash", newMetaHash},
      {"ttssEpochHint", predictedEpoch},
      {"ttssWait", args.bbAsyncSubmit ? 0 : 1},
      {"ttssConfirmations", args.bbConfirmations},
      {"ttssRequestId", std::string("ttss_meta_rotate_merged:") + oldState.id + ":" + std::to_string(newState.ver)}};
  RecoveryRotateResponse rotateResp = post_apply_recovery_rotate_zk(args, newState, recoveredOldKeys, newKeys, rotateExtras);
  applyRecoveryRotateMs = ms_between(applyRecoveryRotateStart, Clock::now());
  const double rotateClientPreRotateMs = rotateResp.clientPrepMs;
  const double rotateFieldNormalizeMs = rotateResp.fieldNormalizeMs;

  RotateReadyState readyState = wait_for_rotate_ready_state(args, rotateResp, oldState, newState, oldRoot);
  RootInfo newRoot = std::move(readyState.root);
  MerklePathZK newPath = std::move(readyState.path);
  nlohmann::json newLeafInfo = std::move(readyState.leafInfo);
  newRootWaitMs = readyState.waitMs;
  rotateReadyProbeMs = readyState.probeMs;

  if (newRoot.epoch != predictedEpoch) {
    const auto regenStart = Clock::now();
    newShareSetup = didzk::ttss_share_nits_shamir(
        newKeys.boardSeeds.recoverySeedHex,
        static_cast<std::uint32_t>(ttssN),
        static_cast<std::uint32_t>(ttssT),
        newRhoSeedHex,
        oldState.id,
        idHash,
        newState.ver,
        newRoot.epoch,
        guardianIds,
        guardianSetHash,
        newDealerKeyHex,
        newDealerPkHex,
        issuedAt,
        0);
    newShareGenMs += ms_between(regenStart, Clock::now());
    newMetaHash = compute_ttss_meta_hash(oldState.id, idHash, newState.ver, newRoot.epoch,
                                         ttssN, ttssT, guardianSetHash,
                                         newShareSetup.vkSetHash, newDealerPkHex,
                                         newShareSetup.rhoCommitHex);
  }

  const auto invalidateOldSharesStart = Clock::now();
  invalidate_committee_shares(committeeUrls, args.committeeToken, idHash, oldState.ver, oldRoot.epoch,
                              "ttss_rotate_to_ver_" + std::to_string(newState.ver));
  invalidateOldSharesMs = elapsed_ms_i64(invalidateOldSharesStart);

  const auto newCommitteeDistributeStart = Clock::now();
  distribute_share_envelopes_to_committees(committeeUrls, args.committeeToken, newShareSetup.shareEnvelopes,
                                           ttssN, "committee_set_new_share_failed");
  verify_committee_share_meta_active(committeeUrls, idHash, newState.ver, newRoot.epoch,
                                     ttssN, "committee_share_meta_not_active_after_rotate");
  newShareDistributeMs = ms_between(newCommitteeDistributeStart, Clock::now());

  TTSSMetaWork rotateMeta = TTSSMetaRegistrar::apply_or_wait_rotate(
      args, rotateResp, oldState.id, idHash, newState.ver, newRoot.epoch,
      newShareSetup.vkSetHash, newMetaHash);
  nlohmann::json ttssMetaRotateResp = std::move(rotateMeta.resp);
  nlohmann::json ttssMetaRotateObserved = std::move(rotateMeta.observed);
  setNewTTSSMetaMs = rotateMeta.setMs;
  rotateMetaWaitMs = rotateMeta.waitMs;
  const double postRotateStateWaitMs = newRootWaitMs + rotateMetaWaitMs;
  save_json_pretty(workDir / "ttss_meta_rotate_response.json", ttssMetaRotateResp);
  save_json_pretty(workDir / "ttss_meta_rotate_effective.json", ttssMetaRotateObserved);

  const auto postRotateChecksStart = Clock::now();
  const LeafPathRefreshStats refreshStats =
      refresh_leaf_and_path_best_effort(args, oldState.id, &newLeafInfo, &newPath, "ttss_rotate");
  const double postRotateLeafFetchMs = refreshStats.leafFetchMs;
  const double postRotatePathFetchMs = refreshStats.pathFetchMs;

  const RotateShareValidation shareValidation = validate_rotated_shares(
      args, committeeUrls, idHash, oldState, newState, oldRoot, newRoot, newKeys, ttssT, &verifyErr);
  nlohmann::json nextSetupJson = build_next_setup_json(
      args, committeeUrls, guardianIds, newState, newKeys, oldRoot, newRoot, newLeafInfo,
      idHash, newLeafLocal, newRhoSeedHex, guardianSetHash, newDealerKeyHex, newDealerPkHex,
      newShareSetup, newMetaHash, ttssN, ttssT, newShareGenMs, newShareDistributeMs,
      setNewTTSSMetaMs, newRootWaitMs, ttssMetaRotateResp, ttssMetaRotateObserved);
  save_ttss_setup_artifacts(workDir / "next_setup", nextSetupJson, newShareSetup);
  save_json_pretty(workDir / "next_setup" / "ttss_meta_register_response.json", ttssMetaRotateResp);
  save_json_pretty(workDir / "next_setup" / "ttss_meta_effective.json", ttssMetaRotateObserved);

  std::string currentLeaf = newLeafInfo.value("leaf", std::string());
  bool currentLeafActive = newLeafInfo.value("active", 0) != 0;
  std::string fieldErr;
  if (!is_valid_field_hex(args, currentLeaf, "run_ttss_recover_and_rotate", "currentLeaf.initial", &fieldErr)) {
    poseidon_debug_log(std::string("rotate compare invalid currentLeaf before refetch: ") + fieldErr +
                       " leafInfo=" + newLeafInfo.dump());
    try {
      nlohmann::json fetchedLeaf = fetch_leaf_json(args, oldState.id);
      if (fetchedLeaf.is_object() && fetchedLeaf.value("ok", 0) != 0) {
        newLeafInfo = std::move(fetchedLeaf);
        currentLeaf = newLeafInfo.value("leaf", std::string());
        currentLeafActive = newLeafInfo.value("active", 0) != 0;
      }
    } catch (const std::exception& e) {
      poseidon_debug_log(std::string("rotate compare refetch leaf failed: ") + e.what());
    }
  }
  std::string canonCurrentLeaf, canonNewRoot, canonOldRoot;
  if (!try_canonicalize_field(args, currentLeaf, "run_ttss_recover_and_rotate", "currentLeaf.after_ready", &canonCurrentLeaf, &fieldErr)) {
    throw std::runtime_error(std::string("invalid_currentLeaf_after_ready: ") + fieldErr +
                             " leafInfo=" + newLeafInfo.dump());
  }
  if (!try_canonicalize_field(args, newRoot.root, "run_ttss_recover_and_rotate", "newRoot.root", &canonNewRoot, &fieldErr)) {
    throw std::runtime_error(std::string("invalid_newRoot_after_ready: ") + fieldErr +
                             " newRoot.root='" + newRoot.root + "'");
  }
  if (!try_canonicalize_field(args, oldRoot.root, "run_ttss_recover_and_rotate", "oldRoot.root", &canonOldRoot, &fieldErr)) {
    throw std::runtime_error(std::string("invalid_oldRoot_before_compare: ") + fieldErr +
                             " oldRoot.root='" + oldRoot.root + "'");
  }
  poseidon_debug_log(std::string("rotate compare canonical currentLeaf=") + canonCurrentLeaf +
                     " newRoot=" + canonNewRoot + " oldRoot=" + canonOldRoot +
                     " currentLeafActive=" + (currentLeafActive ? "1" : "0"));
  const auto rotateRootCompareNormalizeStart = Clock::now();
  const auto rotateCompareFields = didzk::normalize_fields_native({canonCurrentLeaf, canonNewRoot, canonOldRoot}, projectRoot);
  const bool currentLeafMatchesOld = didzk::field_equal(rotateCompareFields.at(0), oldBundle.leafField);
  const bool currentLeafMatchesNew = didzk::field_equal(rotateCompareFields.at(0), newBundle.leafField);
  const bool rootChanged = !didzk::field_equal(rotateCompareFields.at(1), rotateCompareFields.at(2)) &&
                           currentLeafActive && currentLeafMatchesNew && !currentLeafMatchesOld;
  poseidon_debug_log("rotate compare currentLeafMatchesOld=" + std::string(currentLeafMatchesOld ? "1" : "0") +
                     " currentLeafMatchesNew=" + std::string(currentLeafMatchesNew ? "1" : "0") +
                     " currentLeafActive=" + std::string(currentLeafActive ? "1" : "0") +
                     " rootChanged=" + std::string(rootChanged ? "1" : "0"));
  const double rotateRootCompareNormalizeMs = ms_between(rotateRootCompareNormalizeStart, Clock::now());
  const bool ok = oldRecoveredMatches && rootChanged && shareValidation.newSharesRecoverOk;
  const double postRotateChecksMs = ms_between(postRotateChecksStart, Clock::now());

  nlohmann::json result = build_rotate_result_json(
      workDir, oldState, newState, oldRoot, newRoot, idHash, oldLeafLocal, newLeafLocal,
      currentLeaf, currentLeafActive, currentLeafMatchesOld, currentLeafMatchesNew,
      rec, expectedOldSeed, newKeys, oldRecoveredMatches, rootChanged, shareValidation,
      rotateResp, recoverStageMs, applyRecoveryRotateMs, invalidateOldSharesMs,
      newShareGenMs, newShareDistributeMs, setNewTTSSMetaMs, postRotateStateWaitMs,
      postRotateLeafFetchMs, postRotatePathFetchMs, postRotateChecksMs, newRootWaitMs,
      ms_between(rotateFunctionStart, Clock::now()), preRotatePrepareMs, rotateMetaWaitMs,
      rotateReadyProbeMs, rotateClientPreRotateMs, rotateFieldNormalizeMs,
      rotateLocalLeafComputeMs, rotateRootCompareNormalizeMs);
  save_json_pretty(workDir / "ttss_rotate_result.json", result);
  std::cout << "[ttss_recover_and_rotate] ok=" << (ok ? 1 : 0)
            << " rootChanged=" << (rootChanged ? 1 : 0)
            << " oldRecoveredMatches=" << (oldRecoveredMatches ? 1 : 0)
            << " newSharesRecoverOk=" << (shareValidation.newSharesRecoverOk ? 1 : 0)
            << " new_root=" << newRoot.root
            << " workdir=" << workDir.string() << "\n";
  return ok ? 0 : 1;
}
