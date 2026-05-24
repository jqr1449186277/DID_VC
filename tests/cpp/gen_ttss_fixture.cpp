#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "share_envelope.hpp"
#include "ttss_nits_shamir.hpp"

namespace {
void write_json(const std::string& path, const nlohmann::json& j) {
  std::ofstream f(path);
  f << j.dump(2) << "\n";
}
}  // namespace

int main(int argc, char** argv) {
  using namespace didzk;

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <outdir>\n";
    return 1;
  }
  const std::string outdir = argv[1];

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

  const ShareEnvelope& env = setup.shareEnvelopes[0];

  write_json(outdir + "/share_envelope_0.json", share_envelope_to_njson(env));
  write_json(outdir + "/set_share_envelope_req.json",
             {{"token", "demo-token"}, {"shareEnvelope", share_envelope_to_njson(env)}});
  write_json(outdir + "/req_recover.json",
             {{"token", "demo-token"},
              {"idHash", env.idHash},
              {"ver", env.ver},
              {"epoch", env.epoch},
              {"guardianIndex", env.guardianIndex},
              {"nonce", "recover_req_001"},
              {"auditToken", "dummy"}});
  write_json(outdir + "/req_trace.json",
             {{"token", "demo-token"},
              {"traceSessionId", "trace_001"},
              {"idHash", env.idHash},
              {"ver", env.ver},
              {"epoch", env.epoch},
              {"guardianIndex", env.guardianIndex},
              {"requestKind", "honest_challenge"}});
  write_json(outdir + "/req_invalidate.json",
             {{"token", "demo-token"},
              {"idHash", env.idHash},
              {"ver", env.ver},
              {"epoch", env.epoch},
              {"reason", "rotate_to_ver_2_epoch_11"}});

  {
    std::ofstream f(outdir + "/fixture.env");
    f << "ID=" << id << "\n";
    f << "ID_HASH=" << env.idHash << "\n";
    f << "VER=" << env.ver << "\n";
    f << "EPOCH=" << env.epoch << "\n";
    f << "GUARDIAN_INDEX=" << env.guardianIndex << "\n";
    f << "GUARDIAN_ID=" << env.guardianId << "\n";
    f << "VK_SET_HASH=" << setup.vkSetHash << "\n";
  }

  std::cout << "fixture_outdir=" << outdir << "\n";
  std::cout << "idHash=" << env.idHash << "\n";
  std::cout << "ver=" << env.ver << " epoch=" << env.epoch
            << " guardianIndex=" << env.guardianIndex << "\n";
  return 0;
}
