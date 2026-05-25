#include "merkle_poseidon.hpp"
#include "text_utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <gmpxx.h>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace didzk {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const mpz_class kBn254Fr("21888242871839275222246405745257275088548364400416034343698204186575808495617");
const int kNRoundsP[16] = {56, 57, 56, 60, 60, 63, 64, 63, 60, 66, 60, 65, 70, 60, 64, 68};
constexpr char kObjectOpen = static_cast<char>(123);
constexpr char kObjectClose = static_cast<char>(125);
constexpr char kArrayOpen = static_cast<char>(91);
constexpr char kArrayClose = static_cast<char>(93);

std::mutex g_cache_mu;
std::unordered_map<std::string, FieldElementBytes32> g_field_cache;
std::unordered_map<std::string, IdentityPoseidonBundle> g_bundle_cache;
PoseidonBridgeStats g_stats;

struct PoseidonParams {
  int t{0};
  int nRoundsF{8};
  int nRoundsP{0};
  std::vector<mpz_class> C;
  std::vector<mpz_class> S;
  std::vector<std::vector<mpz_class>> M;
  std::vector<std::vector<mpz_class>> P;
};

struct PoseidonRawConstants {
  std::vector<mpz_class> C;
  std::vector<mpz_class> S;
  std::vector<mpz_class> M;
  std::vector<mpz_class> P;
};

std::mutex g_params_mu;
std::unordered_map<int, PoseidonParams> g_params_cache;

fs::path legacy_poseidon_constants_path(const fs::path& root) {
  return root / "circuits" / "node_modules" / "circomlib" / "circuits" /
         "poseidon_constants.circom";
}

fs::path circomlibjs_poseidon_constants_path(const fs::path& root) {
  return root / "hardhat" / "node_modules" / "circomlibjs" / "src" /
         "poseidon_constants_opt.json";
}

fs::path find_poseidon_constants_path(const fs::path& root) {
  const fs::path legacy = legacy_poseidon_constants_path(root);
  if (fs::exists(legacy)) return legacy;
  const fs::path circomlibjs = circomlibjs_poseidon_constants_path(root);
  if (fs::exists(circomlibjs)) return circomlibjs;
  return {};
}

bool looks_like_project_root(const fs::path& root) {
  return !find_poseidon_constants_path(root).empty() &&
         fs::exists(root / "circuits" / "auth_membership.circom") &&
         fs::exists(root / "scripts" / "leaf_utils.mjs");
}


