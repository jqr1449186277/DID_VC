#include "merkle_poseidon.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
  std::cerr << "usage:\n"
            << "  " << argv0 << " leaf <sid> <rho> <pkNormHash> <pkRecHash> <ver> <active>\n"
            << "  " << argv0 << " root <leaf> <depth> (<pathElement> <pathIndex>)+\n";
}

std::uint64_t parse_u64(const std::string& s) {
  return static_cast<std::uint64_t>(std::stoull(s));
}

std::uint8_t parse_u8(const std::string& s) {
  const auto v = std::stoul(s);
  if (v > 255) throw std::runtime_error("u8_overflow");
  return static_cast<std::uint8_t>(v);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage(argv[0]);
      return 2;
    }

    const std::string mode = argv[1];
    if (mode == "leaf") {
      if (argc != 8) {
        usage(argv[0]);
        return 2;
      }
      const std::string cid = didzk::poseidon_cid(argv[2], argv[3]);
      const std::string leaf = didzk::poseidon_leaf(cid, argv[4], argv[5], parse_u64(argv[6]),
                                                    parse_u8(argv[7]) != 0);
      std::cout << "cid=" << cid << "\nleaf=" << leaf << "\n";
      return 0;
    }

    if (mode == "root") {
      if (argc < 5) {
        usage(argv[0]);
        return 2;
      }
      const std::string leaf = argv[2];
      const std::size_t depth = static_cast<std::size_t>(std::stoul(argv[3]));
      const int expected_argc = 4 + static_cast<int>(depth) * 2;
      if (argc != expected_argc) {
        std::cerr << "root mode expected " << expected_argc - 1
                  << " arguments after mode, got " << argc - 2 << "\n";
        return 2;
      }
      didzk::MerklePathZK path;
      path.depth = static_cast<std::uint32_t>(depth);
      for (std::size_t i = 0; i < depth; ++i) {
        path.pathElements.push_back(argv[4 + static_cast<int>(i) * 2]);
        path.pathIndex.push_back(parse_u8(argv[5 + static_cast<int>(i) * 2]));
      }
      std::cout << didzk::compute_root_from_path(leaf, path) << "\n";
      return 0;
    }

    usage(argv[0]);
    return 2;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 1;
  }
}
