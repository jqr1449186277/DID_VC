#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace didzk {

struct CommandResult {
  int rc{0};
  std::string stdoutText;
};

struct CommandSpec {
  std::vector<std::string> argv;
  std::filesystem::path cwd;
  std::map<std::string, std::string> env;
  bool mergeStderr{true};
};

std::string shell_quote(const std::string& s);
CommandResult run_command(const CommandSpec& spec);
CommandResult run_command_capture_argv(const std::vector<std::string>& argv);
std::string run_command_capture_argv_text_checked(const std::vector<std::string>& argv);
CommandResult run_command_capture(const std::string& command);
std::string run_command_capture_text_checked(const std::string& command);
void run_command_checked(const std::string& command);
void run_command_argv_checked(const std::vector<std::string>& argv);
void run_command_to_log_checked(const CommandSpec& spec, const std::filesystem::path& logPath);

}  // namespace didzk
