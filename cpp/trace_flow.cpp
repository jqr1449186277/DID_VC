#include "trace_flow.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void run_logged_command(const std::vector<std::string>& argv, const fs::path& logPath) {
  didzk::CommandSpec spec;
  spec.argv = argv;
  didzk::run_command_to_log_checked(spec, logPath);
}

}  // namespace

std::string load_pirate_box_for_trace(const Args& args,
                                      const nlohmann::json& setupJson,
                                      const std::vector<didzk::ShareEnvelope>& leakedShares,
                                      const fs::path& workDir) {
  const std::string idHash = find_id_hash_from_setup(setupJson);
  const std::uint64_t ver = static_cast<std::uint64_t>(setupJson.value("ver", 0ull));
  const std::uint64_t epoch = static_cast<std::uint64_t>(setupJson.value("epoch", 0ull));
  const std::uint64_t n = static_cast<std::uint64_t>(setupJson.value("n", 0ull));
  const std::uint64_t t = static_cast<std::uint64_t>(setupJson.value("t", 0ull));
  nlohmann::json leaked = nlohmann::json::array();
  for (const auto& env : leakedShares) leaked.push_back(share_envelope_to_njson(env));
  const std::string boxId = trim_copy(args.traceSessionId).empty()
                                ? ("box_" + now_stamp_compact() + "_v" + std::to_string(ver))
                                : ("box_" + args.traceSessionId);
  nlohmann::json body = {
      {"token", args.committeeToken},
      {"boxConfig",
       {{"scheme", didzk::kTTSSSchemeNitsShamirV1},
        {"mode", "stateless"},
        {"boxId", boxId},
        {"idHash", idHash},
        {"ver", ver},
        {"epoch", epoch},
        {"n", n},
        {"t", t},
        {"f", leakedShares.size()},
        {"secretType", "srec_seed"},
        {"outputMode", "seed"},
        {"leakedShares", leaked}}}};
  std::string resp;
  if (!http_post_json(args.pirateUrl, "/loadStaticBox", body, resp, std::max(args.timeoutMs, 5000))) {
    throw std::runtime_error("pirate_loadStaticBox_failed: " + resp);
  }
  const nlohmann::json j = nlohmann::json::parse(resp);
  if (j.value("ok", 0) != 1) {
    throw std::runtime_error("pirate_loadStaticBox_bad_response: " + resp);
  }
  save_json_pretty(workDir / "load_box_response.json", j);
  return j.value("boxId", boxId);
}

