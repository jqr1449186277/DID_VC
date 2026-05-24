#include <iostream>
#include <string>
#include <vector>

#include "share_envelope.hpp"
#include "ttss_nits_shamir.hpp"
#include "ttss_trace.hpp"

using namespace didzk;

int main() {
  const std::vector<std::string> guardians = {"G1", "G2", "G3", "G4", "G5"};
  const std::string srec_seed = "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcd";
  const std::string rho_seed = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";
  const std::string dealer_key = "0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
  const std::string dealer_pk = dealer_key;
  const std::string id = "alice";
  const std::string id_hash = "0x1111111111111111111111111111111111111111111111111111111111111111";
  const std::string guardian_set_hash = "0x2222222222222222222222222222222222222222222222222222222222222222";

  auto setup = ttss_share_nits_shamir(
      srec_seed,
      5,
      3,
      rho_seed,
      id,
      id_hash,
      1,
      10,
      guardians,
      guardian_set_hash,
      dealer_key,
      dealer_pk,
      1234567890,
      0);

  // Pick guardian indexes 2 and 4 in ascending order so accusedSet and proof stay aligned.
  std::vector<std::string> roots = {
      setup.shareEnvelopes[1].share.xiHex,
      setup.shareEnvelopes[3].share.xiHex,
  };

  TraceSessionDescriptor desc;
  desc.idHash = id_hash;
  desc.ver = 1;
  desc.epoch = 10;
  desc.n = 5;
  desc.t = 3;
  desc.delta = 1e-6;

  TraceMeta meta;
  meta.guessedF = 2;
  meta.queryCount = 1;
  meta.candidatePolynomialCount = 1;
  meta.oracleSuccessRate = 1.0;
  meta.transcriptHash = "0xabc";
  meta.note = "phase1_acceptance_extracted_roots_path";

  auto direct = ttss_trace_from_extracted_roots(setup.tk, desc, roots, meta);
  std::cout << "direct.ok=" << direct.ok << " accused=" << direct.accusedSet.size() << "\n";

  std::string verr;
  const bool verified = ttss_tverify_nits_shamir(setup.vk, direct.accusedSet, direct.proof, &verr);
  std::cout << "direct.verify=" << verified << " err=" << verr << "\n";
  if (!direct.ok || !verified) return 2;

  const std::string json = trace_result_to_json(direct, 0);
  if (json.find("\"ok\": true") == std::string::npos ||
      json.find("\"proofType\": \"nits_roots\"") == std::string::npos) {
    std::cerr << "trace_result_to_json missing expected fields\n";
    return 3;
  }

  std::cout << "PASS\n";
  return 0;
}
