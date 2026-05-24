#include "zk_auth_flow.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using Clock = AppClock;

void save_round_metadata(const std::filesystem::path& workDir,
                         const IdentityStateZK& st,
                         const IdentityLocalKeys& keys,
                         const RootInfo& root,
                         const MerklePathZK& path,
                         const SessionContextZK& sess,
                         const SessionKeyPair& sessionKeys,
                         const ZkProofBundle& bundle) {
  nlohmann::json j;
  j["identity"] = {
      {"id", st.id},
      {"sid", st.sid},
      {"rho", st.rho},
      {"cid", st.cid},
      {"pkNormHash", st.pkNormHash},
      {"pkRecHash", st.pkRecHash},
      {"ver", st.ver},
      {"active", st.active ? 1 : 0},
  };
  j["boardSeeds"] = {
      {"ownerSeedHex", keys.boardSeeds.ownerSeedHex},
      {"recoverySeedHex", keys.boardSeeds.recoverySeedHex},
  };
  j["root"] = {{"root", root.root}, {"epoch", root.epoch}, {"depth", root.depth}};
  j["path"] = {{"root", path.root}, {"epoch", path.epoch}, {"depth", path.depth},
                {"pathElements", path.pathElements}, {"pathIndex", path.pathIndex}};
  j["session"] = {
      {"ctxHash", sess.ctxHash},
      {"sessPk", sessionKeys.pkHex},
      {"sessPkHash", sess.sessPkHash},
      {"epoch", sess.epoch},
      {"sessionSigHex", bundle.sessionSigHex},
  };
  j["proof"] = {
      {"proofJsonPath", bundle.proofJsonPath},
      {"publicJsonPath", bundle.publicJsonPath},
      {"publicSignals", bundle.publicSignals},
      {"witnessMs", bundle.witnessMs},
      {"proveMs", bundle.proveMs},
      {"verifyMs", bundle.verifyMs},
      {"proofBytes", bundle.proofBytes},
      {"publicBytes", bundle.publicBytes},
  };
  save_json_pretty(workDir / "round_meta.json", j);
}

AuthRoundResult run_one_zk_auth_round(const Args& args,
                                      const std::string& roundId,
                                      const std::string& baseWorkDir) {
  const std::string projectRoot = default_project_root(args);
  const fs::path workDir = fs::path(baseWorkDir) / roundId;
  std::error_code ec;
  fs::create_directories(workDir, ec);

  AuthRoundResult out;
  out.workDir = workDir.string();
  out.row.mode = "zk_auth_e2e";
  out.row.id = roundId;
  out.row.depth = args.depth;
  out.row.bbEach = args.bbEach;
  out.row.recoverCase = "none";

  out.localKeys = gen_identity_local_keys();
  out.state = gen_identity_state(roundId, out.localKeys, projectRoot);

  const RegisterResponse reg = post_register_zk(args, out.state, out.localKeys);

  nlohmann::json leafInfo;
  const auto tPath0 = Clock::now();
  if (!wait_for_identity_ready_after_register(args, roundId, reg, out.state.ver, &out.root, &out.path, &leafInfo)) {
    throw std::runtime_error("accepted_but_not_observed_ready_after_register");
  }
  const auto tPath1 = Clock::now();
  out.row.pathFetchMs = ms_between(tPath0, tPath1);

  if (out.root.depth != args.depth) {
    std::cerr << "[warn] requested depth=" << args.depth << " but service depth=" << out.root.depth << "\n";
    out.row.depth = out.root.depth;
  }

  const auto localBundle = didzk::compute_identity_bundle_with_path(out.state, out.path, projectRoot);
  poseidon_debug_log("auth local bundle computed id=" + out.state.id + " leaf=" + localBundle.leafHex + " root=" + localBundle.rootHex);
  const std::string localLeaf = localBundle.leafHex;
  const std::string localRoot = localBundle.rootHex;
  const auto rootCheckFields = didzk::normalize_fields_native({leafInfo.at("leaf").get<std::string>(), out.root.root}, projectRoot);
  if (!didzk::field_equal(localBundle.leafField, rootCheckFields.at(0))) {
    throw std::runtime_error("leaf_mismatch_between_local_and_service");
  }
  if (!didzk::field_equal(localBundle.rootField, rootCheckFields.at(1))) {
    throw std::runtime_error("local_merkle_root_mismatch");
  }
  if (!didzk::verify_merkle_path_local(localLeaf, out.path, projectRoot)) {
    throw std::runtime_error("verify_merkle_path_local_failed");
  }

  out.sessionKeys = gen_ed25519_keypair();
  out.sess = build_session_context(roundId, out.root.epoch, out.sessionKeys, projectRoot);

  const ZkWitnessInput witnessIn = didzk::build_zk_witness_input(out.state, out.path, out.sess, projectRoot);
  const ZkBackendPaths paths = didzk::resolve_zk_backend_paths(projectRoot);
  const ZkRunArtifacts artifacts = didzk::make_default_run_artifacts(workDir.string());
  out.bundle = didzk::session_gen_zk(paths, witnessIn, artifacts, true);

  const std::string transcript = didzk::build_session_transcript(out.sess.ctxHash, out.bundle.publicSignals, out.sess.epoch);
  out.bundle.sessionSigHex = didzk::sign_session_transcript_hex(out.sessionKeys.skHex, transcript);

  std::string verifyErr;
  const bool ok = didzk::verify_bundle_with_session(paths,
                                                    out.bundle,
                                                    out.root.root,
                                                    out.sessionKeys.pkHex,
                                                    out.sess.ctxHash,
                                                    out.sess.epoch,
                                                    &verifyErr);
  if (!ok) {
    throw std::runtime_error("verify_bundle_with_session_failed: " + verifyErr);
  }

  out.row.witnessMs = out.bundle.witnessMs;
  out.row.proveMs = out.bundle.proveMs;
  out.row.verifyMs = out.bundle.verifyMs;
  out.row.proofBytes = out.bundle.proofBytes;
  out.row.publicBytes = out.bundle.publicBytes;
  out.row.ok = 1;

  save_round_metadata(workDir, out.state, out.localKeys, out.root, out.path, out.sess, out.sessionKeys, out.bundle);
  return out;
}