bool starts_with_hex_prefix(const std::string& s) {
  return s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

mpz_class normalize_fr(const mpz_class& in) {
  mpz_class out = in % kBn254Fr;
  if (out < 0) out += kBn254Fr;
  return out;
}

mpz_class parse_field_value(const std::string& raw) {
  std::string s = trim_copy(raw);
  if (s.empty()) throw std::runtime_error("empty_field_value");

  bool neg = false;
  if (s[0] == '+' || s[0] == '-') {
    neg = (s[0] == '-');
    s.erase(s.begin());
    s = trim_copy(s);
    if (s.empty()) throw std::runtime_error("invalid_signed_field_value");
  }

  mpz_class value;
  if (starts_with_hex_prefix(s)) {
    const std::string body = s.substr(2);
    if (body.empty()) throw std::runtime_error("invalid_hex_field");
    if (value.set_str(body, 16) != 0) throw std::runtime_error("invalid_hex_field");
  } else {
    for (char ch : s) {
      if (!std::isdigit(static_cast<unsigned char>(ch))) {
        throw std::runtime_error("invalid_decimal_field");
      }
    }
    if (value.set_str(s, 10) != 0) throw std::runtime_error("invalid_decimal_field");
  }
  return neg ? -value : value;
}

FieldElementBytes32 field_bytes_from_mpz(const mpz_class& value) {
  const mpz_class v = normalize_fr(value);
  FieldElementBytes32 out{};
  std::size_t count = 0;
  unsigned char tmp[32]{};
  mpz_export(tmp, &count, 1, 1, 1, 0, v.get_mpz_t());
  if (count > 32) throw std::runtime_error("field_overflow_256");
  if (count > 0) {
    std::memcpy(out.bytes.data() + (32 - count), tmp, count);
  }
  return out;
}

std::string hex_from_field_bytes(const FieldElementBytes32& value) {
  static const char* kHex = "0123456789abcdef";
  std::string out(66, '0');
  out[0] = '0';
  out[1] = 'x';
  for (std::size_t i = 0; i < value.bytes.size(); ++i) {
    out[2 + i * 2] = kHex[(value.bytes[i] >> 4) & 0x0f];
    out[3 + i * 2] = kHex[value.bytes[i] & 0x0f];
  }
  return out;
}

FieldElementBytes32 field_bytes_from_hex32(const std::string& raw) {
  const std::string s = trim_copy(raw);
  if (s.size() != 66 || s.rfind("0x", 0) != 0) throw std::runtime_error("bad_field_hex32");
  FieldElementBytes32 out{};
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (std::size_t i = 0; i < 32; ++i) {
    const int hi = val(s[2 + 2 * i]);
    const int lo = val(s[3 + 2 * i]);
    if (hi < 0 || lo < 0) throw std::runtime_error("bad_field_hex32");
    out.bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return out;
}

void record_stat(double* slot, const Clock::time_point& start) {
  const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  std::lock_guard<std::mutex> lock(g_cache_mu);
  g_stats.totalMs += ms;
  if (slot) *slot += ms;
}

fs::path find_project_root_from(const fs::path& start) {
  fs::path cur = fs::weakly_canonical(start);
  while (!cur.empty()) {
    if (looks_like_project_root(cur)) {
      return cur;
    }
    if (cur == cur.root_path()) break;
    cur = cur.parent_path();
  }
  return {};
}

fs::path poseidon_constants_path(const std::string& preferred_root) {
  const fs::path root = detect_project_root(preferred_root);
  const fs::path p = find_poseidon_constants_path(root);
  if (p.empty()) {
    throw std::runtime_error(
        "poseidon_constants_missing: tried " +
        legacy_poseidon_constants_path(root).string() + " and " +
        circomlibjs_poseidon_constants_path(root).string());
  }
  return p;
}

std::size_t find_matching_delimiter(const std::string& text,
                                    std::size_t open_pos,
                                    char open,
                                    char close,
                                    const std::string& error) {
  if (open_pos >= text.size() || text[open_pos] != open) throw std::runtime_error(error);
  int depth = 0;
  for (std::size_t i = open_pos; i < text.size(); ++i) {
    if (text[i] == open) ++depth;
    else if (text[i] == close) {
      --depth;
      if (depth == 0) return i;
    }
  }
  throw std::runtime_error(error);
}

std::string slice_inside_delimiters(const std::string& text,
                                    std::size_t open_pos,
                                    char open,
                                    char close,
                                    const std::string& error) {
  const std::size_t close_pos = find_matching_delimiter(text, open_pos, open, close, error);
  return text.substr(open_pos + 1, close_pos - open_pos - 1);
}

std::string extract_function_body(const std::string& content, const std::string& fn_name) {
  const std::string needle = "function " + fn_name + "(t)";
  const auto fn_pos = content.find(needle);
  if (fn_pos == std::string::npos) throw std::runtime_error("poseidon_fn_missing: " + fn_name);
  const auto brace_start = content.find(kObjectOpen, fn_pos);
  if (brace_start == std::string::npos) throw std::runtime_error("poseidon_fn_bad_open: " + fn_name);
  return slice_inside_delimiters(content, brace_start, kObjectOpen, kObjectClose,
                                 "poseidon_fn_bad_close: " + fn_name);
}

std::string extract_return_array_text(const std::string& fn_body, int t) {
  const std::regex branch_re("(?:if|else\\s+if)\\s*\\(\\s*t\\s*==\\s*" + std::to_string(t) + "\\s*\\)");
  std::smatch m;
  std::string::const_iterator search_start(fn_body.cbegin());
  if (!std::regex_search(search_start, fn_body.cend(), m, branch_re)) {
    throw std::runtime_error("poseidon_branch_missing_t=" + std::to_string(t));
  }
  const std::size_t branch_pos = static_cast<std::size_t>(m.position(0));
  const auto ret_pos = fn_body.find("return", branch_pos);
  if (ret_pos == std::string::npos) throw std::runtime_error("poseidon_return_missing_t=" + std::to_string(t));
  const auto arr_open = fn_body.find(kArrayOpen, ret_pos);
  if (arr_open == std::string::npos) throw std::runtime_error("poseidon_array_open_missing_t=" + std::to_string(t));
  const std::size_t arr_close = find_matching_delimiter(
      fn_body, arr_open, kArrayOpen, kArrayClose,
      "poseidon_array_close_missing_t=" + std::to_string(t));
  return fn_body.substr(arr_open, arr_close - arr_open + 1);
}

std::vector<mpz_class> collect_hex_tokens(const std::string& text) {
  static const std::regex hex_re("0x[0-9a-fA-F]+");
  std::vector<mpz_class> out;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), hex_re);
       it != std::sregex_iterator(); ++it) {
    out.push_back(normalize_fr(parse_field_value(it->str())));
  }
  return out;
}

