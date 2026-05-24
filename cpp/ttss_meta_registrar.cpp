#include "ttss_meta_registrar.hpp"

#include "normalize_utils.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

using Clock = AppClock;

}  // namespace

std::future<TTSSMetaWork> TTSSMetaRegistrar::start_setup(
    Args args,
    RegisterResponse reg,
    bool registerMetaMergedUsable,
    bool shouldObserveDeferredMeta,
    std::string idHash,
    std::uint64_t ver,
    std::uint64_t epoch,
    std::string vkSetHash,
    std::string metaHash,
    std::string requestId,
    AppClock::time_point registerTTSSMetaStart) {
  return std::async(std::launch::async, [=]() mutable -> TTSSMetaWork {
    TTSSMetaWork out;
    if (registerMetaMergedUsable) {
      out.resp = {
          {"ok", 1},
          {"merged", 1},
          {"txHash", reg.ttssMetaTxHash},
          {"effectiveMeta", {{"idHash", idHash}, {"ver", ver}, {"epoch", epoch}, {"vkSetHash", vkSetHash}, {"metaHash", metaHash}}}};
      return out;
    }

    if (shouldObserveDeferredMeta) {
      Args observeArgs = args;
      observeArgs.rootWaitTimeoutMs = std::min(args.rootWaitTimeoutMs, 250);
      observeArgs.rootPollMs = std::min(args.rootPollMs, 50);
      const auto deferredObserveStart = Clock::now();
      out.deferredObserved = wait_for_ttss_meta_ready(observeArgs, idHash, ver, epoch,
                                                      vkSetHash, metaHash,
                                                      &out.observed, &out.diag);
      out.deferredObserveMs = ms_between(deferredObserveStart, Clock::now());
      if (out.deferredObserved) {
        out.resp = {
            {"ok", 1},
            {"deferred", 1},
            {"effectiveMeta", out.observed},
        };
        out.totalMs = ms_between(registerTTSSMetaStart, Clock::now());
        return out;
      }
    }

    out.resp = post_ttss_meta_endpoint(
        args,
        "/registerTTSSMeta",
        {{"id", args.id},
         {"idHash", idHash},
         {"ver", ver},
         {"epoch", epoch},
         {"vkSetHash", vkSetHash},
         {"metaHash", metaHash},
         {"wait", 1},
         {"confirmations", 1},
         {"fast", 1},
         {"requestId", requestId}});
    out.diag.clear();
    out.totalMs = ms_between(registerTTSSMetaStart, Clock::now());
    return out;
  });
}

bool TTSSMetaRegistrar::response_matches(const nlohmann::json& resp,
                                         const std::string& vkSetHash,
                                         const std::string& metaHash,
                                         std::uint64_t ver,
                                         std::uint64_t epoch) {
  if (!resp.contains("effectiveMeta")) return false;
  const auto effective = resp.value("effectiveMeta", nlohmann::json::object());
  return didzk::normalize_digest_hex32(effective.value("vkSetHash", std::string())) ==
             didzk::normalize_digest_hex32(vkSetHash) &&
         didzk::normalize_digest_hex32(effective.value("metaHash", std::string())) ==
             didzk::normalize_digest_hex32(metaHash) &&
         static_cast<std::uint64_t>(effective.value("ver", 0ull)) == ver &&
         static_cast<std::uint64_t>(effective.value("epoch", 0ull)) == epoch;
}

