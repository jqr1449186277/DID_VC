#include "bb_client.hpp"

#include "normalize_utils.hpp"
#include "wait_utils.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

using Clock = AppClock;

bool try_canonicalize_field(const Args& args,
                            const std::string& raw,
                            const std::string& where,
                            const std::string& field,
                            std::string* out,
                            std::string* err) {
  return didzk::try_normalize_field_hex(raw, default_project_root(args), where + ":" + field, out, err);
}

std::string canonicalize_field_or_throw(const Args& args,
                                        const std::string& raw,
                                        const std::string& where,
                                        const std::string& field) {
  std::string out;
  std::string err;
  if (!try_canonicalize_field(args, raw, where, field, &out, &err)) {
    throw std::runtime_error(err);
  }
  return out;
}

bool is_valid_field_hex(const Args& args,
                        const std::string& raw,
                        const std::string& where,
                        const std::string& field,
                        std::string* err) {
  std::string tmp;
  return try_canonicalize_field(args, raw, where, field, &tmp, err);
}

RootInfo fetch_root(const Args& args) {
  std::string body;
  if (!http_get_text(args.bbUrl, "/root", body, args.timeoutMs)) {
    throw std::runtime_error("http_get_/root_failed: " + body);
  }
  const auto kv = parse_semicolon_kv(body);
  if (kv.find("ok") == kv.end() || kv.at("ok") != "1") {
    throw std::runtime_error("root_fail: " + body);
  }
  RootInfo r;
  r.root = canonicalize_field_or_throw(args, kv.at("root"), "fetch_root", "root");
  r.epoch = static_cast<std::uint64_t>(std::stoull(kv.at("epoch")));
  r.depth = std::stoi(kv.at("depth"));
  return r;
}

MerklePathZK fetch_path(const Args& args, const std::string& id) {
  std::string body;
  if (!http_get_text(args.bbUrl, "/path?id=" + url_encode(id), body, args.timeoutMs)) {
    throw std::runtime_error("http_get_/path_failed: " + body);
  }
  nlohmann::json j = nlohmann::json::parse(body);
  if (!j.value("ok", 0)) {
    throw std::runtime_error("path_fail: " + body);
  }
  MerklePathZK path;
  path.root = canonicalize_field_or_throw(args, j.at("root").get<std::string>(), "fetch_path", "root");
  path.epoch = static_cast<std::uint64_t>(j.at("epoch").get<std::uint64_t>());
  path.depth = static_cast<std::uint32_t>(j.at("depth").get<int>());
  path.pathElements = j.at("pathElements").get<std::vector<std::string>>();
  for (std::size_t i = 0; i < path.pathElements.size(); ++i) {
    path.pathElements[i] = canonicalize_field_or_throw(args, path.pathElements[i], "fetch_path", "pathElements[" + std::to_string(i) + "]");
  }
  const auto bits = j.at("pathIndex");
  path.pathIndex.reserve(bits.size());
  for (const auto& x : bits) {
    if (x.is_number_integer()) path.pathIndex.push_back(static_cast<std::uint8_t>(x.get<int>()));
    else if (x.is_string()) path.pathIndex.push_back(static_cast<std::uint8_t>(std::stoi(x.get<std::string>())));
    else throw std::runtime_error("bad_pathIndex_type");
  }
  return path;
}

nlohmann::json fetch_leaf_json(const Args& args, const std::string& id) {
  std::string body;
  if (!http_get_text(args.bbUrl, "/leaf?id=" + url_encode(id), body, args.timeoutMs)) {
    throw std::runtime_error("http_get_/leaf_failed: " + body);
  }
  nlohmann::json j = nlohmann::json::parse(body);
  if (!j.value("ok", 0)) {
    throw std::runtime_error("leaf_fail: " + body);
  }
  if (j.contains("leaf") && j.at("leaf").is_string()) j["leaf"] = canonicalize_field_or_throw(args, j.at("leaf").get<std::string>(), "fetch_leaf_json", "leaf");
  if (j.contains("cid") && j.at("cid").is_string()) j["cid"] = canonicalize_field_or_throw(args, j.at("cid").get<std::string>(), "fetch_leaf_json", "cid");
  if (j.contains("pkNormHash") && j.at("pkNormHash").is_string()) j["pkNormHash"] = canonicalize_field_or_throw(args, j.at("pkNormHash").get<std::string>(), "fetch_leaf_json", "pkNormHash");
  if (j.contains("pkRecHash") && j.at("pkRecHash").is_string()) j["pkRecHash"] = canonicalize_field_or_throw(args, j.at("pkRecHash").get<std::string>(), "fetch_leaf_json", "pkRecHash");
  if (j.contains("root") && j.at("root").is_string()) j["root"] = canonicalize_field_or_throw(args, j.at("root").get<std::string>(), "fetch_leaf_json", "root");
  return j;
}

