#include <iostream>
#include <string>
#include <vector>

#include "share_envelope.hpp"
#include "ttss_nits_shamir.hpp"

int main() {
  using namespace didzk;

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

  std::cout << "share_count=" << setup.shareEnvelopes.size() << "\n";
  std::cout << "vk_count=" << setup.vk.size() << "\n";
  std::cout << "vkSetHash=" << setup.vkSetHash << "\n";
  std::cout << "rhoCommitHex=" << setup.rhoCommitHex << "\n";

  for (std::size_t i = 0; i < setup.shareEnvelopes.size(); ++i) {
    std::string err;
    const bool ok_basic = basic_validate_share_envelope(setup.shareEnvelopes[i], &err);
    if (!ok_basic) {
      std::cerr << "basic_validate failed @" << i << " err=" << err << "\n";
      return 2;
    }
    const bool ok_sig = verify_share_envelope_demo(setup.shareEnvelopes[i], &err);
    if (!ok_sig) {
      std::cerr << "verify_share_envelope_demo failed @" << i << " err=" << err << "\n";
      return 3;
    }
  }

  std::vector<ShareEnvelope> shares3 = {
      setup.shareEnvelopes[0],
      setup.shareEnvelopes[2],
      setup.shareEnvelopes[4],
  };
  std::string verify_err;
  auto rec_ok = ttss_rec_nits_shamir(shares3, &verify_err);
  std::cout << "recover_ok=" << rec_ok.ok
            << " shareCountUsed=" << rec_ok.shareCountUsed
            << " recovered=" << rec_ok.srecSeedHex
            << " err=" << rec_ok.err << "\n";

  if (!rec_ok.ok) return 4;
  if (normalize_digest_hex32(rec_ok.srecSeedHex) != normalize_digest_hex32(srec_seed)) {
    std::cerr << "recovered seed mismatch\n";
    return 5;
  }

  std::vector<ShareEnvelope> shares2 = {
      setup.shareEnvelopes[0],
      setup.shareEnvelopes[1],
  };
  auto rec_bad = ttss_rec_nits_shamir(shares2, &verify_err);
  std::cout << "recover_with_2_ok=" << rec_bad.ok
            << " err=" << rec_bad.err << "\n";

  if (rec_bad.ok) {
    std::cerr << "unexpected success with < t shares\n";
    return 6;
  }

  std::cout << "PASS\n";
  return 0;
}
