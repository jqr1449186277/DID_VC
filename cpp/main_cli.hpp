#pragma once

#include <string>

struct Args {
  bool zkAuthE2E{false};
  bool zkRecoveryE2E{false};
  bool ttssSetup{false};
  bool ttssRecover{false};
  bool ttssRecoverAndRotate{false};
  bool ttssTrace{false};
  bool ttssTracePublish{false};
  std::string id{"zk_demo"};
  std::string bbUrl{"http://127.0.0.1:3000"};
  std::string pirateUrl{"http://127.0.0.1:4000"};
  std::string csvPath;
  std::string projectRoot;
  std::string workDir;
  std::string ttssStatePath;
  std::string traceResultPath;
  std::string vkFile;
  std::string traceSessionId;
  std::string leakedIndexes;
  std::string committeeUrls;
  std::string committeeToken{"demo-token"};
  int ttssN{5};
  int ttssT{3};
  int challengeCount{0};
  int maxQueries{3};
  double delta{1e-6};
  int runs{1};
  int depth{20};
  int bbEach{1};
  int timeoutMs{5000};
  int registerWaitTimeoutMs{60000};
  int pathWaitTimeoutMs{60000};
  int rootWaitTimeoutMs{15000};
  int rootPollMs{250};
  bool bbAsyncSubmit{true};
  int bbConfirmations{1};
  bool bbIncludeSnapshot{true};
  std::string recoverCase{"legal"};
  bool preferRapidsnark{true};
  bool keepIntermediateFiles{true};
};

void print_usage(const char* prog);
Args parse_args(int argc, char** argv);