bool parse_ready_snapshot_json(const std::string& body, ReadySnapshotInfo* out, std::string* err) {
  try {
    const nlohmann::json j = nlohmann::json::parse(body);
    out->ok = j.value("ok", 0) != 0;
    out->ready = j.value("ready", 0) != 0;
    out->cacheStale = j.value("cacheStale", 0) != 0 || j.value("cache_stale", 0) != 0;
    out->leafOk = j.value("leafOk", 0) != 0 || (j.contains("leaf") && j.at("leaf").is_string() && !trim_copy(j.at("leaf").get<std::string>()).empty());
    out->active = j.value("active", 0) != 0;
    out->rootMatches = j.value("rootMatches", 0) != 0;
    out->recordObserved = j.value("recordObserved", 0) != 0;
    out->recordActive = j.value("recordActive", 0) != 0;
    out->id = j.value("id", std::string());
    out->idHash = j.value("idHash", std::string());
    out->root = j.value("root", std::string());
    out->pathRoot = j.value("pathRoot", std::string());
    out->leaf = j.value("leaf", std::string());
    out->cid = j.value("cid", std::string());
    out->pkNormHash = j.value("pkNormHash", std::string());
    out->pkRecHash = j.value("pkRecHash", std::string());
    out->diag = j.value("diag", std::string());
    if (j.contains("minVersion") && !j.at("minVersion").is_null()) out->minVersion = static_cast<std::uint64_t>(j.at("minVersion").get<std::uint64_t>());
    if (j.contains("version") && !j.at("version").is_null()) out->version = static_cast<std::uint64_t>(j.at("version").get<std::uint64_t>());
    if (j.contains("epoch") && !j.at("epoch").is_null()) out->epoch = static_cast<std::uint64_t>(j.at("epoch").get<std::uint64_t>());
    if (j.contains("recordVersion") && !j.at("recordVersion").is_null()) out->recordVersion = static_cast<std::uint64_t>(j.at("recordVersion").get<std::uint64_t>());
    if (j.contains("depth") && !j.at("depth").is_null()) out->depth = j.at("depth").get<int>();
    if (j.contains("pathElements") && j.at("pathElements").is_array()) out->pathElements = j.at("pathElements").get<std::vector<std::string>>();
    out->pathIndex.clear();
    if (j.contains("pathIndex") && j.at("pathIndex").is_array()) {
      for (const auto& x : j.at("pathIndex")) {
        if (x.is_number_integer()) out->pathIndex.push_back(static_cast<std::uint8_t>(x.get<int>()));
        else if (x.is_string()) out->pathIndex.push_back(static_cast<std::uint8_t>(std::stoi(x.get<std::string>())));
      }
    }
    return true;
  } catch (const std::exception& e) {
    if (err) *err = std::string("parse_ready_snapshot_json_failed: ") + e.what();
    return false;
  }
}

ReadySnapshotInfo fetch_ready_snapshot(const Args& args, const std::string& id, std::uint64_t minVersion) {
  std::string body;
  const std::string path = "/readySnapshot?id=" + url_encode(id) + "&minVersion=" + std::to_string(minVersion);
  if (!http_get_text(args.bbUrl, path, body, args.timeoutMs)) {
    throw std::runtime_error("http_get_/readySnapshot_failed: " + body);
  }
  ReadySnapshotInfo snap;
  std::string err;
  if (!parse_ready_snapshot_json(body, &snap, &err)) {
    throw std::runtime_error(err + ": " + body);
  }
  if (!snap.ok) {
    throw std::runtime_error("ready_snapshot_fail: " + body);
  }
  return snap;
}

bool parse_register_status_json(const std::string& body,
                                RegisterStatusInfo* out,
                                std::string* err) {
  try {
    const nlohmann::json j = nlohmann::json::parse(body);
    out->ok = j.value("ok", 0) != 0;
    out->accepted = j.value("accepted", 0) != 0;
    out->ready = j.value("ready", 0) != 0;
    out->cacheStale = j.value("cache_stale", 0) != 0;
    out->opId = j.value("opId", std::string());
    out->requestKey = j.value("requestKey", std::string());
    out->kind = j.value("kind", std::string());
    out->id = j.value("id", std::string());
    out->status = j.value("status", std::string());
    out->txHash = j.value("txHash", std::string());
    out->root = j.value("root", std::string());
    out->lastError = j.value("lastError", std::string());
    if (j.contains("epoch") && !j.at("epoch").is_null()) {
      out->epoch = static_cast<std::uint64_t>(j.at("epoch").get<std::uint64_t>());
    }
    if (j.contains("version") && !j.at("version").is_null()) {
      out->version = static_cast<std::uint64_t>(j.at("version").get<std::uint64_t>());
    }
    return true;
  } catch (const std::exception& e) {
    if (err) *err = std::string("parse_register_status_json_failed: ") + e.what();
    return false;
  }
}

bool fetch_register_status(const Args& args,
                           const std::string& opId,
                           const std::string& requestKey,
                           RegisterStatusInfo* out,
                           std::string* err) {
  std::string path = "/registerStatus";
  if (!trim_copy(opId).empty()) {
    path += "?opId=" + url_encode(opId);
  } else if (!trim_copy(requestKey).empty()) {
    path += "?requestKey=" + url_encode(requestKey);
  } else {
    if (err) *err = "missing_register_status_locator";
    return false;
  }

  std::string body;
  if (!http_get_text(args.bbUrl, path, body, args.timeoutMs)) {
    if (err) *err = "http_get_/registerStatus_failed: " + body;
    return false;
  }
  if (!parse_register_status_json(body, out, err)) {
    return false;
  }
  if (!out->ok) {
    if (err) *err = "registerStatus_fail: " + body;
    return false;
  }
  return true;
}