std::vector<mpz_class> json_field_vector(const nlohmann::json& arr,
                                         const std::string& label) {
  if (!arr.is_array()) throw std::runtime_error("poseidon_json_vector_expected: " + label);
  std::vector<mpz_class> out;
  out.reserve(arr.size());
  for (const auto& item : arr) {
    if (!item.is_string()) throw std::runtime_error("poseidon_json_hex_expected: " + label);
    out.push_back(normalize_fr(parse_field_value(item.get<std::string>())));
  }
  return out;
}

std::vector<mpz_class> json_field_matrix_flat(const nlohmann::json& matrix,
                                             const std::string& label) {
  if (!matrix.is_array()) throw std::runtime_error("poseidon_json_matrix_expected: " + label);
  std::vector<mpz_class> out;
  for (const auto& row : matrix) {
    const std::vector<mpz_class> values = json_field_vector(row, label);
    out.insert(out.end(), values.begin(), values.end());
  }
  return out;
}

const nlohmann::json& poseidon_json_for_t(const nlohmann::json& root,
                                          const std::string& key,
                                          int t) {
  const int idx = t - 2;
  if (idx < 0) throw std::runtime_error("poseidon_json_bad_t=" + std::to_string(t));
  if (!root.contains(key) || !root.at(key).is_array()) {
    throw std::runtime_error("poseidon_json_key_missing: " + key);
  }
  const auto& outer = root.at(key);
  if (static_cast<std::size_t>(idx) >= outer.size()) {
    throw std::runtime_error("poseidon_json_t_missing: " + key + ":t=" + std::to_string(t));
  }
  return outer.at(static_cast<std::size_t>(idx));
}

