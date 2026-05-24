#include "main_cli.hpp"
#include "text_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using didzk::trim_copy;

namespace {

std::string need_arg_value(int& i, int argc, char** argv, const char* name) {
  if (i + 1 >= argc) throw std::runtime_error(std::string("missing_value_for_") + name);
  return std::string(argv[++i]);
}

bool apply_mode_arg(const std::string& name, Args* args) {
  if (name == "--zk_auth_e2e") args->zkAuthE2E = true;
  else if (name == "--zk_recovery_e2e") args->zkRecoveryE2E = true;
  else if (name == "--ttss_setup") args->ttssSetup = true;
  else if (name == "--ttss_recover") args->ttssRecover = true;
  else if (name == "--ttss_recover_and_rotate") args->ttssRecoverAndRotate = true;
  else if (name == "--ttss_trace") args->ttssTrace = true;
  else if (name == "--ttss_trace_publish") args->ttssTracePublish = true;
  else return false;
  return true;
}

bool apply_value_arg(const std::string& name, int& i, int argc, char** argv, Args* args) {
  if (name == "--id") args->id = need_arg_value(i, argc, argv, "id");
  else if (name == "--bb") args->bbUrl = need_arg_value(i, argc, argv, "bb");
  else if (name == "--pirate") args->pirateUrl = need_arg_value(i, argc, argv, "pirate");
  else if (name == "--runs") args->runs = std::max(1, std::stoi(need_arg_value(i, argc, argv, "runs")));
  else if (name == "--depth") args->depth = std::max(1, std::stoi(need_arg_value(i, argc, argv, "depth")));
  else if (name == "--bb_each") args->bbEach = std::stoi(need_arg_value(i, argc, argv, "bb_each"));
  else if (name == "--csv") args->csvPath = need_arg_value(i, argc, argv, "csv");
  else if (name == "--project_root") args->projectRoot = need_arg_value(i, argc, argv, "project_root");
  else if (name == "--workdir") args->workDir = need_arg_value(i, argc, argv, "workdir");
  else if (name == "--recover_case") args->recoverCase = need_arg_value(i, argc, argv, "recover_case");
  else if (name == "--timeout_ms") args->timeoutMs = std::stoi(need_arg_value(i, argc, argv, "timeout_ms"));
  else if (name == "--register_wait_ms") args->registerWaitTimeoutMs = std::stoi(need_arg_value(i, argc, argv, "register_wait_ms"));
  else if (name == "--path_wait_ms") args->pathWaitTimeoutMs = std::stoi(need_arg_value(i, argc, argv, "path_wait_ms"));
  else if (name == "--root_wait_ms") args->rootWaitTimeoutMs = std::stoi(need_arg_value(i, argc, argv, "root_wait_ms"));
  else if (name == "--root_poll_ms") args->rootPollMs = std::stoi(need_arg_value(i, argc, argv, "root_poll_ms"));
  else if (name == "--bb_async_submit") args->bbAsyncSubmit = std::stoi(need_arg_value(i, argc, argv, "bb_async_submit")) != 0;
  else if (name == "--bb_confirmations") args->bbConfirmations = std::max(0, std::stoi(need_arg_value(i, argc, argv, "bb_confirmations")));
  else if (name == "--bb_include_snapshot") args->bbIncludeSnapshot = std::stoi(need_arg_value(i, argc, argv, "bb_include_snapshot")) != 0;
  else if (name == "--ttss_state") args->ttssStatePath = need_arg_value(i, argc, argv, "ttss_state");
  else if (name == "--committee_urls") args->committeeUrls = need_arg_value(i, argc, argv, "committee_urls");
  else if (name == "--committee_token") args->committeeToken = need_arg_value(i, argc, argv, "committee_token");
  else if (name == "--vk_file") args->vkFile = need_arg_value(i, argc, argv, "vk_file");
  else if (name == "--trace_result") args->traceResultPath = need_arg_value(i, argc, argv, "trace_result");
  else if (name == "--delta") args->delta = std::stod(need_arg_value(i, argc, argv, "delta"));
  else if (name == "--challenge_count") args->challengeCount = std::max(1, std::stoi(need_arg_value(i, argc, argv, "challenge_count")));
  else if (name == "--max_queries") args->maxQueries = std::max(1, std::stoi(need_arg_value(i, argc, argv, "max_queries")));
  else if (name == "--leaked_indexes") args->leakedIndexes = need_arg_value(i, argc, argv, "leaked_indexes");
  else if (name == "--trace_session_id") args->traceSessionId = need_arg_value(i, argc, argv, "trace_session_id");
  else if (name == "--ttss_n") args->ttssN = std::max(2, std::stoi(need_arg_value(i, argc, argv, "ttss_n")));
  else if (name == "--ttss_t") args->ttssT = std::max(2, std::stoi(need_arg_value(i, argc, argv, "ttss_t")));
  else return false;
  return true;
}

int selected_mode_count(const Args& args) {
  int count = 0;
  count += args.zkAuthE2E ? 1 : 0;
  count += args.zkRecoveryE2E ? 1 : 0;
  count += args.ttssSetup ? 1 : 0;
  count += args.ttssRecover ? 1 : 0;
  count += args.ttssRecoverAndRotate ? 1 : 0;
  count += args.ttssTrace ? 1 : 0;
  count += args.ttssTracePublish ? 1 : 0;
  return count;
}

bool ttss_mode_selected(const Args& args) {
  return args.ttssSetup || args.ttssRecover || args.ttssRecoverAndRotate ||
         args.ttssTrace || args.ttssTracePublish;
}

void validate_args(const Args& args) {
  if (selected_mode_count(args) != 1) throw std::runtime_error("choose_exactly_one_mode");
  if (args.bbEach != 0 && args.bbEach != 1) throw std::runtime_error("bb_each_must_be_0_or_1");
  if (!ttss_mode_selected(args)) return;
  if (args.ttssT > args.ttssN) throw std::runtime_error("ttss_t_must_be_leq_ttss_n");
  if (args.ttssSetup && trim_copy(args.committeeUrls).empty()) {
    throw std::runtime_error("committee_urls_required_for_ttss_setup");
  }
  if ((args.ttssRecover || args.ttssRecoverAndRotate || args.ttssTrace ||
       args.ttssTracePublish) && trim_copy(args.ttssStatePath).empty()) {
    throw std::runtime_error("ttss_state_required_for_selected_mode");
  }
  if (args.ttssTracePublish && trim_copy(args.traceResultPath).empty()) {
    throw std::runtime_error("trace_result_required_for_ttss_trace_publish");
  }
}

}  // namespace

