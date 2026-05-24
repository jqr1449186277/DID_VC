#include "app_crypto.hpp"

#include "hex_utils.hpp"

#include <sodium.h>

#include <array>
#include <cstdint>
#include <stdexcept>

void require_sodium() {
  static bool ready = false;
  if (!ready) {
    if (sodium_init() < 0) {
      throw std::runtime_error("sodium_init_failed");
    }
    ready = true;
  }
}

std::string random_hex32() {
  std::array<unsigned char, 32> buf{};
  randombytes_buf(buf.data(), buf.size());
  return didzk::bytes_to_hex(buf.data(), buf.size(), true);
}

std::string hash32_hex_from_bytes(const unsigned char* data, std::size_t len) {
  std::array<unsigned char, 32> out{};
  if (crypto_generichash(out.data(), out.size(), data, len, nullptr, 0) != 0) {
    throw std::runtime_error("crypto_generichash_failed");
  }
  return didzk::bytes_to_hex(out.data(), out.size(), true);
}

std::string hash32_hex_from_text(const std::string& s) {
  return hash32_hex_from_bytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

inline std::uint64_t rotl64(std::uint64_t x, unsigned shift) {
  return (x << shift) | (x >> (64U - shift));
}

void keccakf1600(std::uint64_t st[25]) {
  static constexpr std::uint64_t kRoundConstants[24] = {
      0x0000000000000001ULL, 0x0000000000008082ULL,
      0x800000000000808aULL, 0x8000000080008000ULL,
      0x000000000000808bULL, 0x0000000080000001ULL,
      0x8000000080008081ULL, 0x8000000000008009ULL,
      0x000000000000008aULL, 0x0000000000000088ULL,
      0x0000000080008009ULL, 0x000000008000000aULL,
      0x000000008000808bULL, 0x800000000000008bULL,
      0x8000000000008089ULL, 0x8000000000008003ULL,
      0x8000000000008002ULL, 0x8000000000000080ULL,
      0x000000000000800aULL, 0x800000008000000aULL,
      0x8000000080008081ULL, 0x8000000000008080ULL,
      0x0000000080000001ULL, 0x8000000080008008ULL,
  };
  static constexpr int kRotationOffsets[24] = {
      1,  3,  6, 10, 15, 21, 28, 36, 45, 55, 2,  14,
      27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
  };
  static constexpr int kPiLane[24] = {
      10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
      15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
  };

  std::uint64_t bc[5]{};
  for (int round = 0; round < 24; ++round) {
    for (int i = 0; i < 5; ++i) {
      bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
    }
    for (int i = 0; i < 5; ++i) {
      const std::uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
      for (int j = 0; j < 25; j += 5) {
        st[j + i] ^= t;
      }
    }

    std::uint64_t t = st[1];
    for (int i = 0; i < 24; ++i) {
      const int j = kPiLane[i];
      const std::uint64_t cur = st[j];
      st[j] = rotl64(t, static_cast<unsigned>(kRotationOffsets[i]));
      t = cur;
    }

    for (int j = 0; j < 25; j += 5) {
      for (int i = 0; i < 5; ++i) {
        bc[i] = st[j + i];
      }
      for (int i = 0; i < 5; ++i) {
        st[j + i] = bc[i] ^ ((~bc[(i + 1) % 5]) & bc[(i + 2) % 5]);
      }
    }

    st[0] ^= kRoundConstants[round];
  }
}

std::string keccak256_hex_from_bytes(const unsigned char* data, std::size_t len) {
  constexpr std::size_t kRateBytes = 136;
  std::uint64_t st[25]{};
  std::size_t offset = 0;
  while (offset + kRateBytes <= len) {
    for (std::size_t i = 0; i < kRateBytes; ++i) {
      st[i >> 3] ^= static_cast<std::uint64_t>(data[offset + i]) << (8U * (i & 7U));
    }
    keccakf1600(st);
    offset += kRateBytes;
  }

  const std::size_t rem = len - offset;
  for (std::size_t i = 0; i < rem; ++i) {
    st[i >> 3] ^= static_cast<std::uint64_t>(data[offset + i]) << (8U * (i & 7U));
  }
  st[rem >> 3] ^= static_cast<std::uint64_t>(0x01U) << (8U * (rem & 7U));
  st[(kRateBytes - 1) >> 3] ^= static_cast<std::uint64_t>(0x80U)
                                << (8U * ((kRateBytes - 1) & 7U));
  keccakf1600(st);

  std::array<unsigned char, 32> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<unsigned char>((st[i >> 3] >> (8U * (i & 7U))) & 0xffU);
  }
  return didzk::bytes_to_hex(out.data(), out.size(), true);
}

std::string keccak256_hex_from_text(const std::string& s) {
  return keccak256_hex_from_bytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

SessionKeyPair gen_ed25519_keypair() {
  require_sodium();
  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<unsigned char, crypto_sign_SECRETKEYBYTES> sk{};
  if (crypto_sign_keypair(pk.data(), sk.data()) != 0) {
    throw std::runtime_error("crypto_sign_keypair_failed");
  }
  SessionKeyPair kp;
  kp.pkHex = didzk::bytes_to_hex(pk.data(), pk.size(), true);
  kp.skHex = didzk::bytes_to_hex(sk.data(), sk.size(), true);
  return kp;
}

std::string hash_pubkey_to_field_hex(const std::string& pkHex) {
  const auto pk = didzk::hex_to_bytes(pkHex);
  return hash32_hex_from_bytes(pk.data(), pk.size());
}
