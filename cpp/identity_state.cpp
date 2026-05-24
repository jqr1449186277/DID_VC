#include "identity_state.hpp"

#include "did_app_common.hpp"

IdentityLocalKeys gen_identity_local_keys() {
  IdentityLocalKeys out;
  out.normalEd25519 = gen_ed25519_keypair();
  out.recoveryEd25519 = gen_ed25519_keypair();
  out.boardSeeds.ownerSeedHex = random_hex32();
  out.boardSeeds.recoverySeedHex = random_hex32();
  return out;
}

// Adaptation note: the current bb_service_zk implementation returns version=0 on registration,
// so the experimental skeleton starts from ver=0 to match completed code paths, even though
// the abstract system model uses ver=1.
IdentityStateZK gen_identity_state(const std::string& id,
                                   const IdentityLocalKeys& keys,
                                   const std::string& projectRoot) {
  IdentityStateZK st;
  st.id = id;
  st.sid = random_hex32();
  st.rho = random_hex32();
  st.cid = didzk::poseidon_cid(st.sid, st.rho, projectRoot);
  st.pkNormHash = hash_pubkey_to_field_hex(keys.normalEd25519.pkHex);
  st.pkRecHash = hash_pubkey_to_field_hex(keys.recoveryEd25519.pkHex);
  st.ver = 0;
  st.active = true;
  return st;
}

IdentityStateZK rotate_identity_state(const IdentityStateZK& oldState,
                                      const IdentityLocalKeys& newKeys,
                                      const std::string& projectRoot) {
  IdentityStateZK st;
  st.id = oldState.id;
  st.sid = random_hex32();
  st.rho = random_hex32();
  st.cid = didzk::poseidon_cid(st.sid, st.rho, projectRoot);
  st.pkNormHash = hash_pubkey_to_field_hex(newKeys.normalEd25519.pkHex);
  st.pkRecHash = hash_pubkey_to_field_hex(newKeys.recoveryEd25519.pkHex);
  st.ver = oldState.ver + 1;
  st.active = true;
  return st;
}

SessionContextZK build_session_context(const std::string& id,
                                       std::uint64_t epoch,
                                       const SessionKeyPair& sessionKeys,
                                       const std::string& projectRoot) {
  SessionContextZK sess;
  const std::string ctxMaterial = id + "|" + std::to_string(epoch) + "|" + now_stamp_compact();
  const std::string ctxFieldHex =
      didzk::normalize_field_hex(hash32_hex_from_text(ctxMaterial), projectRoot);
  const std::string sessPkFieldHex =
      didzk::normalize_field_hex(hash_pubkey_to_field_hex(sessionKeys.pkHex), projectRoot);

  // Store canonical decimal field elements, matching the circuit/public.json representation.
  sess.ctxHash = didzk::to_circuit_dec_string(ctxFieldHex);
  sess.sessPk = sessionKeys.pkHex;
  sess.sessPkHash = didzk::to_circuit_dec_string(sessPkFieldHex);
  sess.epoch = epoch;
  return sess;
}
