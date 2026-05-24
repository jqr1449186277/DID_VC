#include "zk_auth_flow.hpp"
#include "ttss_flow.hpp"
#include "trace_flow.hpp"
#include "main_cli.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    if (args.zkAuthE2E) {
      return run_zk_auth_e2e(args);
    }
    if (args.zkRecoveryE2E) {
      return run_zk_recovery_e2e(args);
    }
    if (args.ttssSetup) {
      return run_ttss_setup(args);
    }
    if (args.ttssRecover) {
      return run_ttss_recover(args);
    }
    if (args.ttssRecoverAndRotate) {
      return run_ttss_recover_and_rotate(args);
    }
    if (args.ttssTrace) {
      return run_ttss_trace(args);
    }
    if (args.ttssTracePublish) {
      return run_ttss_trace_publish(args);
    }
    print_usage(argv[0]);
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "[did_demo_zk] fatal: " << e.what() << "\n";
    return 1;
  }
}
