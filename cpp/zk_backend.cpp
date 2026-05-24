#include "zk_backend.hpp"

#include "normalize_utils.hpp"
#include "text_utils.hpp"
#include "zk_backend_internal.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace didzk {
namespace fs = std::filesystem;

ZkWitnessInput build_zk_witness_input(const IdentityStateZK& st,
                                      const MerklePathZK& path,
                                      const SessionContextZK& sess,
                                      const std::string& projectRoot) {
  if (trim_copy(st.sid).empty()) throw std::runtime_error("missing_field: sid");
  if (trim_copy(st.rho).empty()) throw std::runtime_error("missing_field: rho");
  if (trim_copy(st.pkNormHash).empty()) throw std::runtime_error("missing_field: pkNormHash");
  if (trim_copy(st.pkRecHash).empty()) throw std::runtime_error("missing_field: pkRecHash");
  if (trim_copy(path.root).empty()) throw std::runtime_error("missing_field: root");
  if (trim_copy(sess.ctxHash).empty()) throw std::runtime_error("missing_field: ctxHash");
  if (trim_copy(sess.sessPkHash).empty()) throw std::runtime_error("missing_field: sessPkHash");

  if (path.pathElements.size() != path.pathIndex.size()) {
    throw std::runtime_error("path_length_mismatch");
  }
  for (std::size_t i = 0; i < path.pathIndex.size(); ++i) {
    if (path.pathIndex[i] != 0 && path.pathIndex[i] != 1) {
      throw std::runtime_error("bad_path_index_at_" + std::to_string(i));
    }
  }

  const std::string rootHex = require_normalized_field_hex(path.root, projectRoot, "root");
  const std::string leafHex = compute_leaf_from_identity(st, projectRoot);

  MerklePathZK normalizedPath = path;
  normalizedPath.root = rootHex;
  for (std::size_t i = 0; i < normalizedPath.pathElements.size(); ++i) {
    normalizedPath.pathElements[i] =
        require_normalized_field_hex(normalizedPath.pathElements[i], projectRoot,
                                     "pathElements[" + std::to_string(i) + "]");
  }

  if (!verify_merkle_path_local(leafHex, normalizedPath, projectRoot)) {
    throw std::runtime_error("path_root_mismatch");
  }

  ZkWitnessInput in;
  in.root = rootHex;
  in.sid = trim_copy(st.sid);
  in.rho = trim_copy(st.rho);
  in.pkNormHash = require_normalized_field_hex(st.pkNormHash, projectRoot, "pkNormHash");
  in.pkRecHash = require_normalized_field_hex(st.pkRecHash, projectRoot, "pkRecHash");
  in.ver = st.ver;
  in.pathElements = normalizedPath.pathElements;
  in.pathIndex = normalizedPath.pathIndex;
  in.ctxHash = require_normalized_field_hex(sess.ctxHash, projectRoot, "ctxHash");
  in.sessPkHash = require_normalized_field_hex(sess.sessPkHash, projectRoot, "sessPkHash");
  in.epoch = sess.epoch;
  return in;
}

ZkProofBundle session_gen_zk(const ZkBackendPaths& rawPaths,
                             const ZkWitnessInput& input,
                             const ZkRunArtifacts& out,
                             bool runVerifyAfterProve) {
  ZkBackendPaths paths = rawPaths;
  fill_default_backend_paths(paths);
  validate_zk_witness_input(input);

  if (trim_copy(out.inputJsonPath).empty()) {
    throw std::runtime_error("missing_output_path: inputJsonPath");
  }
  if (trim_copy(out.witnessPath).empty()) {
    throw std::runtime_error("missing_output_path: witnessPath");
  }
  if (trim_copy(out.proofPath).empty()) {
    throw std::runtime_error("missing_output_path: proofPath");
  }
  if (trim_copy(out.publicPath).empty()) {
    throw std::runtime_error("missing_output_path: publicPath");
  }

  export_zk_input_json(input, out.inputJsonPath);

  ZkProofBundle bundle;
  bundle.proofJsonPath = out.proofPath;
  bundle.publicJsonPath = out.publicPath;
  bundle.witnessMs = run_witness_calculator(paths, out.inputJsonPath, out.witnessPath);
  bundle.proveMs = run_groth16_prove(paths, out.witnessPath, out.proofPath, out.publicPath);
  bundle.publicSignals = load_public_signals(out.publicPath);
  bundle.proofBytes = file_size_bytes(out.proofPath);
  bundle.publicBytes = file_size_bytes(out.publicPath);

  if (runVerifyAfterProve) {
    const VerifyResult vr = run_groth16_verify(paths, out.proofPath, out.publicPath);
    bundle.verifyMs = vr.verifyMs;
    if (!vr.ok) {
      throw std::runtime_error("groth16_verify_failed: " + vr.stdoutText);
    }
  }

  return bundle;
}

bool session_verify_zk(const ZkBackendPaths& rawPaths, const ZkProofBundle& bundle) {
  ZkBackendPaths paths = rawPaths;
  fill_default_backend_paths(paths);

  if (trim_copy(bundle.proofJsonPath).empty()) {
    throw std::runtime_error("missing_proofJsonPath");
  }
  if (trim_copy(bundle.publicJsonPath).empty()) {
    throw std::runtime_error("missing_publicJsonPath");
  }

  const VerifyResult vr = run_groth16_verify(paths, bundle.proofJsonPath, bundle.publicJsonPath);
  return vr.ok;
}

ZkRunArtifacts make_default_run_artifacts(const std::string& workDir) {
  const fs::path dir(workDir);
  if (trim_copy(workDir).empty()) {
    throw std::runtime_error("missing_workDir");
  }

  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    throw std::runtime_error("create_workdir_failed: " + dir.string());
  }

  ZkRunArtifacts out;
  out.inputJsonPath = (dir / "input.json").string();
  out.witnessPath = (dir / "witness.wtns").string();
  out.proofPath = (dir / "proof.json").string();
  out.publicPath = (dir / "public.json").string();
  return out;
}

void validate_zk_witness_input(const ZkWitnessInput& in) {
  if (trim_copy(in.root).empty()) throw std::runtime_error("missing_field: root");
  if (trim_copy(in.sid).empty()) throw std::runtime_error("missing_field: sid");
  if (trim_copy(in.rho).empty()) throw std::runtime_error("missing_field: rho");
  if (trim_copy(in.pkNormHash).empty()) throw std::runtime_error("missing_field: pkNormHash");
  if (trim_copy(in.pkRecHash).empty()) throw std::runtime_error("missing_field: pkRecHash");
  if (trim_copy(in.ctxHash).empty()) throw std::runtime_error("missing_field: ctxHash");
  if (trim_copy(in.sessPkHash).empty()) throw std::runtime_error("missing_field: sessPkHash");

  if (in.pathElements.size() != in.pathIndex.size()) {
    throw std::runtime_error("path_length_mismatch");
  }
  for (std::size_t i = 0; i < in.pathIndex.size(); ++i) {
    if (in.pathIndex[i] != 0 && in.pathIndex[i] != 1) {
      throw std::runtime_error("bad_path_index_at_" + std::to_string(i));
    }
  }
}

}  // namespace didzk
