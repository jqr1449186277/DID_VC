#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "share_envelope.hpp"
#include "ttss_nits_shamir.hpp"

namespace didzk {

struct TraceQueryRequest {
  std::string queryId;
  std::vector<ShareEnvelope> honestShares;
};

struct TraceQueryResponse {
  bool ok{false};
  std::string resultType;  // seed | reset_auth | unsupported
  std::string srecSeedHex;
  std::vector<std::string> debugCandidateRootsHex;  // optional extractor hook for v1
  std::string err;
};

class ReconstructionOracle {
 public:
  virtual ~ReconstructionOracle() = default;
  virtual TraceQueryResponse query(const TraceQueryRequest& req) = 0;
};

class HonestShareProvider {
 public:
  virtual ~HonestShareProvider() = default;
  virtual std::vector<ShareEnvelope> fetch_challenge_shares(std::size_t count) = 0;
};

struct TraceProof {
  std::string proofType{"nits_roots"};
  std::vector<std::string> witnessRootsHex;
};

struct TraceMeta {
  std::uint32_t guessedF{0};
  std::uint32_t queryCount{0};
  std::uint32_t candidatePolynomialCount{0};
  double oracleSuccessRate{0.0};
  std::string transcriptHash;
  std::string note;
};

struct TraceSessionDescriptor {
  std::string idHash;
  std::uint64_t ver{0};
  std::uint64_t epoch{0};
  std::uint32_t n{0};
  std::uint32_t t{0};
  double delta{0.0};
};

struct TraceOptions {
  std::uint32_t maxQueries{12};
  std::uint32_t challengeShareCount{0};
  bool requireOracleRoots{true};
};

struct TraceResult {
  bool ok{false};
  std::string scheme{kTTSSSchemeNitsShamirV1};
  std::string idHash;
  std::uint64_t ver{0};
  std::uint64_t epoch{0};
  std::uint32_t n{0};
  std::uint32_t t{0};
  double delta{0.0};
  std::vector<std::uint32_t> accusedSet;
  TraceProof proof;
  TraceMeta traceMeta;
  std::string err;
};

bool ttss_tverify_nits_shamir(const std::vector<TTSSKeyEntry>& vk_entries,
                              const std::vector<std::uint32_t>& accused_set,
                              const TraceProof& proof,
                              std::string* err = nullptr);

TraceResult ttss_trace_from_extracted_roots(const std::vector<TTSSKeyEntry>& tk_entries,
                                            const TraceSessionDescriptor& desc,
                                            const std::vector<std::string>& roots_hex,
                                            const TraceMeta& meta);

TraceResult ttss_trace_nits_shamir(const std::vector<TTSSKeyEntry>& tk_entries,
                                   const TraceSessionDescriptor& desc,
                                   ReconstructionOracle& oracle,
                                   HonestShareProvider& provider,
                                   const TraceOptions& options = {});

nlohmann::json trace_result_to_njson(const TraceResult& result);
std::string trace_result_to_json(const TraceResult& result, int indent = 0);

}  // namespace didzk