bool status_reports_ready(const RegisterStatusInfo& st) {
  return st.ready || st.status == "READY";
}

bool status_reports_failed(const RegisterStatusInfo& st) {
  return st.status == "FAILED";
}

std::string status_error_text(const RegisterStatusInfo& st) {
  if (!st.lastError.empty()) return st.lastError;
  return st.status;
}

bool status_reports_cache_stale(const RegisterStatusInfo& st) {
  return st.cacheStale;
}

std::string short_hex(const std::string& s) {
  const std::string t = trim_copy(s);
  if (t.size() <= 18) return t;
  return t.substr(0, 10) + "..." + t.substr(t.size() - 6);
}


std::string redact_hex_secret(const std::string& s) {
  const std::string t = trim_copy(s);
  if (t.empty()) return t;
  std::string u = t;
  if (u.rfind("0x", 0) == 0 || u.rfind("0X", 0) == 0) u = u.substr(2);
  if (u.size() <= 16) return std::string("0x") + u;
  return std::string("0x") + u.substr(0, 8) + "..." + u.substr(u.size() - 8);
}

nlohmann::json redact_recovery_rotate_body_for_log(const nlohmann::json& body) {
  nlohmann::json out = body;
  for (const char* k : {"recoverySeedHex", "newOwnerSeedHex", "newRecoverySeedHex"}) {
    if (out.contains(k) && out.at(k).is_string()) {
      out[k] = redact_hex_secret(out.at(k).get<std::string>());
    }
  }
  return out;
}

bool materialize_ready_snapshot_outputs(const Args& args,
                                     const std::string& id,
                                     const ReadySnapshotInfo& snap,
                                     RootInfo* outRoot,
                                     MerklePathZK* outPath,
                                     nlohmann::json* outLeaf,
                                     std::string* outErr = nullptr) {
  RootInfo fetchedRoot;
  MerklePathZK fetchedPath;
  nlohmann::json fetchedLeaf;
  std::string err;
  try {
    fetchedRoot = fetch_root(args);
  } catch (const std::exception& e) {
    err = std::string("final_fetch_root_failed: ") + e.what();
    poseidon_debug_log("materialize_ready_snapshot_outputs id=" + id + " " + err +
                       " snap.root=" + short_hex(snap.root) +
                       " snap.pathRoot=" + short_hex(snap.pathRoot) +
                       " snap.leaf=" + short_hex(snap.leaf));
    if (outErr) *outErr = err;
    return false;
  }
  try {
    fetchedPath = fetch_path(args, id);
  } catch (const std::exception& e) {
    err = std::string("final_fetch_path_failed: ") + e.what();
    poseidon_debug_log("materialize_ready_snapshot_outputs id=" + id + " " + err +
                       " fetchedRoot=" + short_hex(fetchedRoot.root));
    if (outErr) *outErr = err;
    return false;
  }
  try {
    fetchedLeaf = fetch_leaf_json(args, id);
  } catch (const std::exception& e) {
    err = std::string("final_fetch_leaf_failed: ") + e.what();
    poseidon_debug_log("materialize_ready_snapshot_outputs id=" + id + " " + err +
                       " fetchedRoot=" + short_hex(fetchedRoot.root) +
                       " fetchedPath.root=" + short_hex(fetchedPath.root));
    if (outErr) *outErr = err;
    return false;
  }
  if (!fetchedLeaf.is_object() || fetchedLeaf.value("ok", 0) == 0) {
    err = "final_fetch_leaf_invalid_payload";
    poseidon_debug_log("materialize_ready_snapshot_outputs id=" + id + " " + err +
                       " leafDump=" + fetchedLeaf.dump());
    if (outErr) *outErr = err;
    return false;
  }
  std::string leafHex = fetchedLeaf.value("leaf", std::string());
  if (trim_copy(leafHex).empty()) {
    err = "final_fetch_leaf_missing_leaf";
    poseidon_debug_log("materialize_ready_snapshot_outputs id=" + id + " " + err +
                       " leafDump=" + fetchedLeaf.dump());
    if (outErr) *outErr = err;
    return false;
  }
  if (outRoot) *outRoot = std::move(fetchedRoot);
  if (outPath) *outPath = std::move(fetchedPath);
  if (outLeaf) *outLeaf = std::move(fetchedLeaf);
  if (outErr) outErr->clear();
  poseidon_debug_log("materialize_ready_snapshot_outputs ok id=" + id +
                     " root=" + short_hex(outRoot ? outRoot->root : std::string()) +
                     " path.root=" + short_hex(outPath ? outPath->root : std::string()) +
                     " leaf=" + short_hex(outLeaf ? outLeaf->value("leaf", std::string()) : std::string()));
  return true;
}

bool choose_root_after_ready(const Args& args,
                             const RegisterStatusInfo& st,
                             const MerklePathZK& path,
                             RootInfo* outRoot,
                             std::string* outErr = nullptr) {
  if (!trim_copy(st.root).empty()) {
    outRoot->root = st.root;
    outRoot->epoch = st.epoch;
    outRoot->depth = args.depth;
    return true;
  }
  if (!trim_copy(path.root).empty()) {
    outRoot->root = path.root;
    outRoot->epoch = path.epoch;
    outRoot->depth = static_cast<int>(path.depth);
    return true;
  }
  try {
    *outRoot = fetch_root(args);
    return true;
  } catch (const std::exception& e) {
    if (outErr) *outErr = e.what();
    return false;
  }
}


