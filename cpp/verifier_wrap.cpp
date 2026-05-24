#include "verifier_wrap.hpp"
#include "hex_utils.hpp"
#include "process_utils.hpp"
#include "text_utils.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace didzk {
namespace {

void require_sodium_init() {
  static bool ready = false;
  if (!ready) {
    if (sodium_init() < 0) {
      throw std::runtime_error("sodium_init_failed");
    }
    ready = true;
  }
}

const char* kFieldBatchBridge = R"JS(
const [, mode, payload] = process.argv;
const values = JSON.parse(payload);
if (!Array.isArray(values)) {
  throw new Error("payload_not_array");
}
const BN254_FR = BigInt("21888242871839275222246405745257275088548364400416034343698204186575808495617");

function normalizeFr(v) {
  let x = BigInt(v);
  x %= BN254_FR;
  if (x < 0n) x += BN254_FR;
  return x;
}

function toHex32(v) {
  let h = normalizeFr(v).toString(16);
  while (h.length < 64) h = "0" + h;
  if (h.length > 64) throw new Error("field_overflow_256");
  return "0x" + h;
}

switch (mode) {
  case "normalize_many":
    console.log(JSON.stringify(values.map((v) => toHex32(v))));
    break;
  case "dec_many":
    console.log(JSON.stringify(values.map((v) => normalizeFr(v).toString(10))));
    break;
  default:
    throw new Error(`unknown_mode:${mode}`);
}
)JS";

std::vector<std::string> run_field_batch_bridge(const std::string& mode,
                                                const std::vector<std::string>& values) {
  if (values.empty()) return {};
  const std::string payload = nlohmann::json(values).dump();
  const std::string out =
      run_command_capture_argv_text_checked({"node", "--input-type=module", "-e", kFieldBatchBridge, mode, payload});
  const nlohmann::json j = nlohmann::json::parse(out);
  if (!j.is_array()) {
    throw std::runtime_error("field_batch_output_not_array");
  }
  std::vector<std::string> result;
  result.reserve(j.size());
  for (const auto& item : j) {
    if (!item.is_string()) {
      throw std::runtime_error("field_batch_output_item_not_string");
    }
    result.push_back(item.get<std::string>());
  }
  return result;
}

std::vector<std::string> normalize_fields_hex_batch(
    const std::vector<std::string>& values,
    const std::string& projectRoot) {
  (void)projectRoot;
  std::vector<std::string> trimmed;
  trimmed.reserve(values.size());
  for (const auto& value : values) {
    const std::string v = trim_copy(value);
    if (v.empty()) {
      throw std::runtime_error("missing_field_value");
    }
    trimmed.push_back(v);
  }
  return run_field_batch_bridge("normalize_many", trimmed);
}

std::vector<std::string> field_hex_to_dec_batch(
    const std::vector<std::string>& normalizedHexValues) {
  return run_field_batch_bridge("dec_many", normalizedHexValues);
}

PublicSignalView parse_public_signals_for_auth(const std::vector<std::string>& signals) {
  PublicSignalView v;
  v.raw = signals;

  if (signals.size() == 5) {
    v.nullifier = signals.at(0);
    v.root = signals.at(1);
    v.ctxHash = signals.at(2);
    v.sessPkHash = signals.at(3);
    v.epoch = signals.at(4);
    return v;
  }

  if (signals.size() == 6) {
    v.nullifier = signals.at(0);
    v.root = signals.at(2);
    v.ctxHash = signals.at(3);
    v.sessPkHash = signals.at(4);
    v.epoch = signals.at(5);
    return v;
  }

  if (signals.size() >= 7) {
    v.nullifier = signals.at(0);
    v.root = signals.at(3);
    v.ctxHash = signals.at(4);
    v.sessPkHash = signals.at(5);
    v.epoch = signals.at(6);
    return v;
  }

  throw std::runtime_error("public_signal_count_too_small");
}

