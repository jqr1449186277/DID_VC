// committee_node.cpp
//
// TTSS/Trace v1 committee node for did-e2e.
//
// Build example:
//   g++ -O2 -std=c++17 committee_node.cpp share_envelope.cpp -pthread -o committee_node
//
// TTSS v1 JSON endpoints:
//   POST /setShareEnvelope
//   GET  /shareMeta?idHash=...&ver=...&epoch=...&guardianIndex=...
//   POST /shareForRecover
//   POST /shareForTrace
//   POST /invalidateShares
//   GET  /debugListShares?token=...
//
// Notes:
// - Signature validation uses the demo verification routine from share_envelope.cpp.
// - v1 stores data in memory only.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "httplib.h"
#include "json_utils.hpp"
#include "share_envelope.hpp"
#include "text_utils.hpp"

using didzk::ShareEnvelope;
using didzk::json_object_field;
using didzk::json_string_field;
using didzk::json_u32_field;
using didzk::json_u64_field;
using didzk::normalize_digest_hex32;
using didzk::parse_share_envelope_from_njson;
using didzk::parse_boolish;
using didzk::share_envelope_to_njson;
using didzk::share_key;
using didzk::verify_share_envelope_demo;

namespace {

using Json = nlohmann::json;

struct StoredEnvelope {
  ShareEnvelope env;
  std::string shareKey;
  std::uint64_t storedAtMs{0};
  std::uint64_t invalidatedAtMs{0};
  std::string invalidatedReason;
};

struct CommitteeConfig {
  int port{8001};
  std::string token{"demo-token"};
  int delayMs{0};
  int jitterMs{0};
  double failRate{0.0};
  bool debugEndpoints{true};
};

struct CommitteeStore {
  std::unordered_map<std::string, StoredEnvelope> ttssShares;
  std::multimap<std::string, std::string> byVersion;
  std::mutex mu;
};

static int get_int_arg(int& i, int argc, char** argv, int def) {
  if (i + 1 >= argc) return def;
  return std::stoi(argv[++i]);
}

static std::string get_str_arg(int& i, int argc, char** argv, const std::string& def) {
  if (i + 1 >= argc) return def;
  return argv[++i];
}

static std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

static void send_json(httplib::Response& res, const Json& body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

static void send_error(httplib::Response& res, const std::string& err, int status = 400) {
  send_json(res, {{"ok", 0}, {"err", err}}, status);
}

static Json parse_json_body(const httplib::Request& req) {
  return Json::parse(req.body);
}

static Json share_meta_json(const StoredEnvelope& stored) {
  const auto& env = stored.env;
  return {
      {"ok", 1},
      {"idHash", normalize_digest_hex32(env.idHash)},
      {"ver", env.ver},
      {"epoch", env.epoch},
      {"guardianIndex", env.guardianIndex},
      {"guardianId", env.guardianId},
      {"active", env.active},
      {"scheme", env.scheme},
      {"uiHex", normalize_digest_hex32(env.traceBinding.uiHex)},
      {"tagHex", normalize_digest_hex32(env.tag.tagHex)},
      {"vkSetHash", env.meta.vkSetHash.empty() ? std::string() : normalize_digest_hex32(env.meta.vkSetHash)},
      {"guardianSetHash", env.meta.guardianSetHash.empty() ? std::string() : normalize_digest_hex32(env.meta.guardianSetHash)},
  };
}

static Json debug_list_json(const std::vector<StoredEnvelope>& list) {
  Json items = Json::array();
  for (const auto& item : list) {
    items.push_back({
        {"shareKey", item.shareKey},
        {"storedAtMs", item.storedAtMs},
        {"invalidatedAtMs", item.invalidatedAtMs},
        {"invalidatedReason", item.invalidatedReason},
        {"envelope", share_envelope_to_njson(item.env)},
    });
  }
  return {{"ok", 1}, {"count", list.size()}, {"items", items}};
}

static bool require_token_form(const httplib::Request& req,
                               const std::string& expected_token,
                               httplib::Response& res) {
  auto tok = req.get_param_value("token");
  if (tok != expected_token) {
    send_error(res, "bad_token", 401);
    return false;
  }
  return true;
}

static bool require_token_json(const Json& body,
                               const std::string& expected_token,
                               httplib::Response& res) {
  const std::string tok = json_string_field(body, "token");
  if (tok != expected_token) {
    send_error(res, "bad_token", 401);
    return false;
  }
  return true;
}

static std::string version_key(const std::string& id_hash, std::uint64_t ver, std::uint64_t epoch) {
  return normalize_digest_hex32(id_hash) + ":" + std::to_string(ver) + ":" + std::to_string(epoch);
}

static std::string share_lookup_key(const std::string& id_hash,
                                    std::uint64_t ver,
                                    std::uint64_t epoch,
                                    std::uint32_t guardian_index) {
  return version_key(id_hash, ver, epoch) + ":" + std::to_string(guardian_index);
}

static CommitteeConfig parse_config(int argc, char** argv) {
  CommitteeConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port") cfg.port = get_int_arg(i, argc, argv, cfg.port);
    else if (a == "--token") cfg.token = get_str_arg(i, argc, argv, cfg.token);
    else if (a == "--delay_ms") cfg.delayMs = get_int_arg(i, argc, argv, cfg.delayMs);
    else if (a == "--jitter_ms") cfg.jitterMs = get_int_arg(i, argc, argv, cfg.jitterMs);
    else if (a == "--fail_rate") cfg.failRate = std::stod(get_str_arg(i, argc, argv, "0"));
    else if (a == "--debug_endpoints") cfg.debugEndpoints = parse_boolish(get_str_arg(i, argc, argv, "1"), true);
    else if (a == "--help" || a == "-h") {
      std::cout
          << "Usage: " << argv[0]
          << " --port <p> --token <t> --delay_ms <ms> --jitter_ms <ms> --fail_rate <0..1> --debug_endpoints <0|1>\n";
      std::exit(0);
    }
  }
  return cfg;
}

static bool parse_u64_param(const httplib::Request& req,
                            const char* name,
                            std::uint64_t def,
                            std::uint64_t* out) {
  if (!req.has_param(name)) {
    *out = def;
    return true;
  }
  try {
    *out = static_cast<std::uint64_t>(std::stoull(req.get_param_value(name)));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

static bool parse_u32_param(const httplib::Request& req,
                            const char* name,
                            std::uint32_t def,
                            std::uint32_t* out) {
  std::uint64_t tmp = 0;
  if (!parse_u64_param(req, name, def, &tmp)) return false;
  if (tmp > UINT32_MAX) return false;
  *out = static_cast<std::uint32_t>(tmp);
  return true;
}

static bool maybe_simulate_network(const CommitteeConfig& cfg, httplib::Response& res) {
  thread_local std::mt19937_64 tl_rng(
      static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&res)));
  if (cfg.jitterMs > 0) {
    std::uniform_int_distribution<int> jitter(0, cfg.jitterMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.delayMs + jitter(tl_rng)));
  } else if (cfg.delayMs > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.delayMs));
  }
  if (cfg.failRate > 0.0) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    if (uni(tl_rng) < cfg.failRate) {
      send_error(res, "sim_fail", 503);
      return false;
    }
  }
  return true;
}