RegisterStatusInfo register_status_from_response(const RegisterResponse& reg) {
  RegisterStatusInfo st;
  st.ok = reg.ok;
  st.accepted = reg.accepted;
  st.ready = reg.ready;
  st.opId = reg.opId;
  st.requestKey = reg.requestKey;
  st.status = reg.status;
  st.txHash = reg.txHash;
  st.root = reg.root;
  st.epoch = reg.epoch;
  st.version = reg.version;
  return st;
}

bool response_reports_ready(const RegisterResponse& reg) {
  return reg.ready || reg.status == "READY";
}

bool probe_path_ready(const Args& args,
                      const std::string& id,
                      std::uint64_t minVersion,
                      RootInfo* outRoot,
                      MerklePathZK* outPath,
                      nlohmann::json* outLeaf,
                      std::string* outErr) {
  try {
    ReadySnapshotInfo snap = fetch_ready_snapshot(args, id, minVersion);
    const bool ready = snap.ready && snap.rootMatches && snap.leafOk && snap.active && snap.version >= minVersion && !snap.pathElements.empty() && !snap.pathIndex.empty();
    if (ready) {
      const bool materialized = materialize_ready_snapshot_outputs(args, id, snap, outRoot, outPath, outLeaf, outErr);
      if (!materialized && outErr && outErr->empty()) *outErr = "materialize_ready_snapshot_outputs_failed";
      return materialized;
    }
    if (outErr) *outErr = snap.diag.empty() ? "snapshot_not_ready" : snap.diag;
  } catch (const std::exception& e) {
    if (outErr) *outErr = e.what();
  }
  return false;
}

bool wait_for_record_active(const Args& args,
                            const std::string& id,
                            std::uint64_t minVersion,
                            std::unordered_map<std::string, std::string>* outKv) {
  std::string lastDiag;
  const bool ready = wait_until(
      std::chrono::milliseconds(args.registerWaitTimeoutMs),
      std::chrono::milliseconds(args.rootPollMs),
      [&](int attempt) {
    try {
      ReadySnapshotInfo snap = fetch_ready_snapshot(args, id, minVersion);
      const bool recordReady = snap.recordObserved && snap.recordActive && snap.recordVersion >= minVersion;
      const bool pathReady = snap.ready && snap.rootMatches && snap.leafOk && snap.active && snap.version >= minVersion && !snap.pathElements.empty() && !snap.pathIndex.empty();
      if (recordReady || pathReady) {
        if (outKv) {
          std::unordered_map<std::string, std::string> kv;
          kv["ok"] = "1";
          kv["active"] = (snap.recordActive || snap.active) ? "1" : "0";
          kv["version"] = std::to_string(std::max(snap.recordVersion, snap.version));
          kv["cid"] = snap.cid;
          kv["pkNormHash"] = snap.pkNormHash;
          kv["pkRecHash"] = snap.pkRecHash;
          *outKv = std::move(kv);
        }
        return true;
      }
      lastDiag = snap.diag;
    } catch (const std::exception& e) {
      lastDiag = e.what();
    }
    if (attempt == 1 || attempt % 10 == 0) {
      std::cerr << "[wait_for_record_active] pending id=" << id
                << " minVersion=" << minVersion
                << " attempt=" << attempt
                << " diag=" << lastDiag << "\n";
    }
    return false;
  });
  if (ready) return true;
  std::cerr << "[wait_for_record_active] timeout id=" << id
            << " minVersion=" << minVersion
            << " lastDiag=" << lastDiag << "\n";
  return false;
}

bool wait_for_path_ready(const Args& args,
                         const std::string& id,
                         std::uint64_t minVersion,
                         RootInfo* outRoot,
                         MerklePathZK* outPath,
                         nlohmann::json* outLeaf) {
  std::string lastErr;
  const bool ready = wait_until(
      std::chrono::milliseconds(args.pathWaitTimeoutMs),
      std::chrono::milliseconds(args.rootPollMs),
      [&](int attempt) {
    if (probe_path_ready(args, id, minVersion, outRoot, outPath, outLeaf, &lastErr)) {
      std::cerr << "[wait_for_path_ready] ready id=" << id
                << " minVersion=" << minVersion
                << " attempt=" << attempt
                << " root=" << short_hex(outRoot ? outRoot->root : std::string())
                << " epoch=" << (outRoot ? outRoot->epoch : 0)
                << "\n";
      return true;
    }
    if (attempt == 1 || attempt % 10 == 0) {
      std::cerr << "[wait_for_path_ready] pending id=" << id
                << " minVersion=" << minVersion
                << " attempt=" << attempt
                << " diag=" << lastErr << "\n";
    }
    return false;
  });
  if (ready) return true;
  std::cerr << "[wait_for_path_ready] timeout id=" << id
            << " minVersion=" << minVersion
            << " lastDiag=" << lastErr << "\n";
  try {
    RootInfo dbgRoot = fetch_root(args);
    std::cerr << "[wait_for_path_ready] final_root root=" << short_hex(dbgRoot.root)
              << " epoch=" << dbgRoot.epoch
              << " depth=" << dbgRoot.depth << "\n";
  } catch (const std::exception& e) {
    std::cerr << "[wait_for_path_ready] final_root_fetch_err=" << e.what() << "\n";
  }
  try {
    MerklePathZK dbgPath = fetch_path(args, id);
    std::cerr << "[wait_for_path_ready] final_path root=" << short_hex(dbgPath.root)
              << " epoch=" << dbgPath.epoch
              << " depth=" << dbgPath.depth
              << " elems=" << dbgPath.pathElements.size()
              << " index=" << dbgPath.pathIndex.size() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "[wait_for_path_ready] final_path_fetch_err=" << e.what() << "\n";
  }
  try {
    nlohmann::json dbgLeaf = fetch_leaf_json(args, id);
    std::cerr << "[wait_for_path_ready] final_leaf " << dbgLeaf.dump() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "[wait_for_path_ready] final_leaf_fetch_err=" << e.what() << "\n";
  }
  return false;
}