std::string normalize_any_field_to_dec(const std::string& value,
                                       const std::string& projectRoot) {
  const auto hex = normalize_fields_hex_batch({value}, projectRoot);
  const auto dec = field_hex_to_dec_batch(hex);
  return dec.at(0);
}

using VerifyClock = std::chrono::steady_clock;

double verify_duration_ms(const VerifyClock::time_point& start,
                          const VerifyClock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

void log_verify_wrapper_segment(const std::string& stage,
                                double ms,
                                const std::string& detail = "") {
  std::ostringstream oss;
  oss << "[verify-breakdown] stage=" << stage
      << " ms=" << std::fixed << std::setprecision(3) << ms;
  if (!detail.empty()) {
    oss << " " << detail;
  }
  std::cerr << oss.str() << std::endl;
}

struct NormalizedAuthSignals {
  PublicSignalView pub;
  std::vector<std::string> normalizedSignalsHex;
  std::vector<std::string> normalizedSignalsDec;
  bool decCacheReady = false;
  std::string rootHex;
  std::string ctxHashHex;
  std::string sessPkHashHex;
  std::string epochHex;
  std::string nullifierHex;
  std::string rootDec;
  std::string ctxHashDec;
  std::string sessPkHashDec;
  std::string epochDec;
  std::string nullifierDec;
};

bool env_truthy(const char* name) {
  const char* v = std::getenv(name);
  if (!v) return false;
  const std::string s = trim_copy(v);
  return s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "YES";
}

std::string field_hex_to_dec(const std::string& normalizedHex) {
  const auto dec = field_hex_to_dec_batch({normalizedHex});
  return dec.at(0);
}

NormalizedAuthSignals normalize_public_signals_for_auth(
    const std::vector<std::string>& signals,
    const std::string& projectRoot) {
  NormalizedAuthSignals out;
  out.pub = parse_public_signals_for_auth(signals);
  out.normalizedSignalsHex = normalize_fields_hex_batch(signals, projectRoot);

  if (signals.size() == 5) {
    out.nullifierHex = out.normalizedSignalsHex.at(0);
    out.rootHex = out.normalizedSignalsHex.at(1);
    out.ctxHashHex = out.normalizedSignalsHex.at(2);
    out.sessPkHashHex = out.normalizedSignalsHex.at(3);
    out.epochHex = out.normalizedSignalsHex.at(4);
  } else if (signals.size() == 6) {
    out.nullifierHex = out.normalizedSignalsHex.at(0);
    out.rootHex = out.normalizedSignalsHex.at(2);
    out.ctxHashHex = out.normalizedSignalsHex.at(3);
    out.sessPkHashHex = out.normalizedSignalsHex.at(4);
    out.epochHex = out.normalizedSignalsHex.at(5);
  } else {
    out.nullifierHex = out.normalizedSignalsHex.at(0);
    out.rootHex = out.normalizedSignalsHex.at(3);
    out.ctxHashHex = out.normalizedSignalsHex.at(4);
    out.sessPkHashHex = out.normalizedSignalsHex.at(5);
    out.epochHex = out.normalizedSignalsHex.at(6);
  }

  return out;
}

const std::vector<std::string>& ensure_public_signals_dec_cache(
    NormalizedAuthSignals* norm) {
  if (!norm) throw std::runtime_error("null_normalized_signal_cache");
  if (!norm->decCacheReady) {
    norm->normalizedSignalsDec = field_hex_to_dec_batch(norm->normalizedSignalsHex);
    if (norm->normalizedSignalsDec.size() == 5) {
      norm->nullifierDec = norm->normalizedSignalsDec.at(0);
      norm->rootDec = norm->normalizedSignalsDec.at(1);
      norm->ctxHashDec = norm->normalizedSignalsDec.at(2);
      norm->sessPkHashDec = norm->normalizedSignalsDec.at(3);
      norm->epochDec = norm->normalizedSignalsDec.at(4);
    } else if (norm->normalizedSignalsDec.size() == 6) {
      norm->nullifierDec = norm->normalizedSignalsDec.at(0);
      norm->rootDec = norm->normalizedSignalsDec.at(2);
      norm->ctxHashDec = norm->normalizedSignalsDec.at(3);
      norm->sessPkHashDec = norm->normalizedSignalsDec.at(4);
      norm->epochDec = norm->normalizedSignalsDec.at(5);
    } else if (norm->normalizedSignalsDec.size() >= 7) {
      norm->nullifierDec = norm->normalizedSignalsDec.at(0);
      norm->rootDec = norm->normalizedSignalsDec.at(3);
      norm->ctxHashDec = norm->normalizedSignalsDec.at(4);
      norm->sessPkHashDec = norm->normalizedSignalsDec.at(5);
      norm->epochDec = norm->normalizedSignalsDec.at(6);
    }
    norm->decCacheReady = true;
  }
  return norm->normalizedSignalsDec;
}

std::string build_session_transcript_from_normalized(const std::string& ctxHashDec,
                                                     const std::vector<std::string>& normalizedSignals,
                                                     std::uint64_t epoch) {
  return nlohmann::json{
      {"ctxHash", ctxHashDec},
      {"epoch", std::to_string(epoch)},
      {"publicSignals", normalizedSignals},
  }.dump();
}

}  // namespace