static void register_health_route(httplib::Server& svr) {
  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    send_json(res, {
        {"ok", 1},
        {"service", "committee_node"},
        {"scheme", didzk::kTTSSSchemeNitsShamirV1},
        {"timeMs", now_ms()},
    });
  });
}

static void register_set_share_envelope_route(httplib::Server& svr,
                                              const CommitteeConfig& cfg,
                                              CommitteeStore& store) {
  svr.Post("/setShareEnvelope", [&](const httplib::Request& req, httplib::Response& res) {
    Json body;
    try {
      body = parse_json_body(req);
    } catch (const std::exception& e) {
      send_error(res, std::string("bad_json:") + e.what());
      return;
    }
    if (!require_token_json(body, cfg.token, res)) return;
    const Json* envv = json_object_field(body, "shareEnvelope");
    if (!envv) {
      send_error(res, "missing_shareEnvelope");
      return;
    }
    ShareEnvelope env;
    std::string parse_err;
    if (!parse_share_envelope_from_njson(*envv, &env, &parse_err)) {
      send_error(res, parse_err);
      return;
    }
    std::string verify_err;
    if (!verify_share_envelope_demo(env, &verify_err)) {
      send_error(res, std::string("share_verify_failed:") + verify_err);
      return;
    }

    StoredEnvelope stored;
    stored.env = env;
    stored.shareKey = share_key(env);
    stored.storedAtMs = now_ms();

    {
      std::lock_guard<std::mutex> lk(store.mu);
      store.ttssShares[stored.shareKey] = stored;
      store.byVersion.emplace(version_key(env.idHash, env.ver, env.epoch), stored.shareKey);
    }

    send_json(res, {{"ok", 1}, {"stored", 1}, {"shareKey", stored.shareKey}, {"active", env.active}});
  });
}

