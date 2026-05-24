#include "process_utils.hpp"

#include "text_utils.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace didzk {
namespace {

int decode_wait_status(int status) {
#if defined(__unix__) || defined(__APPLE__)
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
#endif
  return status;
}

std::string command_label(const std::vector<std::string>& argv) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i) oss << ' ';
    oss << shell_quote(argv[i]);
  }
  return oss.str();
}

std::runtime_error command_error(const std::string& label, const CommandResult& res) {
  std::ostringstream oss;
  oss << "command_failed(rc=" << res.rc << "): " << label;
  if (!res.stdoutText.empty()) {
    oss << "\nstdout:\n" << res.stdoutText;
  }
  return std::runtime_error(oss.str());
}

}  // namespace

std::string shell_quote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  out.push_back('\'');
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

CommandResult run_command(const CommandSpec& spec) {
  if (spec.argv.empty()) {
    throw std::runtime_error("empty_command_argv");
  }

#if defined(__unix__) || defined(__APPLE__)
  int out_pipe[2];
  if (pipe(out_pipe) != 0) {
    throw std::runtime_error(std::string("pipe_failed: ") + std::strerror(errno));
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    throw std::runtime_error(std::string("fork_failed: ") + std::strerror(errno));
  }

  if (pid == 0) {
    close(out_pipe[0]);
    dup2(out_pipe[1], STDOUT_FILENO);
    if (spec.mergeStderr) dup2(out_pipe[1], STDERR_FILENO);
    close(out_pipe[1]);

    if (!spec.cwd.empty()) {
      if (chdir(spec.cwd.string().c_str()) != 0) _exit(126);
    }
    for (const auto& kv : spec.env) {
      setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }

    std::vector<char*> argv;
    argv.reserve(spec.argv.size() + 1);
    for (const std::string& arg : spec.argv) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }

  close(out_pipe[1]);
  std::array<char, 4096> buffer{};
  std::string output;
  while (true) {
    const ssize_t n = read(out_pipe[0], buffer.data(), buffer.size());
    if (n > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) break;
    if (errno == EINTR) continue;
    close(out_pipe[0]);
    throw std::runtime_error(std::string("read_pipe_failed: ") + std::strerror(errno));
  }
  close(out_pipe[0]);

  int raw_status = 0;
  while (waitpid(pid, &raw_status, 0) < 0) {
    if (errno == EINTR) continue;
    throw std::runtime_error(std::string("waitpid_failed: ") + std::strerror(errno));
  }

  CommandResult result;
  result.rc = decode_wait_status(raw_status);
  result.stdoutText = trim_copy(output);
  return result;
#else
  (void)spec;
  throw std::runtime_error("process_runner_requires_posix");
#endif
}

CommandResult run_command_capture_argv(const std::vector<std::string>& argv) {
  CommandSpec spec;
  spec.argv = argv;
  return run_command(spec);
}

std::string run_command_capture_argv_text_checked(const std::vector<std::string>& argv) {
  const CommandResult res = run_command_capture_argv(argv);
  if (res.rc != 0) throw command_error(command_label(argv), res);
  return res.stdoutText;
}

CommandResult run_command_capture(const std::string& command) {
  return run_command_capture_argv({"/bin/sh", "-c", command});
}

std::string run_command_capture_text_checked(const std::string& command) {
  const CommandResult res = run_command_capture(command);
  if (res.rc != 0) throw command_error(command, res);
  return res.stdoutText;
}

void run_command_checked(const std::string& command) {
  (void)run_command_capture_text_checked(command);
}

void run_command_argv_checked(const std::vector<std::string>& argv) {
  const CommandResult res = run_command_capture_argv(argv);
  if (res.rc != 0) throw command_error(command_label(argv), res);
}

void run_command_to_log_checked(const CommandSpec& spec, const std::filesystem::path& logPath) {
  std::error_code ec;
  std::filesystem::create_directories(logPath.parent_path(), ec);
  const CommandResult res = run_command(spec);
  {
    std::ofstream ofs(logPath, std::ios::binary);
    if (!ofs) throw std::runtime_error("open_log_failed: " + logPath.string());
    ofs << res.stdoutText;
    if (!res.stdoutText.empty()) ofs << "\n";
  }
  if (res.rc != 0) {
    std::string tail = res.stdoutText;
    if (tail.size() > 2000) tail = tail.substr(tail.size() - 2000);
    throw std::runtime_error("command_failed rc=" + std::to_string(res.rc) +
                             ";cmd=" + command_label(spec.argv) +
                             ";log=" + logPath.string() +
                             ";tail=" + tail);
  }
}

}  // namespace didzk