bool wait_for_identity_ready_after_register(const Args& args,
                                            const std::string& id,
                                            const RegisterResponse& reg,
                                            std::uint64_t minVersion,
                                            RootInfo* outRoot,
                                            MerklePathZK* outPath,
                                            nlohmann::json* outLeaf,
                                            ReadyWaitBreakdown* dbg) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(args.registerWaitTimeoutMs);
  RegisterStatusInfo lastStatus;
  std::string lastErr;

  if (response_reports_ready(reg) && !trim_copy(reg.root).empty()) {
    const auto fastDeadline = Clock::now() + std::chrono::milliseconds(std::min(args.rootWaitTimeoutMs, 2000));
    while (Clock::now() < fastDeadline) {
      MerklePathZK path;
      nlohmann::json leaf;
      std::string pathErr;
      const auto probeStart = Clock::now();
      RootInfo snapRoot;
        const bool pathOk = probe_path_ready(args, id, minVersion, &snapRoot, &path, &leaf, &pathErr);
      if (dbg) dbg->readyProbeMs += ms_between(probeStart, Clock::now());
      if (pathOk) {
        RegisterStatusInfo fastSt = register_status_from_response(reg);
        RootInfo root;
        std::string rootErr;
        const auto chooseStart = Clock::now();
        const bool chooseOk = choose_root_after_ready(args, fastSt, path, &root, &rootErr);
        if (dbg) dbg->chooseRootMs += ms_between(chooseStart, Clock::now());
        if (chooseOk) {
          if (outRoot) *outRoot = std::move(root);
          if (outPath) *outPath = std::move(path);
          if (outLeaf) *outLeaf = std::move(leaf);
          return true;
        }
        lastErr = rootErr;
      } else {
        lastErr = pathErr;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(args.rootPollMs));
      if (dbg) dbg->sleepMs += static_cast<double>(args.rootPollMs);
    }
  }

  while (Clock::now() < deadline) {
    RegisterStatusInfo st;
    std::string stErr;
    const auto statusStart = Clock::now();
    const bool statusOk = fetch_register_status(args, reg.opId, reg.requestKey, &st, &stErr);
    if (dbg) dbg->statusPollMs += ms_between(statusStart, Clock::now());
    if (statusOk) {
      lastStatus = st;

      if (status_reports_failed(st)) {
        throw std::runtime_error("register_status_failed: " + status_error_text(st));
      }

      if (status_reports_ready(st) && !status_reports_cache_stale(st)) {
        MerklePathZK path;
        nlohmann::json leaf;
        std::string pathErr;
        const auto probeStart = Clock::now();
        RootInfo snapRoot;
        const bool pathOk = probe_path_ready(args, id, minVersion, &snapRoot, &path, &leaf, &pathErr);
        if (dbg) dbg->readyProbeMs += ms_between(probeStart, Clock::now());
        if (pathOk) {
          RootInfo root;
          std::string rootErr;
          const auto chooseStart = Clock::now();
          root = snapRoot;
          const bool chooseOk = !trim_copy(root.root).empty();
          if (dbg) dbg->chooseRootMs += ms_between(chooseStart, Clock::now());
          if (chooseOk) {
            if (outRoot) *outRoot = std::move(root);
            if (outPath) *outPath = std::move(path);
            if (outLeaf) *outLeaf = std::move(leaf);
            return true;
          }
          lastErr = rootErr;
        } else {
          lastErr = pathErr;
        }
      } else {
        if (status_reports_cache_stale(st)) {
          lastErr = "cache_stale";
        } else if (!st.status.empty()) {
          lastErr = "status=" + st.status;
        }
      }
    } else {
      lastErr = stErr;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(args.rootPollMs));
    if (dbg) dbg->sleepMs += static_cast<double>(args.rootPollMs);
  }

  {
    RegisterStatusInfo st;
    std::string stErr;
    const auto statusStart = Clock::now();
    const bool statusOk = fetch_register_status(args, reg.opId, reg.requestKey, &st, &stErr);
    if (dbg) dbg->statusPollMs += ms_between(statusStart, Clock::now());
    if (statusOk) {
      lastStatus = st;
      if (status_reports_ready(st) && !status_reports_cache_stale(st)) {
        MerklePathZK path;
        nlohmann::json leaf;
        std::string pathErr;
        const auto probeStart = Clock::now();
        RootInfo snapRoot;
        const bool pathOk = probe_path_ready(args, id, minVersion, &snapRoot, &path, &leaf, &pathErr);
        if (dbg) dbg->readyProbeMs += ms_between(probeStart, Clock::now());
        if (pathOk) {
          RootInfo root;
          std::string rootErr;
          const auto chooseStart = Clock::now();
          root = snapRoot;
          const bool chooseOk = !trim_copy(root.root).empty();
          if (dbg) dbg->chooseRootMs += ms_between(chooseStart, Clock::now());
          if (chooseOk) {
            if (outRoot) *outRoot = std::move(root);
            if (outPath) *outPath = std::move(path);
            if (outLeaf) *outLeaf = std::move(leaf);
            return true;
          }
          lastErr = rootErr;
        } else if (!pathErr.empty()) {
          lastErr = pathErr;
        }
      } else if (status_reports_failed(st)) {
        throw std::runtime_error("register_status_failed: " + status_error_text(st));
      }
    } else if (!stErr.empty()) {
      lastErr = stErr;
    }
  }

  std::string msg = "accepted_but_not_observed_ready_after_register";
  if (!lastStatus.status.empty()) msg += ";last_status=" + lastStatus.status;
  if (!lastErr.empty()) msg += ";last_err=" + lastErr;
  throw std::runtime_error(msg);
}


