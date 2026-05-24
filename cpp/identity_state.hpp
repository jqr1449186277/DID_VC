#pragma once

#include "did_app_types.hpp"

#include <cstdint>
#include <string>

IdentityLocalKeys gen_identity_local_keys();
IdentityStateZK gen_identity_state(const std::string& id,
                                   const IdentityLocalKeys& keys,
                                   const std::string& projectRoot);
IdentityStateZK rotate_identity_state(const IdentityStateZK& oldState,
                                      const IdentityLocalKeys& newKeys,
                                      const std::string& projectRoot);
SessionContextZK build_session_context(const std::string& id,
                                       std::uint64_t epoch,
                                       const SessionKeyPair& sessionKeys,
                                       const std::string& projectRoot);