static void register_share_meta_route(httplib::Server& svr, CommitteeStore& store) {
  svr.Get("/shareMeta", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string id_hash = req.get_param_value("idHash");
    std::uint64_t ver = 0;
    std::uint64_t epoch = 0;
    std::uint32_t guardian_index = 0;
    if (!parse_u64_param(req, "ver", 0, &ver) ||
        !parse_u64_param(req, "epoch", 0, &epoch) ||
        !parse_u32_param(req, "guardianIndex", 0, &guardian_index) ||
        id_hash.empty() || epoch == 0 || guardian_index == 0) {
      send_error(res, "bad_args");
      return;
    }

    StoredEnvelope stored;
    {
      std::lock_guard<std::mutex> lk(store.mu);
      auto it = store.ttssShares.find(share_lookup_key(id_hash, ver, epoch, guardian_index));
      if (it == store.ttssShares.end()) {
        send_error(res, "no_share_meta", 404);
        return;
      }
      stored = it->second;
    }
    send_json(res, share_meta_json(stored));
  });
}

static bool find_requested_envelope(const Json& body, CommitteeStore& store, StoredEnvelope* out, httplib::Response& res) {
  const std::string id_hash = json_string_field(body, "idHash");
  const std::uint64_t ver = json_u64_field(body, "ver", 0);
  const std::uint64_t epoch = json_u64_field(body, "epoch", 0);
  const std::uint32_t guardian_index = json_u32_field(body, "guardianIndex", 0);
  if (id_hash.empty() || epoch == 0) {
    send_error(res, "bad_args");
    return false;
  }

  std::string skey;
  StoredEnvelope stored;
  {
    std::lock_guard<std::mutex> lk(store.mu);
    if (guardian_index != 0) {
      skey = share_lookup_key(id_hash, ver, epoch, guardian_index);
      auto it = store.ttssShares.find(skey);
      if (it == store.ttssShares.end()) {
        send_error(res, "no_share", 404);
        return false;
      }
      stored = it->second;
    } else {
      const std::string vkey = version_key(id_hash, ver, epoch);
      auto range = store.byVersion.equal_range(vkey);
      for (auto it = range.first; it != range.second; ++it) {
        auto jt = store.ttssShares.find(it->second);
        if (jt != store.ttssShares.end() && jt->second.env.active) {
          stored = jt->second;
          skey = jt->second.shareKey;
          break;
        }
      }
      if (skey.empty()) {
        send_error(res, "no_active_share", 404);
        return false;
      }
    }
    if (!stored.env.active) {
      send_error(res, "share_inactive", 409);
      return false;
    }
  }
  *out = stored;
  return true;
}

static void serve_share_envelope_request(const httplib::Request& req,
                                         httplib::Response& res,
                                         const std::string& endpoint_name,
                                         const CommitteeConfig& cfg,
                                         CommitteeStore& store) {
  try {
    Json body = parse_json_body(req);
    if (!require_token_json(body, cfg.token, res)) return;
    if (!maybe_simulate_network(cfg, res)) return;

    StoredEnvelope stored;
    if (!find_requested_envelope(body, store, &stored, res)) return;

    send_json(res, {
        {"ok", 1},
        {"endpoint", endpoint_name},
        {"shareEnvelope", share_envelope_to_njson(stored.env)},
    });
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[committee_node] " << endpoint_name << " json_error: " << e.what() << "\n";
    send_error(res, std::string("json_error:") + e.what(), 400);
  } catch (const std::exception& e) {
    std::cerr << "[committee_node] " << endpoint_name << " internal_error: " << e.what() << "\n";
    send_error(res, std::string("internal_error:") + e.what(), 500);
  }
}

