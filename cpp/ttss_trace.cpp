#include "ttss_trace.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace didzk {
namespace {

std::string transcript_hash_of(const std::vector<TraceQueryResponse>& responses) {
  std::vector<std::string> parts;
  parts.reserve(responses.size());
  for (const auto& r : responses) {
    std::ostringstream oss;
    oss << (r.ok ? 1 : 0) << '|' << r.resultType << '|' << r.srecSeedHex << '|';
    for (const auto& root : r.debugCandidateRootsHex) {
      oss << normalize_field_hex_bn254(root) << ',';
    }
    oss << '|' << r.err;
    parts.push_back(oss.str());
  }
  return sha256_joined_hex(parts);
}

std::vector<std::uint32_t> accused_from_roots(const std::vector<TTSSKeyEntry>& tk_entries,
                                              const std::vector<std::string>& roots_hex) {
  std::vector<std::uint32_t> accused;
  std::set<std::uint32_t> seen;
  for (const auto& root : roots_hex) {
    const std::string ui = compute_ui_hex_from_x(root);
    for (const auto& entry : tk_entries) {
      if (lower_copy(normalize_digest_hex32(entry.uiHex)) == lower_copy(normalize_digest_hex32(ui))) {
        if (seen.insert(entry.guardianIndex).second) {
          accused.push_back(entry.guardianIndex);
        }
      }
    }
  }
  std::sort(accused.begin(), accused.end());
  return accused;
}

}  // namespace

bool ttss_tverify_nits_shamir(const std::vector<TTSSKeyEntry>& vk_entries,
                              const std::vector<std::uint32_t>& accused_set,
                              const TraceProof& proof,
                              std::string* err) {
  auto fail = [&](const std::string& e) {
    if (err) *err = e;
    return false;
  };
  if (proof.proofType != "nits_roots") return fail("unsupported_proof_type");
  if (accused_set.size() != proof.witnessRootsHex.size()) return fail("arity_mismatch");
  if (accused_set.empty()) return fail("empty_accused_set");

  std::map<std::uint32_t, std::string> vk_by_index;
  for (const auto& e : vk_entries) {
    vk_by_index[e.guardianIndex] = normalize_digest_hex32(e.uiHex);
  }

  std::set<std::uint32_t> seen;
  for (std::size_t i = 0; i < accused_set.size(); ++i) {
    const auto idx = accused_set[i];
    if (!seen.insert(idx).second) return fail("duplicate_accused_index");
    const auto it = vk_by_index.find(idx);
    if (it == vk_by_index.end()) return fail("accused_index_missing_from_vk");
    const std::string root_ui = compute_ui_hex_from_x(proof.witnessRootsHex[i]);
    if (lower_copy(root_ui) != lower_copy(it->second)) {
      return fail("root_ui_mismatch");
    }
  }
  if (err) err->clear();
  return true;
}

TraceResult ttss_trace_from_extracted_roots(const std::vector<TTSSKeyEntry>& tk_entries,
                                            const TraceSessionDescriptor& desc,
                                            const std::vector<std::string>& roots_hex,
                                            const TraceMeta& meta) {
  TraceResult out;
  out.idHash = normalize_digest_hex32(desc.idHash);
  out.ver = desc.ver;
  out.epoch = desc.epoch;
  out.n = desc.n;
  out.t = desc.t;
  out.delta = desc.delta;
  out.traceMeta = meta;

  if (roots_hex.empty()) {
    out.err = "no_extracted_roots";
    return out;
  }

  std::vector<std::string> roots_norm;
  roots_norm.reserve(roots_hex.size());
  for (const auto& root : roots_hex) {
    roots_norm.push_back(normalize_field_hex_bn254(root));
  }

  out.accusedSet = accused_from_roots(tk_entries, roots_norm);
  out.proof.proofType = "nits_roots";
  out.proof.witnessRootsHex = roots_norm;

  std::string verify_err;
  if (!ttss_tverify_nits_shamir(tk_entries, out.accusedSet, out.proof, &verify_err)) {
    out.err = verify_err;
    return out;
  }

  out.ok = true;
  return out;
}

