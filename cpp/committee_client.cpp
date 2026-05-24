#include "ttss_flow.hpp"

#include <stdexcept>
#include <string>

bool committee_set_share_envelope(const std::string& url,
                                  const std::string& token,
                                  const didzk::ShareEnvelope& env,
                                  std::string* err) {
  nlohmann::json body;
  body["token"] = token;
  body["shareEnvelope"] = share_envelope_to_njson(env);
  std::string resp;
  if (!http_post_json(url, "/setShareEnvelope", body, resp, 5000)) {
    if (err) *err = resp;
    return false;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(resp);
    if (j.value("ok", 0) != 1 || j.value("stored", 0) != 1) {
      if (err) *err = resp;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

bool committee_fetch_share(const std::string& url,
                           const std::string& endpoint,
                           const std::string& token,
                           const std::string& idHash,
                           std::uint64_t ver,
                           std::uint64_t epoch,
                           std::uint32_t guardianIndex,
                           didzk::ShareEnvelope* out,
                           std::string* err) {
  nlohmann::json body;
  body["token"] = token;
  body["idHash"] = idHash;
  body["ver"] = ver;
  body["epoch"] = epoch;
  body["guardianIndex"] = guardianIndex;
  body["nonce"] = std::string("phase3_") + endpoint;
  body["auditToken"] = "phase3";
  if (endpoint == "/shareForTrace") body["requestKind"] = "honest_challenge";
  std::string resp;
  if (!http_post_json(url, endpoint, body, resp, 5000)) {
    if (err) *err = resp;
    return false;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(resp);
    if (j.value("ok", 0) != 1) {
      if (err) *err = resp;
      return false;
    }
    if (out) *out = didzk::share_envelope_from_njson(j.at("shareEnvelope"));
    return true;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

bool committee_invalidate(const std::string& url,
                          const std::string& token,
                          const std::string& idHash,
                          std::uint64_t ver,
                          std::uint64_t epoch,
                          const std::string& reason,
                          std::string* err) {
  nlohmann::json body;
  body["token"] = token;
  body["idHash"] = idHash;
  body["ver"] = ver;
  body["epoch"] = epoch;
  body["reason"] = reason;
  std::string resp;
  if (!http_post_json(url, "/invalidateShares", body, resp, 5000)) {
    if (err) *err = resp;
    return false;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(resp);
    if (j.value("ok", 0) != 1) {
      if (err) *err = resp;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

nlohmann::json committee_get_share_meta(const std::string& url,
                                        const std::string& idHash,
                                        std::uint64_t ver,
                                        std::uint64_t epoch,
                                        std::uint32_t guardianIndex) {
  std::string body;
  const std::string path = "/shareMeta?idHash=" + url_encode(idHash) +
                           "&ver=" + std::to_string(ver) +
                           "&epoch=" + std::to_string(epoch) +
                           "&guardianIndex=" + std::to_string(guardianIndex);
  if (!http_get_text(url, path, body, 5000)) {
    throw std::runtime_error("committee_get_share_meta_failed: " + body);
  }
  return nlohmann::json::parse(body);
}

std::vector<didzk::ShareEnvelope> recover_shares_from_committees(const nlohmann::json& setupJson,
                                                                 const Args& args) {
  const std::string idHash = setupJson.value("idHash", std::string());
  const std::uint64_t ver = static_cast<std::uint64_t>(setupJson.value("ver", 0ull));
  const std::uint64_t epoch = static_cast<std::uint64_t>(setupJson.value("epoch", 0ull));
  const int threshold = setupJson.value("t", args.ttssT);
  const auto urls = setupJson.at("committeeUrls").get<std::vector<std::string>>();
  std::vector<didzk::ShareEnvelope> shares;
  shares.reserve(static_cast<std::size_t>(threshold));
  for (int i = 0; i < threshold; ++i) {
    didzk::ShareEnvelope env;
    std::string err;
    if (!committee_fetch_share(urls.at(static_cast<std::size_t>(i)), "/shareForRecover", args.committeeToken,
                               idHash, ver, epoch, static_cast<std::uint32_t>(i + 1), &env, &err)) {
      throw std::runtime_error("committee_fetch_shareForRecover_failed: " + err);
    }
    shares.push_back(std::move(env));
  }
  return shares;
}