int run_zk_auth_e2e(const Args& args) {
  const std::string baseWorkDir = default_base_workdir(args, "zk_auth_e2e");
  if (!trim_copy(args.csvPath).empty()) {
    ensure_csv_header(args.csvPath);
  }

  int okCount = 0;
  for (int i = 0; i < args.runs; ++i) {
    const std::string roundId = run_id_for_index(args, i);
    try {
      AuthRoundResult res = run_one_zk_auth_round(args, roundId, baseWorkDir);
      append_csv_row(args.csvPath, res.row);
      ++okCount;
      std::cout << "[zk_auth_e2e] ok id=" << roundId
                << " witness_ms=" << res.row.witnessMs
                << " prove_ms=" << res.row.proveMs
                << " verify_ms=" << res.row.verifyMs
                << " proof_bytes=" << res.row.proofBytes
                << " public_bytes=" << res.row.publicBytes << "\n";
    } catch (const std::exception& e) {
      RunRow row;
      row.mode = "zk_auth_e2e";
      row.id = roundId;
      row.depth = args.depth;
      row.bbEach = args.bbEach;
      row.recoverCase = "none";
      row.ok = 0;
      append_csv_row(args.csvPath, row);
      std::cerr << "[zk_auth_e2e] fail id=" << roundId << " err=" << e.what() << "\n";
    }
  }

  return (okCount == args.runs) ? 0 : 1;
}

