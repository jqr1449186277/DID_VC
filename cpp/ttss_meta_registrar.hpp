#pragma once

#include <future>

#include "ttss_flow.hpp"

struct TTSSMetaWork {
  nlohmann::json resp;
  nlohmann::json observed;
  std::string diag;
  bool deferredObserved{false};
  double deferredObserveMs{0.0};
  double totalMs{0.0};
  double setMs{0.0};
  double waitMs{0.0};
};

class TTSSMetaRegistrar {
 public:
  static std::future<TTSSMetaWork> start_setup(
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
      AppClock::time_point registerTTSSMetaStart);

  static bool response_matches(const nlohmann::json& resp,
                               const std::string& vkSetHash,
                               const std::string& metaHash,
                               std::uint64_t ver,
                               std::uint64_t epoch);

  static TTSSMetaWork apply_or_wait_rotate(const Args& args,
                                           const RecoveryRotateResponse& rotateResp,
                                           const std::string& id,
                                           const std::string& idHash,
                                           std::uint64_t ver,
                                           std::uint64_t epoch,
                                           const std::string& vkSetHash,
                                           const std::string& metaHash);
};