RegisterResponse post_register_zk(const Args& args,
                                  const IdentityStateZK& st,
                                  const IdentityLocalKeys& keys,
                                  const nlohmann::json& extraBody) {
  RegisterResponse out;
  out.requestKey = "register:" + st.id;
  out.version = st.ver;

  const auto prepStart = Clock::now();
  const auto normStart = Clock::now();
  const std::string projectRoot = default_project_root(args);
  const auto normalizedFields = didzk::normalize_fields_native({st.cid, st.pkNormHash, st.pkRecHash}, projectRoot);
  const std::string cidHex = didzk::field_bytes_to_hex(normalizedFields.at(0));
  const std::string pkNormHashHex = didzk::field_bytes_to_hex(normalizedFields.at(1));
  const std::string pkRecHashHex = didzk::field_bytes_to_hex(normalizedFields.at(2));
  poseidon_debug_log("register prep normalized cid/pk hashes for id=" + st.id + " ver=" + std::to_string(st.ver));
  out.fieldNormalizeMs = ms_between(normStart, Clock::now());

  nlohmann::json body;
  body["id"] = st.id;
  body["cidHex"] = cidHex;
  body["pkNormHash"] = pkNormHashHex;
  body["pkRecHash"] = pkRecHashHex;
  body["ownerSeedHex"] = keys.boardSeeds.ownerSeedHex;
  body["recoverySeedHex"] = keys.boardSeeds.recoverySeedHex;
  body["wait"] = args.bbAsyncSubmit ? 0 : 1;
  body["confirmations"] = args.bbConfirmations;
  body["includeSnapshot"] = args.bbIncludeSnapshot ? 1 : 0;
  body["requestId"] = out.requestKey;
  if (extraBody.is_object()) {
    for (auto it = extraBody.begin(); it != extraBody.end(); ++it) {
      body[it.key()] = it.value();
    }
  }
  out.clientPrepMs = ms_between(prepStart, Clock::now());

  std::string resp;
  const int postTimeoutMs = std::max(args.timeoutMs, 15000);
  const bool postOk = http_post_json(args.bbUrl, "/registerZk", body, resp, postTimeoutMs);

  if (postOk) {
    const auto kv = parse_semicolon_kv(resp);
    if (kv.find("ok") == kv.end() || kv.at("ok") != "1") {
      throw std::runtime_error("registerZk_fail: " + resp);
    }
    out.ok = true;
    out.accepted = true;
    auto itOwner = kv.find("owner");
    if (itOwner != kv.end()) out.owner = itOwner->second;
    auto itRecovery = kv.find("recovery");
    if (itRecovery != kv.end()) out.recovery = itRecovery->second;
    auto itRequest = kv.find("requestKey");
    if (itRequest != kv.end() && !itRequest->second.empty()) out.requestKey = itRequest->second;
    auto itOpId = kv.find("opId");
    if (itOpId != kv.end()) out.opId = itOpId->second;
    auto itTxHash = kv.find("txHash");
    if (itTxHash != kv.end()) out.txHash = itTxHash->second;
    auto itStatus = kv.find("status");
    if (itStatus != kv.end()) out.status = itStatus->second;
    out.ready = kv.count("ready") ? (kv.at("ready") == "1") : false;
    out.root = kv.count("root") ? kv.at("root") : std::string();
    out.epoch = kv.count("epoch") ? static_cast<std::uint64_t>(std::stoull(kv.at("epoch"))) : 0ull;
    out.version = kv.count("version") ? static_cast<std::uint64_t>(std::stoull(kv.at("version"))) : st.ver;
    out.submitMs = kv.count("submit_ms") ? std::stod(kv.at("submit_ms")) : 0.0;
    out.confirmMs = kv.count("confirm_ms") ? std::stod(kv.at("confirm_ms")) : 0.0;
    out.ttssMerged = kv.count("ttssMerged") ? (kv.at("ttssMerged") == "1") : false;
    out.ttssDeferredScheduled = kv.count("ttssDeferredScheduled") ? (kv.at("ttssDeferredScheduled") == "1") : false;
    out.ttssVkSetHash = kv.count("ttssVkSetHash") ? kv.at("ttssVkSetHash") : std::string();
    out.ttssMetaHash = kv.count("ttssMetaHash") ? kv.at("ttssMetaHash") : std::string();
    out.ttssMetaTxHash = kv.count("ttssMetaTxHash") ? kv.at("ttssMetaTxHash") : std::string();
    out.ttssEpoch = kv.count("ttssEpoch") ? static_cast<std::uint64_t>(std::stoull(kv.at("ttssEpoch"))) : 0ull;
    return out;
  }

  if (resp.find("transport_error_or_timeout") == std::string::npos) {
    throw std::runtime_error("http_post_/registerZk_failed: " + resp);
  }

  // Transport-layer ambiguity is tolerated. The service is expected to expose
  // acceptance/ready state via /registerStatus and factual readiness endpoints.
  out.ok = true;
  out.accepted = true;
  out.status = "ACCEPTED";
  return out;
}