int run_zk_recovery_e2e(const Args& args) {
  if (args.recoverCase != "legal" && args.recoverCase != "pirate") {
    throw std::runtime_error("recover_case_must_be_legal_or_pirate");
  }
  if (args.recoverCase == "pirate") {
    throw std::runtime_error("pirate_recovery_not_yet_wired_in_this_skeleton; use --recover_case legal first");
  }

  const std::string projectRoot = default_project_root(args);
  const std::string baseWorkDir = default_base_workdir(args, "zk_recovery_e2e");
  ensure_csv_header(args.csvPath);

  const std::string roundId = args.id;
  AuthRoundResult oldRound = run_one_zk_auth_round(args, roundId, baseWorkDir);

  const fs::path recoveryDir = fs::path(baseWorkDir) / (roundId + "_recovery");
  std::error_code ec;
  fs::create_directories(recoveryDir, ec);

  save_json_pretty(recoveryDir / "old_root.json",
                   nlohmann::json{{"root", oldRound.root.root}, {"epoch", oldRound.root.epoch}, {"depth", oldRound.root.depth}});
  save_json_pretty(recoveryDir / "old_path.json",
                   nlohmann::json{{"root", oldRound.path.root}, {"epoch", oldRound.path.epoch},
                                  {"depth", oldRound.path.depth}, {"pathElements", oldRound.path.pathElements},
                                  {"pathIndex", oldRound.path.pathIndex}});

  IdentityLocalKeys newKeys = gen_identity_local_keys();
  IdentityStateZK newState = rotate_identity_state(oldRound.state, newKeys, projectRoot);
  post_apply_recovery_rotate_zk(args, newState, oldRound.localKeys, newKeys);

  std::unordered_map<std::string, std::string> recoveryKv;
  if (!wait_for_record_active(args, roundId, newState.ver, &recoveryKv)) {
    throw std::runtime_error("timed_out_waiting_for_recovery_record");
  }

  RootInfo newRoot;
  if (!wait_for_new_root(args, oldRound.root, newRoot)) {
    throw std::runtime_error("timed_out_waiting_for_new_root");
  }

  bool oldProofValid = false;
  {
    const ZkBackendPaths paths = didzk::resolve_zk_backend_paths(projectRoot);
    std::string err;
    oldProofValid = didzk::verify_bundle_with_session(paths,
                                                      oldRound.bundle,
                                                      newRoot.root,
                                                      oldRound.sessionKeys.pkHex,
                                                      oldRound.sess.ctxHash,
                                                      oldRound.sess.epoch,
                                                      &err);
    if (oldProofValid) {
      std::cerr << "[zk_recovery_e2e] warning: old proof still valid under new root\n";
    }
  }

  MerklePathZK newPath;
  nlohmann::json newLeafInfo;
  RootInfo pathReadyRoot;
  if (!wait_for_path_ready(args, roundId, newState.ver, &pathReadyRoot, &newPath, &newLeafInfo)) {
    throw std::runtime_error("timed_out_waiting_for_new_path_ready");
  }
  newRoot = pathReadyRoot;
  const SessionKeyPair newSessionKeys = gen_ed25519_keypair();
  const SessionContextZK newSess = build_session_context(roundId, newRoot.epoch, newSessionKeys, projectRoot);
  const ZkWitnessInput witnessIn = didzk::build_zk_witness_input(newState, newPath, newSess, projectRoot);
  const ZkBackendPaths paths = didzk::resolve_zk_backend_paths(projectRoot);
  const ZkRunArtifacts artifacts = didzk::make_default_run_artifacts((recoveryDir / "new_round").string());
  ZkProofBundle newBundle = didzk::session_gen_zk(paths, witnessIn, artifacts, true);
  const std::string newTranscript = didzk::build_session_transcript(newSess.ctxHash, newBundle.publicSignals, newSess.epoch);
  newBundle.sessionSigHex = didzk::sign_session_transcript_hex(newSessionKeys.skHex, newTranscript);
  std::string newErr;
  const bool newProofValid = didzk::verify_bundle_with_session(paths,
                                                               newBundle,
                                                               newRoot.root,
                                                               newSessionKeys.pkHex,
                                                               newSess.ctxHash,
                                                               newSess.epoch,
                                                               &newErr);
  if (!newProofValid) {
    throw std::runtime_error("new_proof_should_be_valid_but_failed: " + newErr);
  }

  RunRow row;
  row.mode = "zk_recovery_e2e";
  row.id = roundId;
  row.depth = newRoot.depth;
  row.bbEach = 1;
  row.pathFetchMs = oldRound.row.pathFetchMs;
  row.witnessMs = newBundle.witnessMs;
  row.proveMs = newBundle.proveMs;
  row.verifyMs = newBundle.verifyMs;
  row.proofBytes = newBundle.proofBytes;
  row.publicBytes = newBundle.publicBytes;
  row.recoverCase = args.recoverCase;
  row.oldProofValid = oldProofValid ? 1 : 0;
  row.newProofValid = newProofValid ? 1 : 0;
  row.ok = (!oldProofValid && newProofValid) ? 1 : 0;
  append_csv_row(args.csvPath, row);

  save_json_pretty(recoveryDir / "new_root.json",
                   nlohmann::json{{"root", newRoot.root}, {"epoch", newRoot.epoch}, {"depth", newRoot.depth}});
  save_json_pretty(recoveryDir / "result.json",
                   nlohmann::json{{"recover_case", args.recoverCase},
                                  {"old_proof_valid", oldProofValid},
                                  {"new_proof_valid", newProofValid},
                                  {"ok", row.ok},
                                  {"old_root", oldRound.root.root},
                                  {"new_root", newRoot.root},
                                  {"old_epoch", oldRound.root.epoch},
                                  {"new_epoch", newRoot.epoch},
                                  {"workdir", recoveryDir.string()}});
  std::cout << "[zk_recovery_e2e] ok=" << row.ok
            << " old_proof_valid=" << (oldProofValid ? 1 : 0)
            << " new_proof_valid=" << (newProofValid ? 1 : 0)
            << " new_root=" << newRoot.root
            << " workdir=" << recoveryDir.string() << "\n";
  return row.ok ? 0 : 1;
}
