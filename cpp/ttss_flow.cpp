#include "ttss_flow.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using Clock = AppClock;

std::vector<std::string> guardian_ids_for_n(int n) {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(n));
  for (int i = 1; i <= n; ++i) out.push_back("G" + std::to_string(i));
  return out;
}

std::string did_id_hash_for_ttss(const std::string& id) {
  return keccak256_hex_from_text(id);
}

nlohmann::json key_entries_to_json(const std::vector<didzk::TTSSKeyEntry>& entries) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& e : entries) {
    arr.push_back({{"guardianIndex", e.guardianIndex}, {"guardianId", e.guardianId}, {"uiHex", e.uiHex}});
  }
  return arr;
}

nlohmann::json identity_state_to_json(const IdentityStateZK& st) {
  return {{"id", st.id},
          {"sid", st.sid},
          {"rho", st.rho},
          {"cid", st.cid},
          {"pkNormHash", st.pkNormHash},
          {"pkRecHash", st.pkRecHash},
          {"ver", st.ver},
          {"active", st.active}};
}

IdentityStateZK identity_state_from_json(const nlohmann::json& j) {
  IdentityStateZK st;
  st.id = j.value("id", std::string());
  st.sid = j.value("sid", std::string());
  st.rho = j.value("rho", std::string());
  st.cid = j.value("cid", std::string());
  st.pkNormHash = j.value("pkNormHash", std::string());
  st.pkRecHash = j.value("pkRecHash", std::string());
  st.ver = static_cast<std::uint64_t>(j.value("ver", 0ull));
  st.active = j.value("active", true);
  return st;
}

nlohmann::json session_keypair_to_json(const SessionKeyPair& kp) {
  return {{"pkHex", kp.pkHex}, {"skHex", kp.skHex}};
}

SessionKeyPair session_keypair_from_json(const nlohmann::json& j) {
  SessionKeyPair kp;
  kp.pkHex = j.value("pkHex", std::string());
  kp.skHex = j.value("skHex", std::string());
  return kp;
}

nlohmann::json identity_local_keys_to_json(const IdentityLocalKeys& k) {
  return {{"normalEd25519", session_keypair_to_json(k.normalEd25519)},
          {"recoveryEd25519", session_keypair_to_json(k.recoveryEd25519)},
          {"boardSeeds", {{"ownerSeedHex", k.boardSeeds.ownerSeedHex}, {"recoverySeedHex", k.boardSeeds.recoverySeedHex}}}};
}

IdentityLocalKeys identity_local_keys_from_json(const nlohmann::json& j) {
  IdentityLocalKeys k;
  k.normalEd25519 = session_keypair_from_json(j.at("normalEd25519"));
  k.recoveryEd25519 = session_keypair_from_json(j.at("recoveryEd25519"));
  k.boardSeeds.ownerSeedHex = j.at("boardSeeds").value("ownerSeedHex", std::string());
  k.boardSeeds.recoverySeedHex = j.at("boardSeeds").value("recoverySeedHex", std::string());
  return k;
}

nlohmann::json root_info_to_json(const RootInfo& r) {
  return {{"root", r.root}, {"epoch", r.epoch}, {"depth", r.depth}};
}

RootInfo root_info_from_json(const nlohmann::json& j) {
  RootInfo r;
  r.root = j.value("root", std::string());
  r.epoch = static_cast<std::uint64_t>(j.value("epoch", 0ull));
  r.depth = j.value("depth", 0);
  return r;
}

LeafPathRefreshStats refresh_leaf_and_path_best_effort(const Args& args,
                                                       const std::string& id,
                                                       nlohmann::json* leafInfo,
                                                       MerklePathZK* path,
                                                       const char* logPrefix) {
  LeafPathRefreshStats stats;
  try {
    const auto tLeaf = Clock::now();
    nlohmann::json fetchedLeaf = fetch_leaf_json(args, id);
    stats.leafFetchMs = ms_between(tLeaf, Clock::now());
    if (leafInfo && fetchedLeaf.is_object() && fetchedLeaf.value("ok", 0) != 0) {
      *leafInfo = std::move(fetchedLeaf);
    }
  } catch (const std::exception& e) {
    poseidon_debug_log(std::string(logPrefix ? logPrefix : "ttss") + " fetch_leaf_json skipped: " + e.what());
  }

  try {
    const auto tPath = Clock::now();
    MerklePathZK fetchedPath = fetch_path(args, id);
    stats.pathFetchMs = ms_between(tPath, Clock::now());
    if (path && !trim_copy(fetchedPath.root).empty()) {
      *path = std::move(fetchedPath);
    }
  } catch (const std::exception& e) {
    poseidon_debug_log(std::string(logPrefix ? logPrefix : "ttss") + " fetch_path skipped: " + e.what());
  }
  return stats;
}

