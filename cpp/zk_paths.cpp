#include "zk_paths.hpp"

#include "text_utils.hpp"
#include "zk_backend_internal.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace didzk {
namespace {

namespace fs = std::filesystem;

std::string first_existing_path(const std::vector<fs::path>& candidates) {
  for (const auto& p : candidates) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
      return fs::weakly_canonical(p, ec).string();
    }
  }
  return "";
}

std::vector<fs::path> witness_binary_candidates(const fs::path& root) {
  return {
      root / "zk_build" / "auth_membership" / "auth_membership_cpp" / "auth_membership",
      root / "build" / "circuits" / "auth_membership_cpp" / "auth_membership",
  };
}

std::vector<fs::path> witness_wasm_candidates(const fs::path& root) {
  return {
      root / "zk_build" / "auth_membership" / "auth_membership_js" / "auth_membership.wasm",
      root / "build" / "circuits" / "auth_membership_js" / "auth_membership.wasm",
  };
}

std::vector<fs::path> witness_js_candidates(const fs::path& root) {
  return {
      root / "zk_build" / "auth_membership" / "auth_membership_js" / "generate_witness.js",
      root / "build" / "circuits" / "auth_membership_js" / "generate_witness.js",
  };
}

std::vector<fs::path> zkey_candidates(const fs::path& root) {
  return {
      root / "zk_build" / "zkey" / "auth_membership_final.zkey",
      root / "zk_build" / "auth_membership" / "auth_membership_final.zkey",
      root / "setup" / "zkey" / "auth_membership_final.zkey",
      root / "zk_build" / "auth_membership" / "final.zkey",
      root / "build" / "circuits" / "auth_membership_final.zkey",
  };
}

std::vector<fs::path> vk_candidates(const fs::path& root) {
  return {
      root / "zk_build" / "vk" / "auth_membership_vk.json",
      root / "zk_build" / "vk" / "verification_key.json",
      root / "setup" / "vk" / "auth_membership_vk.json",
      root / "zk_build" / "auth_membership" / "verification_key.json",
      root / "zk_build" / "auth_membership" / "auth_membership_vkey.json",
      root / "build" / "circuits" / "auth_membership_vk.json",
  };
}

}  // namespace

ZkBackendPaths resolve_zk_backend_paths(const std::string& projectRoot) {
  ZkBackendPaths paths;
  paths.projectRoot = detect_project_root(projectRoot);
  fill_default_backend_paths(paths);
  return paths;
}

void fill_default_backend_paths(ZkBackendPaths& paths) {
  if (trim_copy(paths.projectRoot).empty()) {
    paths.projectRoot = detect_project_root();
  }

  const fs::path root(paths.projectRoot);

  if (trim_copy(paths.authWasmOrCpp).empty()) {
    const std::string cppBin = first_existing_path(witness_binary_candidates(root));
    if (!cppBin.empty()) {
      paths.authWasmOrCpp = cppBin;
    } else {
      paths.authWasmOrCpp = first_existing_path(witness_wasm_candidates(root));
    }
  }

  if (trim_copy(paths.witnessBin).empty()) {
    if (!trim_copy(paths.authWasmOrCpp).empty() &&
        !ends_with(trim_copy(paths.authWasmOrCpp), ".wasm")) {
      paths.witnessBin = trim_copy(paths.authWasmOrCpp);
    } else {
      paths.witnessBin = first_existing_path(witness_js_candidates(root));
    }
  }

  if (trim_copy(paths.zkeyPath).empty()) {
    paths.zkeyPath = first_existing_path(zkey_candidates(root));
  }

  if (trim_copy(paths.vkPath).empty()) {
    paths.vkPath = first_existing_path(vk_candidates(root));
  }

  if (trim_copy(paths.rapidsnarkBin).empty()) {
    if (const char* envRapidsnark = std::getenv("RAPIDSNARK_BIN")) {
      paths.rapidsnarkBin = trim_copy(envRapidsnark);
    } else {
      const std::string local = first_existing_path({
          root / "bin" / "rapidsnark",
          root / "rapidsnark",
          root / ".." / "rapidsnark" / "build" / "prover",
      });
      paths.rapidsnarkBin = local.empty() ? "rapidsnark" : local;
    }
  }

  if (trim_copy(paths.snarkjsBin).empty()) {
    if (const char* envSnarkjs = std::getenv("SNARKJS_BIN")) {
      paths.snarkjsBin = trim_copy(envSnarkjs);
    } else {
      paths.snarkjsBin = "snarkjs";
    }
  }
}

}  // namespace didzk