int run_ttss_trace(const Args& args) {
  const nlohmann::json setupJson = load_ttss_setup_json(args.ttssStatePath);
  const fs::path baseWorkDir(default_base_workdir(args, "ttss_phase5"));
  const fs::path workDir = baseWorkDir / (args.id + "_ttss_trace");
  std::error_code ec;
  fs::create_directories(workDir, ec);

  const std::string idHash = find_id_hash_from_setup(setupJson);
  const std::uint64_t ver = static_cast<std::uint64_t>(setupJson.value("ver", 0ull));
  const std::uint64_t epoch = static_cast<std::uint64_t>(setupJson.value("epoch", 0ull));
  const int n = setupJson.value("n", args.ttssN);
  const int t = setupJson.value("t", args.ttssT);
  const std::vector<std::string> committeeUrls = committee_urls_from_setup_or_args(setupJson, args);
  if (static_cast<int>(committeeUrls.size()) < n) {
    throw std::runtime_error("committee_url_count_lt_n_for_trace");
  }

  std::vector<std::uint32_t> leakedIndexes = trim_copy(args.leakedIndexes).empty()
                                                 ? default_leaked_guardian_indexes(n, t)
                                                 : parse_guardian_indexes_csv(args.leakedIndexes);
  if (leakedIndexes.empty()) throw std::runtime_error("empty_leaked_indexes");
  if (static_cast<int>(leakedIndexes.size()) >= t) throw std::runtime_error("leaked_indexes_must_be_lt_threshold");

  std::vector<didzk::ShareEnvelope> leakedShares;
  leakedShares.reserve(leakedIndexes.size());
  for (const auto idx : leakedIndexes) {
    if (idx == 0 || idx > committeeUrls.size()) throw std::runtime_error("leaked_guardian_index_out_of_range");
    didzk::ShareEnvelope env;
    std::string err;
    if (!committee_fetch_share(committeeUrls.at(static_cast<std::size_t>(idx - 1)), "/shareForTrace", args.committeeToken,
                               idHash, ver, epoch, idx, &env, &err)) {
      throw std::runtime_error("committee_fetch_shareForTrace_failed: idx=" + std::to_string(idx) + ";err=" + err);
    }
    leakedShares.push_back(std::move(env));
  }
  nlohmann::json leakedArr = nlohmann::json::array();
  for (const auto& env : leakedShares) leakedArr.push_back(share_envelope_to_njson(env));
  save_json_pretty(workDir / "leaked_shares.json", leakedArr);

  const std::string boxId = load_pirate_box_for_trace(args, setupJson, leakedShares, workDir);

  std::vector<std::string> challengeUrls;
  for (std::size_t i = 0; i < committeeUrls.size(); ++i) {
    const std::uint32_t idx = static_cast<std::uint32_t>(i + 1);
    if (std::find(leakedIndexes.begin(), leakedIndexes.end(), idx) == leakedIndexes.end()) {
      challengeUrls.push_back(committeeUrls[i]);
    }
  }
  if (challengeUrls.empty()) throw std::runtime_error("empty_challenge_committee_urls");

  const fs::path vkFile = resolve_vk_file_path(setupJson, args);
  const fs::path traceResultPath = trim_copy(args.traceResultPath).empty()
                                       ? (workDir / "trace_result.json")
                                       : fs::path(args.traceResultPath);
  const fs::path traceVerifyPath = traceResultPath.parent_path() / "trace_verify.json";
  const fs::path traceRunLog = workDir / "trace_run.log";
  const fs::path traceVerifyLog = workDir / "trace_verify.log";
  const int challengeCount = args.challengeCount > 0 ? args.challengeCount : std::max(1, t - static_cast<int>(leakedIndexes.size()));
  const std::string projectRoot = default_project_root(args);
  const fs::path tracerScript = fs::path(projectRoot) / "hardhat" / "tracer_client.js";
  if (!fs::exists(tracerScript)) throw std::runtime_error("tracer_client_js_not_found: " + tracerScript.string());

  std::vector<std::string> traceCmd = {
      "node", tracerScript.string(), "trace-run",
      "--idHash", idHash,
      "--ver", std::to_string(ver),
      "--epoch", std::to_string(epoch),
      "--n", std::to_string(n),
      "--t", std::to_string(t),
      "--delta", std::to_string(args.delta),
      "--vk-file", vkFile.string(),
      "--committee", join_urls_csv(challengeUrls),
      "--pirate", args.pirateUrl,
      "--box-id", boxId,
      "--challenge-count", std::to_string(challengeCount),
      "--max-queries", std::to_string(args.maxQueries),
      "--out", traceResultPath.string()};
  if (!trim_copy(args.traceSessionId).empty()) {
    traceCmd.push_back("--trace-session-id");
    traceCmd.push_back(args.traceSessionId);
  }
  run_logged_command(traceCmd, traceRunLog);

  const std::vector<std::string> verifyCmd = {
      "node", tracerScript.string(), "trace-verify",
      "--vk-file", vkFile.string(),
      "--trace-result", traceResultPath.string(),
      "--out", traceVerifyPath.string()};
  run_logged_command(verifyCmd, traceVerifyLog);

  const nlohmann::json traceResult = load_json_file(traceResultPath);
  const nlohmann::json traceVerify = load_json_file(traceVerifyPath);
  const bool ok = traceResult.value("ok", false) && traceVerify.value("accepted", false);
  save_json_pretty(workDir / "trace_context.json",
                   {{"idHash", idHash},
                    {"ver", ver},
                    {"epoch", epoch},
                    {"boxId", boxId},
                    {"leakedIndexes", leakedIndexes},
                    {"challengeCommitteeUrls", challengeUrls},
                    {"traceResult", traceResultPath.string()},
                    {"traceVerify", traceVerifyPath.string()}});
  std::cout << "[ttss_trace] ok=" << (ok ? 1 : 0)
            << " accusedSet=" << traceResult.value("accusedSet", nlohmann::json::array()).dump()
            << " trace_result=" << traceResultPath.string()
            << " verify_json=" << traceVerifyPath.string() << "\n";
  return ok ? 0 : 1;
}

int run_ttss_trace_publish(const Args& args) {
  const nlohmann::json setupJson = load_ttss_setup_json(args.ttssStatePath);
  const fs::path baseWorkDir(default_base_workdir(args, "ttss_phase5"));
  const fs::path workDir = baseWorkDir / (args.id + "_ttss_trace_publish");
  std::error_code ec;
  fs::create_directories(workDir, ec);

  const fs::path vkFile = resolve_vk_file_path(setupJson, args);
  const fs::path traceResultPath(args.traceResultPath);
  if (!fs::exists(traceResultPath)) {
    throw std::runtime_error("trace_result_not_found: " + traceResultPath.string());
  }
  const std::string projectRoot = default_project_root(args);
  const fs::path tracerScript = fs::path(projectRoot) / "hardhat" / "tracer_client.js";
  if (!fs::exists(tracerScript)) throw std::runtime_error("tracer_client_js_not_found: " + tracerScript.string());

  const fs::path verifyOut = workDir / "trace_verify.json";
  const fs::path publishOut = workDir / "trace_publish.json";
  const fs::path publishLog = workDir / "trace_publish.log";

  const std::vector<std::string> publishCmd = {
      "node", tracerScript.string(), "publish-trace",
      "--bb", args.bbUrl,
      "--vk-file", vkFile.string(),
      "--trace-result", traceResultPath.string(),
      "--verify-out", verifyOut.string(),
      "--out", publishOut.string()};
  run_logged_command(publishCmd, publishLog);

  const nlohmann::json publishJson = load_json_file(publishOut);
  const bool ok = publishJson.value("ok", 0) == 1;
  std::cout << "[ttss_trace_publish] ok=" << (ok ? 1 : 0)
            << " publish_json=" << publishOut.string()
            << " traceDigest=" << publishJson.value("traceDigest", std::string()) << "\n";
  return ok ? 0 : 1;
}
