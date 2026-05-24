#pragma once

#include "did_app_types.hpp"

#include <cstddef>
#include <string>

void require_sodium();
std::string random_hex32();
std::string hash32_hex_from_bytes(const unsigned char* data, std::size_t len);
std::string hash32_hex_from_text(const std::string& s);
std::string keccak256_hex_from_bytes(const unsigned char* data, std::size_t len);
std::string keccak256_hex_from_text(const std::string& s);
SessionKeyPair gen_ed25519_keypair();
std::string hash_pubkey_to_field_hex(const std::string& pkHex);