void distribute_share_envelopes_to_committees(const std::vector<std::string>& committeeUrls,
                                              const std::string& token,
                                              const std::vector<didzk::ShareEnvelope>& envelopes,
                                              int n,
                                              const std::string& failurePrefix) {
  std::vector<std::future<void>> tasks;
  tasks.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    tasks.emplace_back(std::async(std::launch::async, [&, i]() {
      std::string err;
      if (!committee_set_share_envelope(committeeUrls.at(static_cast<std::size_t>(i)), token,
                                        envelopes.at(static_cast<std::size_t>(i)), &err)) {
        throw std::runtime_error(failurePrefix + ": " + err);
      }
    }));
  }
  for (auto& f : tasks) f.get();
}

void verify_committee_share_meta_active(const std::vector<std::string>& committeeUrls,
                                        const std::string& idHash,
                                        std::uint64_t ver,
                                        std::uint64_t epoch,
                                        int n,
                                        const std::string& failureMessage) {
  std::vector<std::future<void>> tasks;
  tasks.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    tasks.emplace_back(std::async(std::launch::async, [&, i]() {
      const auto meta = committee_get_share_meta(committeeUrls.at(static_cast<std::size_t>(i)), idHash, ver,
                                                 epoch, static_cast<std::uint32_t>(i + 1));
      if (meta.value("ok", 0) != 1 || meta.value("active", false) != true) {
        throw std::runtime_error(failureMessage);
      }
    }));
  }
  for (auto& f : tasks) f.get();
}

void invalidate_committee_shares(const std::vector<std::string>& committeeUrls,
                                 const std::string& token,
                                 const std::string& idHash,
                                 std::uint64_t ver,
                                 std::uint64_t epoch,
                                 const std::string& reason) {
  std::vector<std::future<void>> tasks;
  tasks.reserve(committeeUrls.size());
  for (const auto& url : committeeUrls) {
    tasks.emplace_back(std::async(std::launch::async, [&, url]() {
      std::string err;
      if (!committee_invalidate(url, token, idHash, ver, epoch, reason, &err)) {
        throw std::runtime_error("committee_invalidate_failed: " + err);
      }
    }));
  }
  for (auto& f : tasks) f.get();
}

std::string compute_ttss_meta_hash(const std::string& id,
                                   const std::string& idHash,
                                   std::uint64_t ver,
                                   std::uint64_t epoch,
                                   int n,
                                   int t,
                                   const std::string& guardianSetHash,
                                   const std::string& vkSetHash,
                                   const std::string& dealerPkHex,
                                   const std::string& rhoCommitHex) {
  std::ostringstream oss;
  oss << "TTSS-META"
      << "|id=" << id
      << "|idHash=" << didzk::normalize_digest_hex32(idHash)
      << "|ver=" << ver
      << "|epoch=" << epoch
      << "|n=" << n
      << "|t=" << t
      << "|guardianSetHash=" << didzk::normalize_digest_hex32(guardianSetHash)
      << "|vkSetHash=" << didzk::normalize_digest_hex32(vkSetHash)
      << "|dealerPkHex=" << didzk::normalize_digest_hex32(dealerPkHex)
      << "|rhoCommitHex=" << didzk::normalize_digest_hex32(rhoCommitHex);
  return hash32_hex_from_text(oss.str());
}

nlohmann::json post_ttss_meta_endpoint(const Args& args,
                                       const std::string& endpoint,
                                       const nlohmann::json& body,
                                       bool tolerateTransportTimeout) {
  std::string resp;
  const bool ok = http_post_json(args.bbUrl, endpoint, body, resp, std::max(args.timeoutMs, 15000));
  if (!ok) {
    if (tolerateTransportTimeout && resp.find("transport_error_or_timeout") != std::string::npos) {
      return nlohmann::json{{"ok", 1}, {"accepted", 1}, {"status", "ACCEPTED"}, {"transportTimeout", 1}};
    }
    throw std::runtime_error("http_post_" + endpoint + "_failed: " + resp);
  }
  nlohmann::json out;
  try {
    out = nlohmann::json::parse(resp);
  } catch (const std::exception& e) {
    throw std::runtime_error("parse_" + endpoint + "_response_failed: " + std::string(e.what()) + ";raw=" + resp);
  }
  if (out.value("ok", 0) != 1) {
    throw std::runtime_error(endpoint + "_fail: " + out.dump());
  }
  return out;
}

