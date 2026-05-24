#include "zk_runner.hpp"

#include "http_transport.hpp"
#include "process_utils.hpp"
#include "text_utils.hpp"
#include "zk_backend_internal.hpp"

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace didzk {
namespace {

using Clock = zk_backend_internal::Clock;

std::string get_verify_service_url() {
  const char* env = std::getenv("DIDZK_VERIFY_SERVICE_URL");
  if (env && *env) return trim_copy(std::string(env));
  return "http://127.0.0.1:3400/verify";
}

int get_verify_service_timeout_ms() {
  const char* env = std::getenv("DIDZK_VERIFY_SERVICE_TIMEOUT_MS");
  if (!env || !*env) return 30000;
  try {
    const int parsed = std::stoi(trim_copy(std::string(env)));
    return parsed > 0 ? parsed : 30000;
  } catch (...) {
    return 30000;
  }
}

bool allow_verify_cli_fallback() {
  const char* env = std::getenv("DIDZK_VERIFY_ALLOW_CLI_FALLBACK");
  if (!env || !*env) return true;
  const std::string v = trim_copy(std::string(env));
  return !(v == "0" || v == "false" || v == "FALSE");
}

std::string require_snarkjs_bin(const ZkBackendPaths& paths) {
  if (!trim_copy(paths.snarkjsBin).empty()) return trim_copy(paths.snarkjsBin);
  return "snarkjs";
}

bool can_try_rapidsnark(const ZkBackendPaths& paths) {
  const std::string bin = trim_copy(paths.rapidsnarkBin);
  if (bin.empty()) return false;
  if (bin == "rapidsnark") return true;
  return zk_backend_internal::file_exists(bin);
}

VerifyResult run_groth16_verify_via_service(const ZkBackendPaths& paths,
                                            const std::string& proofPath,
                                            const std::string& publicPath) {
  zk_backend_internal::ensure_file_exists(paths.vkPath, "verification_key");
  zk_backend_internal::ensure_file_exists(proofPath, "proof");
  zk_backend_internal::ensure_file_exists(publicPath, "public");

  nlohmann::json req;
  req["vkPath"] = paths.vkPath;
  req["proofPath"] = proofPath;
  req["publicPath"] = publicPath;

  const auto t0 = Clock::now();
  std::string body;
  const bool httpOk = ::http_post_json_url(get_verify_service_url(), req, body, get_verify_service_timeout_ms());
  const auto t1 = Clock::now();

  VerifyResult vr;
  vr.verifyMs = zk_backend_internal::duration_ms(t0, t1);
  if (!httpOk) {
    vr.ok = false;
    vr.stdoutText = "verify_service_http_failed: " + trim_copy(body);
    return vr;
  }

  const nlohmann::json j = nlohmann::json::parse(body);
  vr.ok = j.value("ok", false);
  vr.stdoutText = j.value("stdoutText", std::string{});
  if (vr.stdoutText.empty() && j.contains("error") && j["error"].is_string()) {
    vr.stdoutText = j["error"].get<std::string>();
  }
  return vr;
}

}  // namespace

double run_witness_calculator(const ZkBackendPaths& rawPaths,
                              const std::string& inputJsonPath,
                              const std::string& witnessOutPath) {
  ZkBackendPaths paths = rawPaths;
  fill_default_backend_paths(paths);

  zk_backend_internal::ensure_file_exists(inputJsonPath, "input_json");
  zk_backend_internal::ensure_parent_dir(witnessOutPath);

  const std::string target = trim_copy(paths.authWasmOrCpp);
  if (target.empty()) {
    throw std::runtime_error("missing_authWasmOrCpp");
  }

  std::vector<std::string> command;
  if (ends_with(target, ".wasm")) {
    const std::string genJs = trim_copy(paths.witnessBin);
    if (genJs.empty()) {
      throw std::runtime_error("missing_generate_witness_js");
    }
    zk_backend_internal::ensure_file_exists(genJs, "generate_witness_js");
    zk_backend_internal::ensure_file_exists(target, "auth_wasm");
    command = {"node", genJs, target, inputJsonPath, witnessOutPath};
  } else {
    zk_backend_internal::ensure_file_exists(target, "auth_cpp_witness_bin");
    command = {target, inputJsonPath, witnessOutPath};
  }

  const auto t0 = Clock::now();
  run_command_argv_checked(command);
  const auto t1 = Clock::now();

  zk_backend_internal::ensure_file_exists(witnessOutPath, "witness_output");
  return zk_backend_internal::duration_ms(t0, t1);
}

double run_groth16_prove(const ZkBackendPaths& rawPaths,
                         const std::string& witnessPath,
                         const std::string& proofOutPath,
                         const std::string& publicOutPath) {
  ZkBackendPaths paths = rawPaths;
  fill_default_backend_paths(paths);

  zk_backend_internal::ensure_file_exists(witnessPath, "witness");
  zk_backend_internal::ensure_file_exists(paths.zkeyPath, "zkey");
  zk_backend_internal::ensure_parent_dir(proofOutPath);
  zk_backend_internal::ensure_parent_dir(publicOutPath);

  std::vector<std::string> command;
  if (can_try_rapidsnark(paths)) {
    command = {paths.rapidsnarkBin, paths.zkeyPath, witnessPath, proofOutPath, publicOutPath};
  } else {
    const std::string snarkjs = require_snarkjs_bin(paths);
    command = {snarkjs, "groth16", "prove", paths.zkeyPath, witnessPath, proofOutPath, publicOutPath};
  }

  const auto t0 = Clock::now();
  run_command_argv_checked(command);
  const auto t1 = Clock::now();

  zk_backend_internal::ensure_file_exists(proofOutPath, "proof_output");
  zk_backend_internal::ensure_file_exists(publicOutPath, "public_output");
  return zk_backend_internal::duration_ms(t0, t1);
}

VerifyResult run_groth16_verify(const ZkBackendPaths& rawPaths,
                                const std::string& proofPath,
                                const std::string& publicPath) {
  ZkBackendPaths paths = rawPaths;
  fill_default_backend_paths(paths);

  try {
    return run_groth16_verify_via_service(paths, proofPath, publicPath);
  } catch (const std::exception& serviceErr) {
    if (!allow_verify_cli_fallback()) {
      throw;
    }

    zk_backend_internal::ensure_file_exists(paths.vkPath, "verification_key");
    zk_backend_internal::ensure_file_exists(proofPath, "proof");
    zk_backend_internal::ensure_file_exists(publicPath, "public");

    const std::string snarkjs = require_snarkjs_bin(paths);
    const std::vector<std::string> command = {
        snarkjs, "groth16", "verify", paths.vkPath, publicPath, proofPath};

    const auto t0 = Clock::now();
    const CommandResult res = run_command_capture_argv(command);
    const auto t1 = Clock::now();

    VerifyResult vr;
    vr.verifyMs = zk_backend_internal::duration_ms(t0, t1);
    vr.stdoutText = trim_copy(std::string("fallback_cli: ") + serviceErr.what() +
                              (res.stdoutText.empty() ? "" : std::string(" | cli: ") + res.stdoutText));
    vr.ok = (res.rc == 0);
    return vr;
  }
}

}  // namespace didzk