std::vector<std::vector<mpz_class>> reshape_matrix(const std::vector<mpz_class>& flat, int t) {
  if (flat.size() != static_cast<std::size_t>(t * t)) {
    throw std::runtime_error("poseidon_matrix_size_mismatch_t=" + std::to_string(t));
  }
  std::vector<std::vector<mpz_class>> out(static_cast<std::size_t>(t), std::vector<mpz_class>(static_cast<std::size_t>(t)));
  std::size_t k = 0;
  for (int r = 0; r < t; ++r) {
    for (int c = 0; c < t; ++c) {
      out[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = flat[k++];
    }
  }
  return out;
}

std::vector<mpz_class> extract_poseidon_vector(const std::string& content,
                                               const std::string& fn_name,
                                               int t) {
  return collect_hex_tokens(extract_return_array_text(extract_function_body(content, fn_name), t));
}

PoseidonRawConstants extract_poseidon_raw_constants(const std::string& content, int t) {
  PoseidonRawConstants raw;
  raw.C = extract_poseidon_vector(content, "POSEIDON_C", t);
  raw.S = extract_poseidon_vector(content, "POSEIDON_S", t);
  raw.M = extract_poseidon_vector(content, "POSEIDON_M", t);
  raw.P = extract_poseidon_vector(content, "POSEIDON_P", t);
  return raw;
}

PoseidonRawConstants extract_poseidon_raw_constants_json(const std::string& content, int t) {
  const nlohmann::json root = nlohmann::json::parse(content);
  PoseidonRawConstants raw;
  raw.C = json_field_vector(poseidon_json_for_t(root, "C", t), "C");
  raw.S = json_field_vector(poseidon_json_for_t(root, "S", t), "S");
  raw.M = json_field_matrix_flat(poseidon_json_for_t(root, "M", t), "M");
  raw.P = json_field_matrix_flat(poseidon_json_for_t(root, "P", t), "P");
  return raw;
}

void validate_poseidon_param_sizes(const PoseidonParams& params) {
  const int t = params.t;
  const std::size_t c_expected = static_cast<std::size_t>(t * params.nRoundsF + params.nRoundsP);
  const std::size_t s_expected = static_cast<std::size_t>(params.nRoundsP * (t * 2 - 1));

  if (params.C.size() != c_expected) {
    throw std::runtime_error("poseidon_C_size_mismatch_t=" + std::to_string(t));
  }
  if (params.S.size() != s_expected) {
    throw std::runtime_error("poseidon_S_size_mismatch_t=" + std::to_string(t));
  }
}

PoseidonParams build_poseidon_params(int t, const PoseidonRawConstants& raw) {
  PoseidonParams params;
  params.t = t;
  params.nRoundsF = 8;
  params.nRoundsP = kNRoundsP[t - 2];
  params.C = raw.C;
  params.S = raw.S;
  params.M = reshape_matrix(raw.M, t);
  params.P = reshape_matrix(raw.P, t);
  validate_poseidon_param_sizes(params);
  return params;
}

PoseidonParams load_poseidon_params_from_file(int t, const std::string& preferred_root) {
  const fs::path p = poseidon_constants_path(preferred_root);
  const std::string content = read_text_file(p);
  const PoseidonRawConstants raw = p.extension() == ".json"
      ? extract_poseidon_raw_constants_json(content, t)
      : extract_poseidon_raw_constants(content, t);
  PoseidonParams params = build_poseidon_params(t, raw);
  return params;
}

const PoseidonParams& poseidon_params_for_t(int t, const std::string& preferred_root) {
  std::lock_guard<std::mutex> lock(g_params_mu);
  auto it = g_params_cache.find(t);
  if (it != g_params_cache.end()) return it->second;
  auto inserted = g_params_cache.emplace(t, load_poseidon_params_from_file(t, preferred_root));
  return inserted.first->second;
}

const PoseidonParams& poseidon_params_for_arity(std::size_t n_inputs, const std::string& preferred_root) {
  switch (n_inputs) {
    case 2: return poseidon_params_for_t(3, preferred_root);
    case 5: return poseidon_params_for_t(6, preferred_root);
    default: throw std::runtime_error("unsupported_poseidon_arity");
  }
}

mpz_class sigma(const mpz_class& x) {
  const mpz_class x2 = normalize_fr(x * x);
  const mpz_class x4 = normalize_fr(x2 * x2);
  return normalize_fr(x4 * x);
}

std::vector<mpz_class> mix_full(const std::vector<mpz_class>& state,
                                const std::vector<std::vector<mpz_class>>& matrix) {
  const std::size_t t = state.size();
  std::vector<mpz_class> out(t, 0);
  for (std::size_t i = 0; i < t; ++i) {
    mpz_class acc = 0;
    for (std::size_t j = 0; j < t; ++j) {
      acc += matrix[j][i] * state[j];
      acc = normalize_fr(acc);
    }
    out[i] = normalize_fr(acc);
  }
  return out;
}

std::vector<mpz_class> mix_sparse(const std::vector<mpz_class>& state,
                                  const std::vector<mpz_class>& sparse_slice) {
  const std::size_t t = state.size();
  std::vector<mpz_class> out(t, 0);
  mpz_class acc = 0;
  for (std::size_t i = 0; i < t; ++i) {
    acc += sparse_slice[i] * state[i];
    acc = normalize_fr(acc);
  }
  out[0] = normalize_fr(acc);
  for (std::size_t i = 1; i < t; ++i) {
    out[i] = normalize_fr(state[i] + state[0] * sparse_slice[t + i - 1]);
  }
  return out;
}

std::string poseidon_hash_native(const std::vector<std::string>& inputs, const std::string& preferred_root) {
  const PoseidonParams& params = poseidon_params_for_arity(inputs.size(), preferred_root);
  const std::size_t t = static_cast<std::size_t>(params.t);
  std::vector<mpz_class> state(t, 0);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    state[i + 1] = normalize_fr(parse_field_value(inputs[i]));
  }

  // ark[0]
  for (std::size_t j = 0; j < t; ++j) {
    state[j] = normalize_fr(state[j] + params.C[j]);
  }

  // First half full rounds except the transition round into P.
  for (int r = 0; r < params.nRoundsF / 2 - 1; ++r) {
    for (mpz_class& x : state) x = sigma(x);
    const std::size_t c_off = static_cast<std::size_t>((r + 1) * params.t);
    for (std::size_t j = 0; j < t; ++j) {
      state[j] = normalize_fr(state[j] + params.C[c_off + j]);
    }
    state = mix_full(state, params.M);
  }

  // Transition full round before partial rounds: sigma -> ark -> P
  for (mpz_class& x : state) x = sigma(x);
  {
    const std::size_t c_off = static_cast<std::size_t>((params.nRoundsF / 2) * params.t);
    for (std::size_t j = 0; j < t; ++j) {
      state[j] = normalize_fr(state[j] + params.C[c_off + j]);
    }
  }
  state = mix_full(state, params.P);

  // Partial rounds.
  const std::size_t partial_c_off = static_cast<std::size_t>((params.nRoundsF / 2 + 1) * params.t);
  const std::size_t sparse_width = static_cast<std::size_t>(params.t * 2 - 1);
  for (int r = 0; r < params.nRoundsP; ++r) {
    state[0] = sigma(state[0]);
    state[0] = normalize_fr(state[0] + params.C[partial_c_off + static_cast<std::size_t>(r)]);
    const std::size_t s_off = static_cast<std::size_t>(r) * sparse_width;
    std::vector<mpz_class> sparse_slice(params.S.begin() + static_cast<std::ptrdiff_t>(s_off),
                                        params.S.begin() + static_cast<std::ptrdiff_t>(s_off + sparse_width));
    state = mix_sparse(state, sparse_slice);
  }

  // Second half full rounds except the final mixLast round.
  for (int r = 0; r < params.nRoundsF / 2 - 1; ++r) {
    for (mpz_class& x : state) x = sigma(x);
    const std::size_t c_off = static_cast<std::size_t>((params.nRoundsF / 2 + 1) * params.t + params.nRoundsP + r * params.t);
    for (std::size_t j = 0; j < t; ++j) {
      state[j] = normalize_fr(state[j] + params.C[c_off + j]);
    }
    state = mix_full(state, params.M);
  }

  // Final round: sigma only, then MixLast with M. For a single output this is equivalent to
  // a final full mix and reading state[0].
  for (mpz_class& x : state) x = sigma(x);
  state = mix_full(state, params.M);
  return hex_from_field_bytes(field_bytes_from_mpz(state[0]));
}