void print_usage(const char* prog) {
  std::cerr
      << "Usage:\n"
      << "  " << prog << " --zk_auth_e2e [options]\n"
      << "  " << prog << " --zk_recovery_e2e [options]\n"
      << "  " << prog << " --ttss_setup [options]\n"
      << "  " << prog << " --ttss_recover [options]\n"
      << "  " << prog << " --ttss_recover_and_rotate [options]\n"
      << "  " << prog << " --ttss_trace [options]\n"
      << "  " << prog << " --ttss_trace_publish [options]\n\n"
      << "Core options:\n"
      << "  --id <str>               identity id (default: zk_demo)\n"
      << "  --bb <url>               bb_service_zk base url (default: http://127.0.0.1:3000)\n"
      << "  --pirate <url>           pirate_box base url (default: http://127.0.0.1:4000)\n"
      << "  --project_root <path>    did-e2e root; auto-detected if omitted\n"
      << "  --workdir <path>         output directory root\n"
      << "  --timeout_ms <n>         fast HTTP timeout in ms (default: 5000)\n"
      << "  --register_wait_ms <n>   wait budget for register/recovery submit (default: 60000)\n"
      << "  --path_wait_ms <n>       wait budget for /path and /leaf readiness (default: 60000)\n"
      << "  --root_wait_ms <n>       wait for new root after recovery (default: 15000)\n"
      << "  --root_poll_ms <n>       polling interval in ms (default: 250)\n"
      << "  --bb_async_submit <0|1>  submit register/rotate without waiting (default: 1)\n"
      << "  --bb_confirmations <n>   confirmations when service waits (default: 1)\n"
      << "  --bb_include_snapshot <0|1> request inline snapshot hints in submit response (default: 1)\n\n"
      << "Auth / recovery options:\n"
      << "  --runs <n>               number of auth rounds (default: 1)\n"
      << "  --depth <n>              merkle depth expectation (default: 20)\n"
      << "  --bb_each <0|1>          refetch /root+/path every round (default: 1)\n"
      << "  --csv <path>             CSV output path\n"
      << "  --recover_case <kind>    legal | pirate (default: legal)\n\n"
      << "TTSS options:\n"
      << "  --ttss_state <path>      setup json path produced by --ttss_setup\n"
      << "  --committee_urls <csv>   comma-separated committee base URLs (setup override)\n"
      << "  --committee_token <str>  committee bearer token / shared token\n"
      << "  --ttss_n <n>             number of guardians (default: 5)\n"
      << "  --ttss_t <t>             threshold (default: 3)\n"
      << "  --vk_file <path>         explicit TTSS vk json path\n"
      << "  --trace_result <path>    trace_result.json path for publish mode\n"
      << "  --delta <float>          trace delta parameter (default: 1e-6)\n"
      << "  --challenge_count <n>    honest shares per query (default: t-f)\n"
      << "  --max_queries <n>        trace query rounds (default: 3)\n"
      << "  --leaked_indexes <csv>   leaked guardian indexes (default: derived t-1 set)\n"
      << "  --trace_session_id <s>   optional trace session id override\n";
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (apply_mode_arg(a, &args)) continue;
    if (apply_value_arg(a, i, argc, argv, &args)) continue;
    if (a == "--help" || a == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    }
    throw std::runtime_error("unknown_arg: " + a);
  }
  validate_args(args);
  return args;
}
