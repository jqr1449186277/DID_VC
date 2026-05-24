#include "csv_utils.hpp"

#include "text_utils.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

void ensure_csv_header(const std::string& csvPath) {
  if (didzk::trim_copy(csvPath).empty()) return;
  const fs::path p(csvPath);
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  if (fs::exists(p, ec) && fs::file_size(p, ec) > 0) return;
  std::ofstream ofs(csvPath, std::ios::binary);
  ofs << "mode,id,depth,bb_each,path_fetch_ms,witness_ms,prove_ms,verify_ms,proof_bytes,public_bytes,recover_case,old_proof_valid,new_proof_valid,ok\n";
}

std::string csv_escape(const std::string& s) {
  if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '\"') out += "\"\"";
    else out.push_back(c);
  }
  out += "\"";
  return out;
}

void append_csv_row(const std::string& csvPath, const RunRow& row) {
  if (didzk::trim_copy(csvPath).empty()) return;
  ensure_csv_header(csvPath);
  std::ofstream ofs(csvPath, std::ios::app | std::ios::binary);
  if (!ofs) throw std::runtime_error("open_csv_failed: " + csvPath);
  ofs << csv_escape(row.mode) << ','
      << csv_escape(row.id) << ','
      << row.depth << ','
      << row.bbEach << ','
      << row.pathFetchMs << ','
      << row.witnessMs << ','
      << row.proveMs << ','
      << row.verifyMs << ','
      << row.proofBytes << ','
      << row.publicBytes << ','
      << csv_escape(row.recoverCase) << ','
      << row.oldProofValid << ','
      << row.newProofValid << ','
      << row.ok << '\n';
}