void validate_path(const MerklePathZK& path) {
  if (path.pathElements.size() != path.pathIndex.size()) {
    throw std::runtime_error("path_length_mismatch");
  }
  for (auto idx : path.pathIndex) {
    if (idx != 0 && idx != 1) throw std::runtime_error("bad_path_index");
  }
}

std::string bundle_key(const IdentityStateZK& st, const MerklePathZK* path) {
  std::ostringstream oss;
  oss << trim_copy(st.sid) << '|'
      << trim_copy(st.rho) << '|'
      << trim_copy(st.cid) << '|'
      << trim_copy(st.pkNormHash) << '|'
      << trim_copy(st.pkRecHash) << '|'
      << st.ver << '|'
      << (st.active ? 1 : 0);
  if (path) {
    oss << "|p:" << path->pathElements.size();
    for (const auto& e : path->pathElements) oss << '|' << trim_copy(e);
    oss << "|i:" << path->pathIndex.size();
    for (auto i : path->pathIndex) oss << '|' << static_cast<unsigned>(i);
  }
  return oss.str();
}

IdentityPoseidonBundle build_bundle(const IdentityStateZK& st, const MerklePathZK* path,
                                    const std::string& project_root) {
  IdentityPoseidonBundle out;
  out.cidHex = (!trim_copy(st.sid).empty() || !trim_copy(st.rho).empty())
                   ? poseidon_cid(st.sid, st.rho, project_root)
                   : normalize_field_hex(st.cid, project_root);
  out.cidField = field_bytes_from_hex32(out.cidHex);
  out.leafHex = poseidon_leaf(out.cidHex, st.pkNormHash, st.pkRecHash, st.ver, st.active, project_root);
  out.leafField = field_bytes_from_hex32(out.leafHex);
  if (path) {
    out.rootHex = compute_root_from_path(out.leafHex, *path, project_root);
    out.rootField = field_bytes_from_hex32(out.rootHex);
    out.hasRoot = true;
  }
  return out;
}

}  // namespace