static void register_share_request_routes(httplib::Server& svr,
                                          const CommitteeConfig& cfg,
                                          CommitteeStore& store) {
  svr.Post("/shareForRecover", [&](const httplib::Request& req, httplib::Response& res) {
    serve_share_envelope_request(req, res, "shareForRecover", cfg, store);
  });

  svr.Post("/shareForTrace", [&](const httplib::Request& req, httplib::Response& res) {
    serve_share_envelope_request(req, res, "shareForTrace", cfg, store);
  });
}

static void register_invalidate_route(httplib::Server& svr,
                                      const CommitteeConfig& cfg,
                                      CommitteeStore& store) {
  svr.Post("/invalidateShares", [&](const httplib::Request& req, httplib::Response& res) {
    Json body;
    try {
      body = parse_json_body(req);
    } catch (const std::exception& e) {
      send_error(res, std::string("bad_json:") + e.what());
      return;
    }
    if (!require_token_json(body, cfg.token, res)) return;

    const std::string id_hash = json_string_field(body, "idHash");
    const std::uint64_t ver = json_u64_field(body, "ver", 0);
    const std::uint64_t epoch = json_u64_field(body, "epoch", 0);
    const std::string reason = json_string_field(body, "reason");
    if (id_hash.empty() || epoch == 0) {
      send_error(res, "bad_args");
      return;
    }
    std::size_t invalidated = 0;
    {
      std::lock_guard<std::mutex> lk(store.mu);
      auto range = store.byVersion.equal_range(version_key(id_hash, ver, epoch));
      for (auto it = range.first; it != range.second; ++it) {
        auto jt = store.ttssShares.find(it->second);
        if (jt != store.ttssShares.end() && jt->second.env.active) {
          jt->second.env.active = false;
          jt->second.invalidatedAtMs = now_ms();
          jt->second.invalidatedReason = reason;
          ++invalidated;
        }
      }
    }
    send_json(res, {{"ok", 1}, {"invalidated", invalidated}});
  });
}

static void register_debug_routes(httplib::Server& svr,
                                  const CommitteeConfig& cfg,
                                  CommitteeStore& store) {
  if (!cfg.debugEndpoints) return;
  svr.Get("/debugListShares", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_token_form(req, cfg.token, res)) return;
    std::vector<StoredEnvelope> list;
    {
      std::lock_guard<std::mutex> lk(store.mu);
      list.reserve(store.ttssShares.size());
      for (const auto& kv : store.ttssShares) list.push_back(kv.second);
    }
    std::sort(list.begin(), list.end(), [](const StoredEnvelope& a, const StoredEnvelope& b) {
      if (a.env.idHash != b.env.idHash) return a.env.idHash < b.env.idHash;
      if (a.env.ver != b.env.ver) return a.env.ver < b.env.ver;
      if (a.env.epoch != b.env.epoch) return a.env.epoch < b.env.epoch;
      return a.env.guardianIndex < b.env.guardianIndex;
    });
    send_json(res, debug_list_json(list));
  });
}

static void register_routes(httplib::Server& svr,
                            const CommitteeConfig& cfg,
                            CommitteeStore& store) {
  register_health_route(svr);
  register_set_share_envelope_route(svr, cfg, store);
  register_share_meta_route(svr, store);
  register_share_request_routes(svr, cfg, store);
  register_invalidate_route(svr, cfg, store);
  register_debug_routes(svr, cfg, store);
}

}  // namespace

int main(int argc, char** argv) {
  const CommitteeConfig cfg = parse_config(argc, argv);
  CommitteeStore store;
  httplib::Server svr;
  register_routes(svr, cfg, store);

  std::cout << "committee_node listening on 0.0.0.0:" << cfg.port
            << " scheme=" << didzk::kTTSSSchemeNitsShamirV1
            << " debug_endpoints=" << (cfg.debugEndpoints ? 1 : 0) << "\n";
  svr.listen("0.0.0.0", cfg.port);
  return 0;
}
