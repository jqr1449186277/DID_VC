// cpp/zk_backend.hpp
#pragma once

#include "merkle_poseidon.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace didzk {

struct SessionContextZK {
  std::string ctxHash;
  std::string sessPk;
  std::string sessPkHash;
  std::uint64_t epoch{0};
};

struct ZkWitnessInput {
  std::string root;
  std::string sid;
  std::string rho;
  std::string pkNormHash;
  std::string pkRecHash;
  std::uint64_t ver{0};
  std::vector<std::string> pathElements;
  std::vector<std::uint8_t> pathIndex;
  std::string ctxHash;
  std::string sessPkHash;
  std::uint64_t epoch{0};
};

struct ZkProofBundle {
  std::string proofJsonPath;
  std::string publicJsonPath;
  std::vector<std::string> publicSignals;
  std::string sessionSigHex;
  double witnessMs{0.0};
  double proveMs{0.0};
  double verifyMs{0.0};
  std::size_t proofBytes{0};
  std::size_t publicBytes{0};
};

struct ZkBackendPaths {
  std::string projectRoot;
  std::string witnessBin;
  std::string authWasmOrCpp;
  std::string zkeyPath;
  std::string vkPath;
  std::string rapidsnarkBin;
  std::string snarkjsBin;
};

struct ZkRunArtifacts {
  std::string inputJsonPath;
  std::string witnessPath;
  std::string proofPath;
  std::string publicPath;
};

struct ZkBackendOptions {
  std::string projectRoot;
  std::string workDir;
  bool preferRapidsnark{true};
  bool keepIntermediateFiles{true};
};

struct VerifyResult {
  bool ok{false};
  double verifyMs{0.0};
  std::string stdoutText;
};

struct PublicSignalView {
  std::string root;
  std::string ctxHash;
  std::string sessPkHash;
  std::string epoch;
  std::string nullifier;
  std::vector<std::string> raw;
};

// ---------- input export ----------

// Convert decimal / hex field input into circuit-friendly decimal string.
std::string to_circuit_dec_string(const std::string& value);

// Convert witness input to JSON object with all field elements serialized as
// decimal strings, matching auth_membership.circom expectations.
nlohmann::json to_json_obj(const ZkWitnessInput& in);

// Write witness input JSON to disk.
void export_zk_input_json(const ZkWitnessInput& in, const std::string& outPath);

// Render witness input JSON to pretty text for logging / debugging.
std::string render_zk_input_json_text(const ZkWitnessInput& in);

// ---------- witness input building ----------

// Build a witness input object from identity + path + session context.
ZkWitnessInput build_zk_witness_input(const IdentityStateZK& st,
                                      const MerklePathZK& path,
                                      const SessionContextZK& sess,
                                      const std::string& projectRoot = "");

// ---------- path / binary discovery ----------

// Resolve standard artifact paths relative to project root.
ZkBackendPaths resolve_zk_backend_paths(const std::string& projectRoot = "");

// Fill unset binary paths from common defaults.
void fill_default_backend_paths(ZkBackendPaths& paths);

// ---------- proof generation / verification ----------

// Run witness generation using auth_membership witness calculator or wasm path.
double run_witness_calculator(const ZkBackendPaths& paths,
                              const std::string& inputJsonPath,
                              const std::string& witnessOutPath);

// Generate proof using rapidsnark if configured, otherwise snarkjs.
double run_groth16_prove(const ZkBackendPaths& paths,
                         const std::string& witnessPath,
                         const std::string& proofOutPath,
                         const std::string& publicOutPath);

// Verify Groth16 proof with snarkjs.
VerifyResult run_groth16_verify(const ZkBackendPaths& paths,
                                const std::string& proofPath,
                                const std::string& publicPath);

// Read public.json into raw ordered vector.
std::vector<std::string> load_public_signals(const std::string& publicJsonPath);

// Interpret ordered public signals into named fields.
// Current expected order for AuthMembership is:
//   [root, ctxHash, sessPkHash, epoch, nullifier, ...optional extras]
PublicSignalView parse_public_signals(const std::vector<std::string>& signals);

// File size helper for CSV metrics.
std::size_t file_size_bytes(const std::string& path);

// ---------- high-level E2E wrapper ----------

// Full local prove flow: export input -> witness -> prove -> optional verify.
ZkProofBundle session_gen_zk(const ZkBackendPaths& paths,
                             const ZkWitnessInput& input,
                             const ZkRunArtifacts& out,
                             bool runVerifyAfterProve = true);

// Verify an already-generated bundle.
bool session_verify_zk(const ZkBackendPaths& paths,
                       const ZkProofBundle& bundle);

// ---------- utility helpers ----------

// Build default output artifact paths inside a working directory.
ZkRunArtifacts make_default_run_artifacts(const std::string& workDir);

// Validate path vectors and basic witness-input shape.
void validate_zk_witness_input(const ZkWitnessInput& in);

}  // namespace didzk