std::string detect_project_root(const std::string& preferred_root) {
  if (!preferred_root.empty()) {
    const fs::path candidate = fs::weakly_canonical(preferred_root);
    if (looks_like_project_root(candidate)) {
      return candidate.string();
    }
  }

  const char* env_root = std::getenv("PROJECT_ROOT");
  if (env_root && *env_root) {
    const fs::path candidate = fs::weakly_canonical(env_root);
    if (looks_like_project_root(candidate)) {
      return candidate.string();
    }
  }

  if (auto found = find_project_root_from(fs::current_path()); !found.empty()) {
    return found.string();
  }

  throw std::runtime_error("unable_to_detect_project_root: set PROJECT_ROOT or pass preferred_root");
}

std::string normalize_field_hex(const std::string& value, const std::string& project_root) {
  (void)project_root;
  return field_bytes_to_hex(normalize_field_native(value, project_root));
}

FieldElementBytes32 normalize_field_native(const std::string& value, const std::string& project_root) {
  (void)project_root;
  const auto start = Clock::now();
  const std::string key = trim_copy(value);
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    auto it = g_field_cache.find(key);
    if (it != g_field_cache.end()) {
      ++g_stats.cacheHits;
      ++g_stats.calls;
      g_stats.normalizeMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
      return it->second;
    }
  }
  const FieldElementBytes32 out = field_bytes_from_mpz(parse_field_value(value));
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    g_field_cache.emplace(key, out);
    ++g_stats.calls;
  }
  record_stat(&g_stats.normalizeMs, start);
  return out;
}

std::vector<FieldElementBytes32> normalize_fields_native(const std::vector<std::string>& values,
                                                         const std::string& project_root) {
  std::vector<FieldElementBytes32> out;
  out.reserve(values.size());
  for (const auto& v : values) out.push_back(normalize_field_native(v, project_root));
  return out;
}

std::string field_bytes_to_hex(const FieldElementBytes32& value) {
  return hex_from_field_bytes(value);
}

bool field_equal(const FieldElementBytes32& lhs, const FieldElementBytes32& rhs) {
  return lhs.bytes == rhs.bytes;
}

std::string poseidon_hash2(const std::string& a, const std::string& b, const std::string& project_root) {
  const auto start = Clock::now();
  const std::string out = poseidon_hash_native({a, b}, project_root);
  record_stat(&g_stats.hash2Ms, start);
  return out;
}

std::string poseidon_hash5(const std::string& a, const std::string& b, const std::string& c,
                           const std::string& d, const std::string& e,
                           const std::string& project_root) {
  const auto start = Clock::now();
  const std::string out = poseidon_hash_native({a, b, c, d, e}, project_root);
  record_stat(&g_stats.leafMs, start);
  return out;
}

std::string poseidon_cid(const std::string& sid, const std::string& rho, const std::string& project_root) {
  const auto start = Clock::now();
  const std::string out = poseidon_hash_native({sid, rho}, project_root);
  record_stat(&g_stats.cidMs, start);
  return out;
}