TraceResult ttss_trace_nits_shamir(const std::vector<TTSSKeyEntry>& tk_entries,
                                   const TraceSessionDescriptor& desc,
                                   ReconstructionOracle& oracle,
                                   HonestShareProvider& provider,
                                   const TraceOptions& options) {
  TraceResult out;
  out.idHash = normalize_digest_hex32(desc.idHash);
  out.ver = desc.ver;
  out.epoch = desc.epoch;
  out.n = desc.n;
  out.t = desc.t;
  out.delta = desc.delta;

  const std::uint32_t challenge_count =
      options.challengeShareCount == 0 ? (desc.t > 0 ? desc.t - 1 : 0) : options.challengeShareCount;
  if (challenge_count == 0) {
    out.err = "bad_challenge_share_count";
    return out;
  }

  std::vector<TraceQueryResponse> responses;
  std::vector<std::string> all_roots;
  std::uint32_t success_count = 0;

  for (std::uint32_t q = 0; q < options.maxQueries; ++q) {
    TraceQueryRequest req;
    req.queryId = "trace_q_" + std::to_string(q);
    req.honestShares = provider.fetch_challenge_shares(challenge_count);
    TraceQueryResponse resp = oracle.query(req);
    if (resp.ok) {
      ++success_count;
      for (const auto& root : resp.debugCandidateRootsHex) {
        all_roots.push_back(root);
      }
    }
    responses.push_back(std::move(resp));
  }

  TraceMeta meta;
  meta.queryCount = static_cast<std::uint32_t>(responses.size());
  meta.oracleSuccessRate = responses.empty() ? 0.0 : static_cast<double>(success_count) / responses.size();
  meta.transcriptHash = transcript_hash_of(responses);
  meta.note = "v1_trace_scaffold_uses_oracle_debugCandidateRootsHex_for_root_extraction";

  std::sort(all_roots.begin(), all_roots.end());
  all_roots.erase(std::unique(all_roots.begin(), all_roots.end()), all_roots.end());
  meta.guessedF = static_cast<std::uint32_t>(all_roots.size());
  meta.candidatePolynomialCount = all_roots.empty() ? 0u : 1u;

  if (all_roots.empty() && options.requireOracleRoots) {
    out.traceMeta = meta;
    out.err = "trace_extractor_unavailable: oracle returned no debugCandidateRootsHex";
    return out;
  }

  return ttss_trace_from_extracted_roots(tk_entries, desc, all_roots, meta);
}

nlohmann::json trace_result_to_njson(const TraceResult& result) {
  nlohmann::json witnessRoots = nlohmann::json::array();
  for (const auto& root : result.proof.witnessRootsHex) {
    witnessRoots.push_back(normalize_field_hex_bn254(root));
  }

  return {
      {"ok", result.ok},
      {"scheme", result.scheme},
      {"idHash", normalize_digest_hex32(result.idHash)},
      {"ver", result.ver},
      {"epoch", result.epoch},
      {"n", result.n},
      {"t", result.t},
      {"delta", result.delta},
      {"accusedSet", result.accusedSet},
      {"proof",
       {{"proofType", result.proof.proofType},
        {"witnessRootsHex", witnessRoots}}},
      {"traceMeta",
       {{"guessedF", result.traceMeta.guessedF},
        {"queryCount", result.traceMeta.queryCount},
        {"candidatePolynomialCount", result.traceMeta.candidatePolynomialCount},
        {"oracleSuccessRate", result.traceMeta.oracleSuccessRate},
        {"transcriptHash", result.traceMeta.transcriptHash},
        {"note", result.traceMeta.note}}},
      {"err", result.err},
  };
}

std::string trace_result_to_json(const TraceResult& result, int indent) {
  const nlohmann::json j = trace_result_to_njson(result);
  return j.dump(indent > 0 ? indent : 2);
}

}  // namespace didzk