std::string build_session_transcript(const std::string& ctxHash,
                                     const std::vector<std::string>& publicSignals,
                                     std::uint64_t epoch) {
  const std::string projectRoot = didzk::detect_project_root();
  NormalizedAuthSignals norm =
      normalize_public_signals_for_auth(publicSignals, projectRoot);
  const auto& normalizedSignalsDec = ensure_public_signals_dec_cache(&norm);
  const std::string ctxHashDec = trim_copy(ctxHash).empty()
      ? field_hex_to_dec(norm.ctxHashHex)
      : normalize_any_field_to_dec(ctxHash, projectRoot);
  return build_session_transcript_from_normalized(ctxHashDec, normalizedSignalsDec, epoch);
}

std::string sign_session_transcript_hex(const std::string& sessionSkHex,
                                        const std::string& transcript) {
  require_sodium_init();
  const std::vector<unsigned char> sk = hex_to_bytes(sessionSkHex);
  if (sk.size() != crypto_sign_SECRETKEYBYTES) {
    throw std::runtime_error("session_secret_key_must_be_64_bytes_hex");
  }

  std::array<unsigned char, crypto_sign_BYTES> sig{};
  unsigned long long siglen = 0;
  if (crypto_sign_detached(sig.data(), &siglen,
                           reinterpret_cast<const unsigned char*>(transcript.data()),
                           static_cast<unsigned long long>(transcript.size()),
                           sk.data()) != 0) {
    throw std::runtime_error("crypto_sign_detached_failed");
  }
  return bytes_to_hex(sig.data(), static_cast<std::size_t>(siglen), true);
}

bool verify_session_signature_hex(const std::string& sessionPkHex,
                                  const std::string& transcript,
                                  const std::string& sigHex) {
  require_sodium_init();
  const std::vector<unsigned char> pk = hex_to_bytes(sessionPkHex);
  const std::vector<unsigned char> sig = hex_to_bytes(sigHex);
  if (pk.size() != crypto_sign_PUBLICKEYBYTES) {
    throw std::runtime_error("session_public_key_must_be_32_bytes_hex");
  }
  if (sig.size() != crypto_sign_BYTES) {
    throw std::runtime_error("session_signature_must_be_64_bytes_hex");
  }

  return crypto_sign_verify_detached(
             sig.data(),
             reinterpret_cast<const unsigned char*>(transcript.data()),
             static_cast<unsigned long long>(transcript.size()),
             pk.data()) == 0;
}

