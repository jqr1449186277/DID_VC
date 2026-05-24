// cpp/input_export.cpp
#include "zk_backend.hpp"
#include "hex_utils.hpp"
#include "process_utils.hpp"
#include "text_utils.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace didzk {
namespace {

namespace fs = std::filesystem;

bool is_all_decimal_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool is_all_hex_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string field_to_dec_string(const std::string& value) {
  const std::string v = trim_copy(value);
  if (v.empty()) {
    throw std::runtime_error("empty_field_value");
  }

  if (is_all_decimal_digits(v)) {
    return v;
  }

  std::string hex_body;
  if (starts_with_0x(v)) {
    hex_body = v.substr(2);
  } else if (is_all_hex_digits(v)) {
    hex_body = v;
  } else {
    throw std::runtime_error("invalid_field_value: " + v);
  }

  if (hex_body.empty()) return "0";

  const std::string node_code =
      "const x = process.argv[1];"
      "const n = x.startsWith('0x') || x.startsWith('0X') ? BigInt(x) : BigInt('0x' + x);"
      "console.log(n.toString(10));";

  return run_command_capture_argv_text_checked({"node", "-e", node_code, v});
}

std::string uint64_to_dec(std::uint64_t v) {
  return std::to_string(v);
}

void ensure_path_lengths_match(const ZkWitnessInput& in) {
  if (in.pathElements.size() != in.pathIndex.size()) {
    throw std::runtime_error("path_length_mismatch");
  }
  for (std::size_t i = 0; i < in.pathIndex.size(); ++i) {
    if (in.pathIndex[i] != 0 && in.pathIndex[i] != 1) {
      throw std::runtime_error("bad_path_index_at_" + std::to_string(i));
    }
  }
}

}  // namespace

std::string to_circuit_dec_string(const std::string& value) {
  return field_to_dec_string(value);
}

nlohmann::json to_json_obj(const ZkWitnessInput& in) {
  ensure_path_lengths_match(in);

  nlohmann::json j;
  j["root"] = to_circuit_dec_string(in.root);
  j["sid"] = to_circuit_dec_string(in.sid);
  j["rho"] = to_circuit_dec_string(in.rho);
  j["pkNormHash"] = to_circuit_dec_string(in.pkNormHash);
  j["pkRecHash"] = to_circuit_dec_string(in.pkRecHash);
  j["ver"] = uint64_to_dec(in.ver);

  std::vector<std::string> path_elements_dec;
  path_elements_dec.reserve(in.pathElements.size());
  for (const auto& x : in.pathElements) {
    path_elements_dec.push_back(to_circuit_dec_string(x));
  }
  j["pathElements"] = path_elements_dec;

  std::vector<std::string> path_index_str;
  path_index_str.reserve(in.pathIndex.size());
  for (std::uint8_t bit : in.pathIndex) {
    if (bit != 0 && bit != 1) {
      throw std::runtime_error("bad_path_index");
    }
    path_index_str.push_back(bit == 0 ? "0" : "1");
  }
  j["pathIndex"] = path_index_str;

  j["ctxHash"] = to_circuit_dec_string(in.ctxHash);
  j["sessPkHash"] = to_circuit_dec_string(in.sessPkHash);
  j["epoch"] = uint64_to_dec(in.epoch);

  return j;
}

void export_zk_input_json(const ZkWitnessInput& in, const std::string& outPath) {
  const nlohmann::json j = to_json_obj(in);
  const fs::path p(outPath);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path());
  }
  std::ofstream ofs(outPath, std::ios::binary);
  if (!ofs) {
    throw std::runtime_error("open_output_failed: " + outPath);
  }
  ofs << j.dump(2) << '\n';
  ofs.close();
  if (!ofs) {
    throw std::runtime_error("write_output_failed: " + outPath);
  }
}

std::string render_zk_input_json_text(const ZkWitnessInput& in) {
  const nlohmann::json j = to_json_obj(in);
  return j.dump(2) + "\n";
}

}  // namespace didzk