nlohmann::json fetch_ttss_meta_json(const Args& args,
                                    const std::string& idHash) {
  std::string body;
  if (!http_get_text(args.bbUrl, "/ttssMeta?idHash=" + url_encode(idHash), body, args.timeoutMs)) {
    throw std::runtime_error("http_get_/ttssMeta_failed: " + body);
  }
  return nlohmann::json::parse(body);
}

bool wait_for_ttss_meta_ready(const Args& args,
                              const std::string& idHash,
                              std::uint64_t ver,
                              std::uint64_t epoch,
                              const std::string& vkSetHash,
                              const std::string& metaHash,
                              nlohmann::json* outMeta,
                              std::string* outDiag) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(args.rootWaitTimeoutMs);
  std::string lastDiag;
  while (Clock::now() < deadline) {
    try {
      nlohmann::json meta = fetch_ttss_meta_json(args, idHash);
      const bool ok = meta.value("ok", 0) == 1;
      const std::uint64_t gotVer = static_cast<std::uint64_t>(meta.value("ver", 0ull));
      const std::uint64_t gotEpoch = static_cast<std::uint64_t>(meta.value("epoch", 0ull));
      const std::string gotVkSetHash = meta.value("vkSetHash", std::string());
      const std::string gotMetaHash = meta.value("metaHash", std::string());
      const bool verOk = gotVer == ver;
      const bool epochOk = gotEpoch == epoch;
      const bool vkOk = didzk::normalize_digest_hex32(gotVkSetHash) == didzk::normalize_digest_hex32(vkSetHash);
      const bool metaHashOk = didzk::normalize_digest_hex32(gotMetaHash) == didzk::normalize_digest_hex32(metaHash);
      if (ok && verOk && epochOk && vkOk && metaHashOk) {
        if (outMeta) *outMeta = std::move(meta);
        if (outDiag) *outDiag = "ready";
        return true;
      }
      std::ostringstream oss;
      oss << "ok=" << (ok ? 1 : 0)
          << ",ver=" << gotVer
          << ",epoch=" << gotEpoch
          << ",vkSetHash=" << short_hex(gotVkSetHash)
          << ",metaHash=" << short_hex(gotMetaHash);
      lastDiag = oss.str();
    } catch (const std::exception& e) {
      lastDiag = e.what();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(args.rootPollMs));
  }
  if (outDiag) *outDiag = lastDiag;
  return false;
}


nlohmann::json load_ttss_setup_json(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) throw std::runtime_error("open_ttss_state_failed: " + path);
  nlohmann::json j;
  ifs >> j;
  return j;
}


int run_ttss_recover(const Args& args) {
  const nlohmann::json setupJson = load_ttss_setup_json(args.ttssStatePath);
  const fs::path baseWorkDir(default_base_workdir(args, "ttss_phase3"));
  const fs::path workDir = baseWorkDir / (args.id + "_ttss_recover");
  std::error_code ec;
  fs::create_directories(workDir, ec);

  const auto recoverStart = Clock::now();
  std::vector<didzk::ShareEnvelope> shares = recover_shares_from_committees(setupJson, args);
  std::string verifyErr;
  didzk::TTSSRecoverResult rec = didzk::ttss_rec_nits_shamir(shares, &verifyErr);
  const std::int64_t recoverMs = elapsed_ms_i64(recoverStart);
  if (!rec.ok) {
    throw std::runtime_error("ttss_rec_failed: " + rec.err + ";verify_err=" + verifyErr);
  }
  const std::string expected = setupJson.at("localKeys").at("boardSeeds").value("recoverySeedHex", std::string());
  const bool recoveredMatchesExpected =
      didzk::normalize_digest_hex32(rec.srecSeedHex) == didzk::normalize_digest_hex32(expected);

  nlohmann::json out = {{"ok", recoveredMatchesExpected ? 1 : 0},
                        {"id", setupJson.value("id", std::string())},
                        {"idHash", setupJson.value("idHash", std::string())},
                        {"ver", setupJson.value("ver", 0)},
                        {"epoch", setupJson.value("epoch", 0)},
                        {"shareCountUsed", rec.shareCountUsed},
                        {"recoveredSeedHex", rec.srecSeedHex},
                        {"expectedSeedHex", expected},
                        {"recoveredMatchesExpected", recoveredMatchesExpected},
                        {"timings", {{"recover_ms", recoverMs}}}};
  save_json_pretty(workDir / "recover_result.json", out);
  std::cout << "[ttss_recover] ok=" << (recoveredMatchesExpected ? 1 : 0)
            << " recoveredMatchesExpected=" << (recoveredMatchesExpected ? 1 : 0)
            << " result_json=" << (workDir / "recover_result.json").string() << "\n";
  return recoveredMatchesExpected ? 0 : 1;
}