bool public_root_matches_current_root(const std::vector<std::string>& publicSignals,
                                      const std::string& currentRoot,
                                      const std::string& projectRoot) {
  const NormalizedAuthSignals norm =
      normalize_public_signals_for_auth(publicSignals, projectRoot);
  const std::string currentRootHex = normalize_field_hex(trim_copy(currentRoot), projectRoot);
  return norm.rootHex == currentRootHex;
}

bool verify_bundle_with_session(const ZkBackendPaths& rawPaths,
                                const ZkProofBundle& bundle,
                                const std::string& currentRoot,
                                const std::string& sessionPkHex,
                                const std::string& expectedCtxHash,
                                std::uint64_t expectedEpoch,
                                std::string* err) {
  const auto verifyTotalStart = VerifyClock::now();
  try {
    ZkBackendPaths paths = rawPaths;
    fill_default_backend_paths(paths);

    std::vector<std::string> signals = bundle.publicSignals;
    if (signals.empty()) {
      signals = load_public_signals(bundle.publicJsonPath);
    }

    const auto normalizeHexStart = VerifyClock::now();
    NormalizedAuthSignals norm =
        normalize_public_signals_for_auth(signals, paths.projectRoot);
    log_verify_wrapper_segment(
        "normalize_public_signals_cache_hex",
        verify_duration_ms(normalizeHexStart, VerifyClock::now()),
        "signalCount=" + std::to_string(signals.size()) + " mode=batch");

    const auto bindingStart = VerifyClock::now();
    std::vector<std::string> bindingInputs;
    bindingInputs.push_back(trim_copy(currentRoot));
    const bool hasExpectedCtx = !trim_copy(expectedCtxHash).empty();
    const bool hasExpectedEpoch = expectedEpoch != 0;
    if (hasExpectedCtx) bindingInputs.push_back(trim_copy(expectedCtxHash));
    if (hasExpectedEpoch) bindingInputs.push_back(std::to_string(expectedEpoch));
    const std::vector<std::string> bindingHex =
        normalize_fields_hex_batch(bindingInputs, paths.projectRoot);
    std::size_t bindingIndex = 0;
    const std::string currentRootHex = bindingHex.at(bindingIndex++);
    const std::string expectedCtxHashHex = hasExpectedCtx ? bindingHex.at(bindingIndex++) : std::string();
    const std::string expectedEpochHex = hasExpectedEpoch ? bindingHex.at(bindingIndex++) : std::string();
    log_verify_wrapper_segment(
        "normalize_binding_inputs_cache",
        verify_duration_ms(bindingStart, VerifyClock::now()),
        "hasExpectedCtx=" + std::string(expectedCtxHashHex.empty() ? "0" : "1") +
        " hasExpectedEpoch=" + std::string(expectedEpochHex.empty() ? "0" : "1") +
        " mode=batch");

    const auto parseStart = VerifyClock::now();
    if (norm.rootHex != currentRootHex) {
      if (err) {
        std::ostringstream oss;
        oss << "public_root_mismatch: publicRootHex=" << norm.rootHex
            << ", currentRootHex=" << currentRootHex
            << ", signalCount=" << signals.size();
        *err = oss.str();
      }
      log_verify_wrapper_segment(
          "public_and_binding_checks",
          verify_duration_ms(parseStart, VerifyClock::now()),
          "result=fail reason=public_root_mismatch signalCount=" + std::to_string(signals.size()));
      return false;
    }

    if (!expectedCtxHashHex.empty() && expectedCtxHashHex != norm.ctxHashHex) {
      if (err) {
        std::ostringstream oss;
        oss << "ctxHash_mismatch: wantHex=" << expectedCtxHashHex
            << ", gotHex=" << norm.ctxHashHex;
        *err = oss.str();
      }
      log_verify_wrapper_segment(
          "public_and_binding_checks",
          verify_duration_ms(parseStart, VerifyClock::now()),
          "result=fail reason=ctxHash_mismatch signalCount=" + std::to_string(signals.size()));
      return false;
    }

    if (!expectedEpochHex.empty() && expectedEpochHex != norm.epochHex) {
      if (err) {
        std::ostringstream oss;
        oss << "epoch_mismatch: wantHex=" << expectedEpochHex
            << ", gotHex=" << norm.epochHex;
        *err = oss.str();
      }
      log_verify_wrapper_segment(
          "public_and_binding_checks",
          verify_duration_ms(parseStart, VerifyClock::now()),
          "result=fail reason=epoch_mismatch signalCount=" + std::to_string(signals.size()));
      return false;
    }
    log_verify_wrapper_segment(
        "public_and_binding_checks",
        verify_duration_ms(parseStart, VerifyClock::now()),
        "result=ok signalCount=" + std::to_string(signals.size()));

    const bool rerunGroth16 = env_truthy("DIDZK_VERIFY_BUNDLE_RUN_GROTH16");
    if (rerunGroth16) {
      const auto grothStart = VerifyClock::now();
      const VerifyResult vr = run_groth16_verify(paths, bundle.proofJsonPath, bundle.publicJsonPath);
      log_verify_wrapper_segment(
          "groth16_verify_result",
          verify_duration_ms(grothStart, VerifyClock::now()),
          std::string("ok=") + (vr.ok ? "1" : "0") + " mode=rerun_external");
      if (!vr.ok) {
        if (err) *err = "groth16_verify_failed: " + vr.stdoutText;
        return false;
      }
    } else {
      log_verify_wrapper_segment(
          "groth16_verify_result",
          0.0,
          "ok=1 mode=skipped_external");
    }

    if (trim_copy(bundle.sessionSigHex).empty()) {
      if (err) *err = "missing_sessionSigHex";
      return false;
    }

    const auto transcriptNormStart = VerifyClock::now();
    const auto& normalizedSignalsDec = ensure_public_signals_dec_cache(&norm);
    const std::string& ctxHashDec = norm.ctxHashDec;
    const std::string& epochDec = norm.epochDec;
    log_verify_wrapper_segment(
        "normalize_public_signals_transcript_cache",
        verify_duration_ms(transcriptNormStart, VerifyClock::now()),
        "signalCount=" + std::to_string(normalizedSignalsDec.size()) + " mode=batch");

    const auto transcriptStart = VerifyClock::now();
    const std::uint64_t epochForTranscript =
        static_cast<std::uint64_t>(std::stoull(epochDec));
    const std::string transcript =
        build_session_transcript_from_normalized(ctxHashDec,
                                                 normalizedSignalsDec,
                                                 epochForTranscript);
    log_verify_wrapper_segment(
        "build_session_transcript",
        verify_duration_ms(transcriptStart, VerifyClock::now()),
        "bytes=" + std::to_string(transcript.size()));

    const auto sigStart = VerifyClock::now();
    const bool sigOk = verify_session_signature_hex(sessionPkHex, transcript, bundle.sessionSigHex);
    log_verify_wrapper_segment(
        "session_signature_verify",
        verify_duration_ms(sigStart, VerifyClock::now()),
        std::string("ok=") + (sigOk ? "1" : "0"));
    if (!sigOk) {
      if (err) *err = "session_signature_invalid";
      return false;
    }

    log_verify_wrapper_segment(
        "verify_bundle_total",
        verify_duration_ms(verifyTotalStart, VerifyClock::now()),
        "result=ok");
    return true;
  } catch (const std::exception& e) {
    log_verify_wrapper_segment(
        "verify_bundle_total",
        verify_duration_ms(verifyTotalStart, VerifyClock::now()),
        std::string("result=exception error=") + e.what());
    if (err) *err = e.what();
    return false;
  }
}

}  // namespace didzk