RecoveryRotateResponse post_apply_recovery_rotate_zk(const Args& args,
                                   const IdentityStateZK& newState,
                                   const IdentityLocalKeys& oldKeys,
                                   const IdentityLocalKeys& newKeys,
                                   const nlohmann::json& extraBody) {
  RecoveryRotateResponse out;
  const auto prepStart = Clock::now();
  const auto normStart = Clock::now();
  const std::string projectRoot = default_project_root(args);
  const auto normalizedFields = didzk::normalize_fields_native({newState.cid, newState.pkNormHash, newState.pkRecHash}, projectRoot);
  const std::string newCidHex = didzk::field_bytes_to_hex(normalizedFields.at(0));
  const std::string pkNormHashHex = didzk::field_bytes_to_hex(normalizedFields.at(1));
  const std::string pkRecHashHex = didzk::field_bytes_to_hex(normalizedFields.at(2));
  poseidon_debug_log("rotate prep normalized cid/pk hashes for id=" + newState.id + " ver=" + std::to_string(newState.ver));
  out.fieldNormalizeMs = ms_between(normStart, Clock::now());
  nlohmann::json body;
  body["id"] = newState.id;
  body["newCidHex"] = newCidHex;
  body["pkNormHash"] = pkNormHashHex;
  body["pkRecHash"] = pkRecHashHex;
  body["recoverySeedHex"] = oldKeys.boardSeeds.recoverySeedHex;
  body["newOwnerSeedHex"] = newKeys.boardSeeds.ownerSeedHex;
  body["newRecoverySeedHex"] = newKeys.boardSeeds.recoverySeedHex;
  const std::uint64_t oldVersion = (newState.ver > 0 ? (newState.ver - 1) : 0);
  body["oldVersion"] = oldVersion;
  body["currentVersion"] = oldVersion;
  body["newVersion"] = newState.ver;
  body["version"] = newState.ver;
  body["wait"] = args.bbAsyncSubmit ? 0 : 1;
  body["confirmations"] = args.bbConfirmations;
  body["includeSnapshot"] = args.bbIncludeSnapshot ? 1 : 0;
  body["requestId"] = "recovery:" + newState.id + ":" + std::to_string(newState.ver);
  if (extraBody.is_object()) {
    for (auto it = extraBody.begin(); it != extraBody.end(); ++it) body[it.key()] = it.value();
  }
  out.clientPrepMs = ms_between(prepStart, Clock::now());

  std::cerr << "[applyRecoveryRotateZk] request "
            << redact_recovery_rotate_body_for_log(body).dump() << "\n";
  std::cerr << "[applyRecoveryRotateZk] intent id=" << newState.id
            << " targetVer=" << newState.ver
            << " newCid=" << short_hex(newState.cid)
            << " newPkNormHash=" << short_hex(newState.pkNormHash)
            << " newPkRecHash=" << short_hex(newState.pkRecHash)
            << " oldRecoverySeed=" << redact_hex_secret(oldKeys.boardSeeds.recoverySeedHex)
            << " newOwnerSeed=" << redact_hex_secret(newKeys.boardSeeds.ownerSeedHex)
            << " newRecoverySeed=" << redact_hex_secret(newKeys.boardSeeds.recoverySeedHex)
            << " requestId=" << body.value("requestId", std::string()) << "\n";

  std::string resp;
  const bool postOk = http_post_json(args.bbUrl, "/applyRecoveryRotateZk", body, resp, std::max(args.timeoutMs, 15000));
  std::cerr << "[applyRecoveryRotateZk] response postOk=" << (postOk ? 1 : 0)
            << " raw=" << resp << "\n";

  if (!postOk && resp.find("transport_error_or_timeout") == std::string::npos) {
    throw std::runtime_error("http_post_/applyRecoveryRotateZk_failed: " + resp);
  }
  out.ok = true;
  out.accepted = true;
  if (postOk) {
    const auto parseStart = Clock::now();
    const auto kv = parse_semicolon_kv(resp);
    out.parseMs = elapsed_ms_i64(parseStart);
    std::cerr << "[applyRecoveryRotateZk] parsed"
              << " ok=" << (kv.count("ok") ? kv.at("ok") : std::string("<missing>"))
              << " status=" << (kv.count("status") ? kv.at("status") : std::string("<missing>"))
              << " version=" << (kv.count("version") ? kv.at("version") : std::string("<missing>"))
              << " ver=" << (kv.count("ver") ? kv.at("ver") : std::string("<missing>"))
              << " root=" << (kv.count("root") ? short_hex(kv.at("root")) : std::string("<missing>"))
              << " epoch=" << (kv.count("epoch") ? kv.at("epoch") : std::string("<missing>"))
              << " txHash=" << (kv.count("txHash") ? short_hex(kv.at("txHash")) : std::string("<missing>"))
              << "\n";
    if (kv.find("ok") == kv.end() || kv.at("ok") != "1") {
      throw std::runtime_error("applyRecoveryRotateZk_fail: " + resp);
    }
    out.ready = kv.count("ready") ? (kv.at("ready") == "1") : false;
    out.status = kv.count("status") ? kv.at("status") : std::string();
    out.root = kv.count("root") ? kv.at("root") : std::string();
    out.accepted = kv.count("accepted") ? (kv.at("accepted") == "1") : true;
    out.epoch = kv.count("epoch") ? static_cast<std::uint64_t>(std::stoull(kv.at("epoch"))) : 0ull;
    out.version = kv.count("version") ? static_cast<std::uint64_t>(std::stoull(kv.at("version"))) : newState.ver;
    out.txHash = kv.count("txHash") ? kv.at("txHash") : std::string();
    out.ttssMerged = kv.count("ttssMerged") ? (kv.at("ttssMerged") == "1" || kv.at("ttssMerged") == "true") : false;
    out.snapshotReady = kv.count("snapshotReady") ? (kv.at("snapshotReady") == "1" || kv.at("snapshotReady") == "true") : false;
    out.snapshotRootMatches = kv.count("snapshotRootMatches") ? (kv.at("snapshotRootMatches") == "1" || kv.at("snapshotRootMatches") == "true") : false;
    out.snapshotRoot = kv.count("snapshotRoot") ? kv.at("snapshotRoot") : std::string();
    out.snapshotLeaf = kv.count("snapshotLeaf") ? kv.at("snapshotLeaf") : std::string();
    out.snapshotPathRoot = kv.count("snapshotPathRoot") ? kv.at("snapshotPathRoot") : std::string();
    out.snapshotDiag = kv.count("snapshotDiag") ? kv.at("snapshotDiag") : std::string();
    out.ttssVkSetHash = kv.count("ttssVkSetHash") ? kv.at("ttssVkSetHash") : std::string();
    out.ttssMetaHash = kv.count("ttssMetaHash") ? kv.at("ttssMetaHash") : std::string();
    out.ttssMetaTxHash = kv.count("ttssMetaTxHash") ? kv.at("ttssMetaTxHash") : std::string();
    out.ttssEpoch = kv.count("ttssEpoch") ? static_cast<std::uint64_t>(std::stoull(kv.at("ttssEpoch"))) : 0ull;
    out.snapshotEpoch = kv.count("snapshotEpoch") ? static_cast<std::uint64_t>(std::stoull(kv.at("snapshotEpoch"))) : 0ull;
    out.snapshotVersion = kv.count("snapshotVersion") ? static_cast<std::uint64_t>(std::stoull(kv.at("snapshotVersion"))) : 0ull;
    out.submitMs = kv.count("submit_ms") ? std::stod(kv.at("submit_ms")) : 0.0;
    out.confirmMs = kv.count("confirm_ms") ? std::stod(kv.at("confirm_ms")) : 0.0;
    std::cerr << "[applyRecoveryRotateZk] merged parse"
              << " ttssMerged=" << (out.ttssMerged ? 1 : 0)
              << " ttssEpoch=" << out.ttssEpoch
              << " ttssVkSetHash=" << short_hex(out.ttssVkSetHash)
              << " ttssMetaHash=" << short_hex(out.ttssMetaHash)
              << " ttssMetaTxHash=" << short_hex(out.ttssMetaTxHash)
              << " parseMs=" << out.parseMs
              << "\n";
  }
  return out;
}

bool wait_for_new_root(const Args& args,
                       const RootInfo& oldRoot,
                       RootInfo& outRoot) {
  const std::string projectRoot = default_project_root(args);
  const std::string oldNormRoot = didzk::normalize_field_hex(oldRoot.root, projectRoot);
  return wait_until(
      std::chrono::milliseconds(args.rootWaitTimeoutMs),
      std::chrono::milliseconds(args.rootPollMs),
      [&](int) {
    try {
      RootInfo r = fetch_root(args);
      const std::string newNormRoot = didzk::normalize_field_hex(r.root, projectRoot);
      if (r.epoch > oldRoot.epoch || newNormRoot != oldNormRoot) {
        outRoot = r;
        return true;
      }
    } catch (const std::exception& e) {
      poseidon_debug_log(std::string("wait_for_new_root fetch_root skipped: ") + e.what());
    }
    return false;
  });
}