std::string poseidon_leaf(const std::string& cid, const std::string& pkNormHash,
                          const std::string& pkRecHash, std::uint64_t ver, bool active,
                          const std::string& project_root) {
  return poseidon_hash5(cid, pkNormHash, pkRecHash,
                        std::to_string(ver), active ? "1" : "0", project_root);
}

std::string compute_leaf_from_identity(const IdentityStateZK& st, const std::string& project_root) {
  const std::string cid = trim_copy(st.cid).empty() ? poseidon_cid(st.sid, st.rho, project_root)
                                                    : normalize_field_hex(st.cid, project_root);
  return poseidon_leaf(cid, st.pkNormHash, st.pkRecHash, st.ver, st.active, project_root);
}

IdentityPoseidonBundle compute_identity_bundle(const IdentityStateZK& st, const std::string& project_root) {
  const std::string key = bundle_key(st, nullptr);
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    auto it = g_bundle_cache.find(key);
    if (it != g_bundle_cache.end()) {
      ++g_stats.cacheHits;
      return it->second;
    }
  }
  IdentityPoseidonBundle out = build_bundle(st, nullptr, project_root);
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    g_bundle_cache.emplace(key, out);
  }
  return out;
}

IdentityPoseidonBundle compute_identity_bundle_with_path(const IdentityStateZK& st,
                                                         const MerklePathZK& path,
                                                         const std::string& project_root) {
  validate_path(path);
  const std::string key = bundle_key(st, &path);
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    auto it = g_bundle_cache.find(key);
    if (it != g_bundle_cache.end()) {
      ++g_stats.cacheHits;
      return it->second;
    }
  }
  IdentityPoseidonBundle out = build_bundle(st, &path, project_root);
  {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    g_bundle_cache.emplace(key, out);
  }
  return out;
}

std::vector<IdentityPoseidonBundle> compute_identity_bundles(const std::vector<IdentityStateZK>& states,
                                                             const std::string& project_root) {
  std::vector<IdentityPoseidonBundle> out;
  out.reserve(states.size());
  for (const auto& st : states) out.push_back(compute_identity_bundle(st, project_root));
  return out;
}

std::vector<IdentityPoseidonBundle> compute_identity_bundles_with_paths(const std::vector<IdentityStateZK>& states,
                                                                        const std::vector<MerklePathZK>& paths,
                                                                        const std::string& project_root) {
  if (states.size() != paths.size()) throw std::runtime_error("state_path_count_mismatch");
  std::vector<IdentityPoseidonBundle> out;
  out.reserve(states.size());
  for (std::size_t i = 0; i < states.size(); ++i) {
    out.push_back(compute_identity_bundle_with_path(states[i], paths[i], project_root));
  }
  return out;
}

std::string fold_merkle_level(const std::string& current, const std::string& sibling,
                              std::uint8_t path_index, const std::string& project_root) {
  if (path_index == 0) return poseidon_hash2(current, sibling, project_root);
  if (path_index == 1) return poseidon_hash2(sibling, current, project_root);
  throw std::runtime_error("bad_path_index");
}

std::string compute_root_from_path(const std::string& leaf, const MerklePathZK& path,
                                   const std::string& project_root) {
  validate_path(path);
  const auto start = Clock::now();
  std::string cur = normalize_field_hex(leaf, project_root);
  for (std::size_t i = 0; i < path.pathElements.size(); ++i) {
    cur = fold_merkle_level(cur, path.pathElements[i], path.pathIndex[i], project_root);
  }
  record_stat(&g_stats.foldPathMs, start);
  return normalize_field_hex(cur, project_root);
}

bool verify_merkle_path_local(const std::string& leaf, const MerklePathZK& path,
                              const std::string& project_root) {
  return normalize_field_hex(compute_root_from_path(leaf, path, project_root), project_root) ==
         normalize_field_hex(path.root, project_root);
}

void reset_poseidon_bridge_stats() {
  std::lock_guard<std::mutex> lock(g_cache_mu);
  g_stats = PoseidonBridgeStats{};
}

PoseidonBridgeStats get_poseidon_bridge_stats() {
  std::lock_guard<std::mutex> lock(g_cache_mu);
  return g_stats;
}

}  // namespace didzk
