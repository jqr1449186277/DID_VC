#include "ttss_flow.hpp"
#include "ttss_meta_registrar.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using Clock = AppClock;

namespace {

struct RootBeforeProbe {
  RootInfo root;
  bool available{false};
};

RootBeforeProbe fetch_setup_root_before(const Args& args) {
  RootBeforeProbe out;
  for (int attempt = 0; attempt < 3 && !out.available; ++attempt) {
    try {
      out.root = fetch_root(args);
      out.available = !trim_copy(out.root.root).empty();
      if (!out.available) {
        poseidon_debug_log("setup rootBefore unavailable attempt=" + std::to_string(attempt + 1));
      }
    } catch (const std::exception& e) {
      poseidon_debug_log(std::string("setup fetch_root(before) failed attempt=") +
                         std::to_string(attempt + 1) + " err=" + e.what());
    }
    if (!out.available && attempt + 1 < 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  return out;
}

std::uint64_t json_u64_fallback(const nlohmann::json& j,
                                const char* k1,
                                const char* k2,
                                std::uint64_t fb) {
  if (!j.is_object()) return fb;
  for (const char* key : {k1, k2}) {
    auto it = j.find(key);
    if (it == j.end()) continue;
    if (it->is_number_unsigned() || it->is_number_integer()) return static_cast<std::uint64_t>(it->get<long long>());
    if (it->is_string()) {
      try {
        return static_cast<std::uint64_t>(std::stoull(it->get<std::string>()));
      } catch (const std::exception& e) {
        poseidon_debug_log(std::string("json_u64_fallback parse failed key=") + key + " err=" + e.what());
      }
    }
  }
  return fb;
}

std::string json_string_fallback(const nlohmann::json& j,
                                 const char* k1,
                                 const char* k2,
                                 const std::string& fb) {
  if (!j.is_object()) return fb;
  for (const char* key : {k1, k2}) {
    auto it = j.find(key);
    if (it != j.end() && it->is_string()) return it->get<std::string>();
  }
  return fb;
}

nlohmann::json build_setup_result_json(
    const Args& args,
    const std::vector<std::string>& committeeUrls,
    const std::string& idHash,
    const IdentityStateZK& state,
    const IdentityLocalKeys& localKeys,
    const RootInfo& root,
    const RootInfo& rootBefore,
    const std::string& localLeaf,
    const std::string& serviceLeaf,
    bool setupRootChanged,
    bool usePredictedSetup,
    bool registerMetaMergedUsable,
    const RegisterResponse& reg,
    bool deferredMetaObserved,
    const std::string& rhoSeedHex,
    const std::vector<std::string>& guardianIds,
    const std::string& guardianSetHash,
    const std::string& dealerAttestationKeyHex,
    const std::string& dealerPkHex,
    const didzk::TTSSShareSetupResult& shareSetup,
    const std::string& metaHash,
    double registerZkMs,
    double shareGenMs,
    double committeeDistributeMs,
    double registerTTSSMetaMs,
    double deferredMetaObserveMs,
    double leafFetchMs,
    double pathFetchMs,
    double postMetaVerifyMs,
    double postSetupStateWaitMs,
    double rootWaitMs,
    double setupTotalInnerMs,
    const ReadyWaitBreakdown& setupWaitDbg,
    double setupClientPreRegisterMs,
    double setupFieldNormalizeMs,
    double setupLocalLeafComputeMs,
    double setupRootCompareNormalizeMs,
    const nlohmann::json& ttssMetaResp,
    const nlohmann::json& ttssMetaObserved) {
  return {
      {"mode", "ttss_setup"},
      {"id", args.id},
      {"idHash", idHash},
      {"ver", state.ver},
      {"epoch", root.epoch},
      {"n", args.ttssN},
      {"t", args.ttssT},
      {"committeeUrls", committeeUrls},
      {"committeeToken", args.committeeToken},
      {"state", identity_state_to_json(state)},
      {"localKeys", identity_local_keys_to_json(localKeys)},
      {"root", root_info_to_json(root)},
      {"rootBefore", root_info_to_json(rootBefore)},
      {"rootAfter", root_info_to_json(root)},
      {"localLeaf", localLeaf},
      {"serviceLeaf", serviceLeaf},
      {"rootChanged", setupRootChanged},
      {"predictedSetupUsed", usePredictedSetup},
      {"ttssMetaMergedInRegister", registerMetaMergedUsable},
      {"ttssMetaDeferredScheduled", reg.ttssDeferredScheduled},
      {"ttssMetaDeferredObserved", deferredMetaObserved},
      {"ttss",
       {{"rhoSeedHex", rhoSeedHex},
        {"guardianIds", guardianIds},
        {"guardianSetHash", guardianSetHash},
        {"dealerAttestationKeyHex", dealerAttestationKeyHex},
        {"dealerPkHex", dealerPkHex},
        {"vkSetHash", shareSetup.vkSetHash},
        {"rhoCommitHex", shareSetup.rhoCommitHex},
        {"metaHash", metaHash}}},
      {"timings",
       {{"register_zk_ms", registerZkMs},
        {"share_gen_ms", shareGenMs},
        {"committee_distribute_ms", committeeDistributeMs},
        {"register_ttss_meta_ms", registerTTSSMetaMs},
        {"register_ttss_meta_observe_ms", deferredMetaObserveMs},
        {"predicted_setup_used", usePredictedSetup ? 1 : 0},
        {"register_ttss_meta_merged", registerMetaMergedUsable ? 1 : 0},
        {"register_ttss_meta_deferred_scheduled", reg.ttssDeferredScheduled ? 1 : 0},
        {"register_ttss_meta_deferred_ready", deferredMetaObserved ? 1 : 0},
        {"leaf_fetch_ms", leafFetchMs},
        {"path_fetch_ms", pathFetchMs},
        {"post_meta_verify_ms", postMetaVerifyMs},
        {"post_setup_state_wait_ms", postSetupStateWaitMs},
        {"root_wait_ms", rootWaitMs},
        {"setup_total_inner_ms", setupTotalInnerMs},
        {"register_status_poll_ms", setupWaitDbg.statusPollMs},
        {"ready_probe_path_leaf_ms", setupWaitDbg.readyProbeMs + setupWaitDbg.chooseRootMs},
        {"ready_wait_sleep_ms", setupWaitDbg.sleepMs},
        {"client_pre_register_ms", setupClientPreRegisterMs},
        {"field_normalize_ms", setupFieldNormalizeMs},
        {"local_leaf_compute_ms", setupLocalLeafComputeMs},
        {"root_compare_normalize_ms", setupRootCompareNormalizeMs}}},
      {"ttssMetaResponse", ttssMetaResp},
      {"ttssMetaObserved", ttssMetaObserved}};
}

}  // namespace


int run_ttss_setup(const Args& args) {
  const auto committeeUrls = didzk::split_csv_nonempty(args.committeeUrls);
  if (static_cast<int>(committeeUrls.size()) < args.ttssN) {
    throw std::runtime_error("committee_url_count_lt_ttss_n");
  }
  const std::string projectRoot = default_project_root(args);
  const fs::path baseWorkDir(default_base_workdir(args, "ttss_phase3"));
  const fs::path workDir = baseWorkDir / (args.id + "_ttss_setup");
  std::error_code ec;
  fs::create_directories(workDir, ec);

  const RootBeforeProbe rootBeforeProbe = fetch_setup_root_before(args);
  const RootInfo rootBefore = rootBeforeProbe.root;
  const bool rootBeforeAvailable = rootBeforeProbe.available;

  IdentityLocalKeys localKeys = gen_identity_local_keys();
  IdentityStateZK state = gen_identity_state(args.id, localKeys, projectRoot);

  const auto guardianIds = guardian_ids_for_n(args.ttssN);
  const std::string predictedIdHash = did_id_hash_for_ttss(args.id);
  const std::string predictedGuardianSetHash = hash32_hex_from_text(std::string("TTSS-GSET|") + predictedIdHash + "|" + std::to_string(args.ttssN));
  const std::string predictedRhoSeedHex = random_hex32();
  const std::string predictedDealerAttestationKeyHex = random_hex32();
  const std::string predictedDealerPkHex = didzk::normalize_digest_hex32(predictedDealerAttestationKeyHex);
  const std::uint64_t predictedIssuedAt = static_cast<std::uint64_t>(std::time(nullptr));
  bool predictedSetupPrepared = false;
  std::uint64_t predictedEpoch = 0;
  std::string predictedMetaHash;
  didzk::TTSSShareSetupResult predictedShareSetup;
  nlohmann::json registerExtra = nlohmann::json::object();
  if (rootBeforeAvailable && !trim_copy(rootBefore.root).empty()) {
    predictedEpoch = rootBefore.epoch + 1;
    predictedShareSetup = didzk::ttss_share_nits_shamir(
        localKeys.boardSeeds.recoverySeedHex,
        static_cast<std::uint32_t>(args.ttssN),
        static_cast<std::uint32_t>(args.ttssT),
        predictedRhoSeedHex,
        args.id,
        predictedIdHash,
        state.ver,
        predictedEpoch,
        guardianIds,
        predictedGuardianSetHash,
        predictedDealerAttestationKeyHex,
        predictedDealerPkHex,
        predictedIssuedAt,
        0);
    predictedMetaHash = compute_ttss_meta_hash(args.id, predictedIdHash, state.ver, predictedEpoch,
                                               args.ttssN, args.ttssT, predictedGuardianSetHash,
                                               predictedShareSetup.vkSetHash, predictedDealerPkHex,
                                               predictedShareSetup.rhoCommitHex);
    registerExtra = {
        {"ttssVkSetHash", predictedShareSetup.vkSetHash},
        {"ttssMetaHash", predictedMetaHash},
        {"ttssEpochHint", predictedEpoch},
        {"ttssWait", 1},
        {"ttssConfirmations", 1},
        {"ttssSetupMode", "deferred"},
        {"ttssRequestId", std::string("ttss_meta:") + args.id + ":" + std::to_string(state.ver)},
    };
    predictedSetupPrepared = true;
  }

  const auto setupFunctionStart = Clock::now();
  const auto registerZkStart = Clock::now();
  RegisterResponse reg = post_register_zk(args, state, localKeys, registerExtra);
  const double registerZkMs = ms_between(registerZkStart, Clock::now());
  const double setupClientPreRegisterMs = reg.clientPrepMs;
  const double setupFieldNormalizeMs = reg.fieldNormalizeMs;
  RootInfo root;
  MerklePathZK path;
  nlohmann::json leafInfo;
  ReadyWaitBreakdown setupWaitDbg;
  const auto postSetupStateWaitStart = Clock::now();
  if (!wait_for_identity_ready_after_register(args, args.id, reg, state.ver, &root, &path, &leafInfo, &setupWaitDbg)) {
    throw std::runtime_error("ttss_setup_register_not_ready");
  }
  const double postSetupStateWaitMs = ms_between(postSetupStateWaitStart, Clock::now());
  const double rootWaitMs = registerZkMs + postSetupStateWaitMs;

  const LeafPathRefreshStats refreshStats =
      refresh_leaf_and_path_best_effort(args, args.id, &leafInfo, &path, "ttss_setup");
  const double leafFetchMs = refreshStats.leafFetchMs;
  const double pathFetchMs = refreshStats.pathFetchMs;

  const auto setupLocalLeafComputeStart = Clock::now();
  const auto localBundle = didzk::compute_identity_bundle_with_path(state, path, projectRoot);
  poseidon_debug_log("setup local bundle computed id=" + state.id + " leaf=" + localBundle.leafHex + " root=" + localBundle.rootHex);
  const std::string localLeaf = localBundle.leafHex;
  const std::string localRoot = localBundle.rootHex;
  const double setupLocalLeafComputeMs = ms_between(setupLocalLeafComputeStart, Clock::now());
  const std::string serviceLeaf = leafInfo.value("leaf", std::string());
  const auto setupRootCompareNormalizeStart = Clock::now();
  const auto serviceLeafField = didzk::normalize_field_native(serviceLeaf, projectRoot);
  const auto serviceRootField = didzk::normalize_field_native(root.root, projectRoot);
  const bool setupLeafMatches = didzk::field_equal(localBundle.leafField, serviceLeafField);
  bool setupRootChanged = didzk::field_equal(localBundle.rootField, serviceRootField);
  if (rootBeforeAvailable && !trim_copy(rootBefore.root).empty()) {
    const auto rootBeforeField = didzk::normalize_field_native(rootBefore.root, projectRoot);
    setupRootChanged =
        !didzk::field_equal(serviceRootField, rootBeforeField) &&
        didzk::field_equal(localBundle.rootField, serviceRootField);
  } else {
    poseidon_debug_log("setup rootBefore unavailable; skip root-changed comparison and only verify local/service root equality");
  }
  const double setupRootCompareNormalizeMs = ms_between(setupRootCompareNormalizeStart, Clock::now());
  if (!setupLeafMatches) {
    throw std::runtime_error("ttss_setup_leaf_mismatch");
  }
  if (!setupRootChanged) {
    throw std::runtime_error("ttss_setup_root_not_changed_or_not_locally_verifiable");
  }

  const std::uint64_t chainVer = json_u64_fallback(leafInfo, "ver", "version", state.ver);
  state.ver = chainVer;
  state.active = true;

  const std::string idHash = json_string_fallback(leafInfo, "idHash", "id_hash", std::string());
  if (idHash.empty()) {
    throw std::runtime_error("ttss_setup_missing_leaf_idHash");
  }
  const std::string normalizedIdHash = didzk::normalize_digest_hex32(idHash);
  const bool usePredictedSetup = predictedSetupPrepared &&
      state.ver == 0 &&
      root.epoch == predictedEpoch &&
      normalizedIdHash == didzk::normalize_digest_hex32(predictedIdHash);

  std::string guardianSetHash = hash32_hex_from_text(std::string("TTSS-GSET|") + idHash + "|" + std::to_string(args.ttssN));
  std::string rhoSeedHex;
  std::string dealerAttestationKeyHex;
  std::string dealerPkHex;
  std::uint64_t issuedAt = 0;
  didzk::TTSSShareSetupResult shareSetup;
  std::string metaHash;
  double shareGenMs = 0.0;
  if (usePredictedSetup) {
    guardianSetHash = predictedGuardianSetHash;
    rhoSeedHex = predictedRhoSeedHex;
    dealerAttestationKeyHex = predictedDealerAttestationKeyHex;
    dealerPkHex = predictedDealerPkHex;
    issuedAt = predictedIssuedAt;
    shareSetup = predictedShareSetup;
    metaHash = predictedMetaHash;
  } else {
    rhoSeedHex = random_hex32();
    dealerAttestationKeyHex = random_hex32();
    dealerPkHex = didzk::normalize_digest_hex32(dealerAttestationKeyHex);
    issuedAt = static_cast<std::uint64_t>(std::time(nullptr));
    const auto shareGenStart = Clock::now();
    shareSetup = didzk::ttss_share_nits_shamir(
        localKeys.boardSeeds.recoverySeedHex,
        static_cast<std::uint32_t>(args.ttssN),
        static_cast<std::uint32_t>(args.ttssT),
        rhoSeedHex,
        args.id,
        idHash,
        state.ver,
        root.epoch,
        guardianIds,
        guardianSetHash,
        dealerAttestationKeyHex,
        dealerPkHex,
        issuedAt,
        0);
    shareGenMs = ms_between(shareGenStart, Clock::now());
    metaHash = compute_ttss_meta_hash(args.id, idHash, state.ver, root.epoch,
                                      args.ttssN, args.ttssT, guardianSetHash,
                                      shareSetup.vkSetHash, dealerPkHex,
                                      shareSetup.rhoCommitHex);
  }

  const auto committeeDistributeStart = Clock::now();
  distribute_share_envelopes_to_committees(committeeUrls, args.committeeToken, shareSetup.shareEnvelopes,
                                           args.ttssN, "committee_set_share_envelope_failed");
  verify_committee_share_meta_active(committeeUrls, idHash, state.ver, root.epoch,
                                     args.ttssN, "committee_share_meta_not_active_after_setup");
  const bool registerMetaMergedUsable = reg.ttssMerged &&
      usePredictedSetup &&
      (reg.ttssEpoch == 0 || reg.ttssEpoch == root.epoch);
  const bool shouldObserveDeferredMeta = reg.ttssDeferredScheduled && !registerMetaMergedUsable;
  const std::string ttssMetaRequestId = std::string("ttss_meta:") + args.id + ":" + std::to_string(state.ver);

  const auto registerTTSSMetaStart = Clock::now();
  auto ttssMetaFuture = TTSSMetaRegistrar::start_setup(
      args, reg, registerMetaMergedUsable, shouldObserveDeferredMeta,
      idHash, state.ver, root.epoch, shareSetup.vkSetHash, metaHash,
      ttssMetaRequestId, registerTTSSMetaStart);

  const double committeeDistributeMs = ms_between(committeeDistributeStart, Clock::now());

  TTSSMetaWork ttssMetaWork = ttssMetaFuture.get();
  nlohmann::json ttssMetaResp = std::move(ttssMetaWork.resp);
  nlohmann::json ttssMetaObserved = std::move(ttssMetaWork.observed);
  std::string ttssMetaDiag = std::move(ttssMetaWork.diag);
  bool deferredMetaObserved = ttssMetaWork.deferredObserved;
  double deferredMetaObserveMs = ttssMetaWork.deferredObserveMs;
  const double registerTTSSMetaMs = ttssMetaWork.totalMs;
  const auto postMetaVerifyStart = Clock::now();
  const bool setupMetaFromResponse =
      TTSSMetaRegistrar::response_matches(ttssMetaResp, shareSetup.vkSetHash, metaHash, state.ver, root.epoch);
  if (setupMetaFromResponse) {
    ttssMetaObserved = ttssMetaResp.at("effectiveMeta");
    if (registerMetaMergedUsable) {
      ttssMetaDiag = "merged_in_registerZk";
    } else if (deferredMetaObserved) {
      ttssMetaDiag = "deferred_ready_before_registerTTSSMeta";
    } else {
      ttssMetaDiag = "response_effective_meta";
    }
  } else if (!wait_for_ttss_meta_ready(args, idHash, state.ver, root.epoch, shareSetup.vkSetHash, metaHash,
                                       &ttssMetaObserved, &ttssMetaDiag)) {
    throw std::runtime_error("ttss_meta_not_ready_after_setup: " + ttssMetaDiag);
  }
  const double postMetaVerifyMs = ms_between(postMetaVerifyStart, Clock::now());
  save_json_pretty(workDir / "ttss_meta_register_response.json", ttssMetaResp);
  save_json_pretty(workDir / "ttss_meta_effective.json", ttssMetaObserved);

  nlohmann::json setupJson = build_setup_result_json(
      args, committeeUrls, idHash, state, localKeys, root, rootBefore, localLeaf, serviceLeaf,
      setupRootChanged, usePredictedSetup, registerMetaMergedUsable, reg,
      deferredMetaObserved, rhoSeedHex, guardianIds, guardianSetHash,
      dealerAttestationKeyHex, dealerPkHex, shareSetup, metaHash, registerZkMs,
      shareGenMs, committeeDistributeMs, registerTTSSMetaMs, deferredMetaObserveMs,
      leafFetchMs, pathFetchMs, postMetaVerifyMs, postSetupStateWaitMs, rootWaitMs,
      ms_between(setupFunctionStart, Clock::now()), setupWaitDbg,
      setupClientPreRegisterMs, setupFieldNormalizeMs, setupLocalLeafComputeMs,
      setupRootCompareNormalizeMs, ttssMetaResp, ttssMetaObserved);

  save_ttss_setup_artifacts(workDir, setupJson, shareSetup);
  std::cout << "[ttss_setup] ok id=" << args.id
            << " setup_json=" << (workDir / "ttss_setup.json").string()
            << " root=" << root.root
            << " epoch=" << root.epoch
            << " vkSetHash=" << shareSetup.vkSetHash << "\n";
  return 0;
}
