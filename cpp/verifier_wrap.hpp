#pragma once

#include "zk_backend.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace didzk {

// Build the canonical transcript bound to the session context.
// The transcript is serialized deterministically from:
//   - ctxHash
//   - epoch
//   - ordered publicSignals
//
// All field elements are normalized through to_circuit_dec_string(...) before
// serialization so signing and verification use identical bytes.
std::string build_session_transcript(const std::string& ctxHash,
                                     const std::vector<std::string>& publicSignals,
                                     std::uint64_t epoch);

// Detached Ed25519 signature helpers for the session transcript.
//
// Requirements:
//   - sessionSkHex: 64-byte Ed25519 secret key in hex (optionally 0x-prefixed)
//   - sessionPkHex: 32-byte Ed25519 public key in hex (optionally 0x-prefixed)
//   - sigHex: 64-byte detached signature in hex (optionally 0x-prefixed)
//
// Throws std::runtime_error on malformed key/signature material.
std::string sign_session_transcript_hex(const std::string& sessionSkHex,
                                        const std::string& transcript);

bool verify_session_signature_hex(const std::string& sessionPkHex,
                                  const std::string& transcript,
                                  const std::string& sigHex);

// Compare the proof bundle's public root against the current anchored root.
// publicSignals are interpreted with parse_public_signals(...), and both roots
// are normalized with normalize_field_hex(...).
bool public_root_matches_current_root(const std::vector<std::string>& publicSignals,
                                      const std::string& currentRoot,
                                      const std::string& projectRoot = "");

// Full verifier wrapper matching the experiment-design SessionVerifyZK flow:
//   1) publicSignals.root must equal the current anchored root
//   2) Groth16 proof must verify
//   3) detached session signature must verify
//
// Parameters:
//   rawPaths         : backend paths; unset fields may be auto-filled
//   bundle           : generated proof/public/session-signature bundle
//   currentRoot      : latest root fetched from /root or chain
//   sessionPkHex     : session public key used for detached signature verify
//   expectedCtxHash  : optional expected ctxHash; pass empty string to skip
//   expectedEpoch    : optional expected epoch; pass 0 to skip
//   err              : optional error string output
//
// Returns true only when all checks pass.
bool verify_bundle_with_session(const ZkBackendPaths& rawPaths,
                                const ZkProofBundle& bundle,
                                const std::string& currentRoot,
                                const std::string& sessionPkHex,
                                const std::string& expectedCtxHash,
                                std::uint64_t expectedEpoch,
                                std::string* err = nullptr);

}  // namespace didzk