TTSSMetaWork TTSSMetaRegistrar::apply_or_wait_rotate(const Args& args,
                                                     const RecoveryRotateResponse& rotateResp,
                                                     const std::string& id,
                                                     const std::string& idHash,
                                                     std::uint64_t ver,
                                                     std::uint64_t epoch,
                                                     const std::string& vkSetHash,
                                                     const std::string& metaHash) {
  TTSSMetaWork out;
  const auto setStart = Clock::now();
  const std::uint64_t mergedEpoch = rotateResp.ttssEpoch != 0 ? rotateResp.ttssEpoch : epoch;
  const bool rotateMetaMerged = rotateResp.ttssMerged;
  const std::string expectedVkSetHash = didzk::normalize_digest_hex32(vkSetHash);
  const std::string expectedMetaHash = didzk::normalize_digest_hex32(metaHash);
  std::string responseDiag;
  std::string rotateRespVkSetHashNorm;
  std::string rotateRespMetaHashNorm;
  const bool respVkReady = didzk::try_normalize_digest_hex32(
      rotateResp.ttssVkSetHash, "rotateResp.ttssVkSetHash", &rotateRespVkSetHashNorm, &responseDiag);
  const bool respMetaReady = didzk::try_normalize_digest_hex32(
      rotateResp.ttssMetaHash, "rotateResp.ttssMetaHash", &rotateRespMetaHashNorm, &responseDiag);
  const bool responseHashesPresent = respVkReady && respMetaReady;
  const bool responseHashesMatch = responseHashesPresent &&
      rotateRespVkSetHashNorm == expectedVkSetHash &&
      rotateRespMetaHashNorm == expectedMetaHash;

  if (rotateMetaMerged) {
    out.resp = {
        {"ok", 1},
        {"merged", 1},
        {"txHash", rotateResp.ttssMetaTxHash},
        {"effectiveMeta", {{"idHash", idHash}, {"ver", ver}, {"epoch", mergedEpoch}, {"vkSetHash", vkSetHash}, {"metaHash", metaHash}}}};
  } else {
    out.resp = post_ttss_meta_endpoint(
        args,
        "/applyRecoveryRotateTTSS",
        {{"id", id},
         {"idHash", idHash},
         {"ver", ver},
         {"epoch", epoch},
         {"vkSetHash", vkSetHash},
         {"metaHash", metaHash},
         {"wait", 0},
         {"confirmations", 1},
         {"fast", 1},
         {"requestId", std::string("ttss_meta_rotate:") + id + ":" + std::to_string(ver)}},
        true);
  }
  out.setMs = ms_between(setStart, Clock::now());

  const auto waitStart = Clock::now();
  if (rotateMetaMerged) {
    if (responseHashesMatch) {
      out.observed = out.resp.at("effectiveMeta");
      out.diag = "merged_in_applyRecoveryRotateZk";
    } else {
      if (!responseDiag.empty()) {
        out.diag = "merged_response_unusable: " + responseDiag;
      } else if (!responseHashesPresent) {
        out.diag = "merged_response_missing_hashes";
      }
      if (!wait_for_ttss_meta_ready(args, idHash, ver, mergedEpoch, vkSetHash, metaHash, &out.observed, &out.diag)) {
        throw std::runtime_error("ttss_meta_not_ready_after_rotate_merged: " + out.diag);
      }
    }
  } else {
    const auto effectiveMeta = out.resp.value("effectiveMeta", nlohmann::json::object());
    std::string effectiveVkSetHashNorm;
    std::string effectiveMetaHashNorm;
    const bool effectiveVkReady = didzk::try_normalize_digest_hex32(
        effectiveMeta.value("vkSetHash", std::string()),
        "ttssMetaRotateResp.effectiveMeta.vkSetHash",
        &effectiveVkSetHashNorm,
        &responseDiag);
    const bool effectiveMetaReady = didzk::try_normalize_digest_hex32(
        effectiveMeta.value("metaHash", std::string()),
        "ttssMetaRotateResp.effectiveMeta.metaHash",
        &effectiveMetaHashNorm,
        &responseDiag);
    const bool rotateMetaFromResponse = out.resp.contains("effectiveMeta") &&
        effectiveVkReady &&
        effectiveMetaReady &&
        effectiveVkSetHashNorm == expectedVkSetHash &&
        effectiveMetaHashNorm == expectedMetaHash &&
        static_cast<std::uint64_t>(effectiveMeta.value("ver", 0ull)) == ver &&
        static_cast<std::uint64_t>(effectiveMeta.value("epoch", 0ull)) == epoch;
    if (rotateMetaFromResponse) {
      out.observed = out.resp.at("effectiveMeta");
      out.diag = "response_effective_meta";
    } else {
      if (out.resp.contains("effectiveMeta") && !responseDiag.empty()) {
        out.diag = "response_effective_meta_unusable: " + responseDiag;
      }
      if (!wait_for_ttss_meta_ready(args, idHash, ver, epoch, vkSetHash, metaHash, &out.observed, &out.diag)) {
        throw std::runtime_error("ttss_meta_not_ready_after_rotate: " + out.diag);
      }
    }
  }
  out.waitMs = ms_between(waitStart, Clock::now());
  return out;
}
