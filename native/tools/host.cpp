// OS services for seed-compatible Neri tooling. Build and test policy lives in Neri.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

extern char **environ;

namespace {
void check(int error, const char *operation) {
  if (error != 0) throw std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}

void write(const std::string &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << value;
  stream.close();
  if (!stream) throw std::runtime_error("cannot write " + path);
}

void list_files(const char *root, const char *suffix, const char *output) {
  std::vector<std::string> paths;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto path = entry.path().string();
    if (!path.ends_with(suffix)) continue;
    if (path.find('\n') != std::string::npos || path.find('\r') != std::string::npos)
      throw std::runtime_error("file names must not contain line separators");
    paths.push_back(path);
  }
  std::sort(paths.begin(), paths.end());
  std::string result;
  for (const auto &path : paths) result += path + "\n";
  write(output, result);
}

void stamp(const char *root, const char *epoch) {
  std::size_t consumed = 0;
  const auto seconds = std::stoll(epoch, &consumed);
  if (consumed != std::strlen(epoch) || seconds < 0)
    throw std::runtime_error("invalid timestamp");
  const auto time = std::filesystem::file_time_type::clock::from_sys(
      std::chrono::system_clock::time_point(std::chrono::seconds(seconds)));
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_symlink()) throw std::runtime_error("timestamp tree contains a symlink");
    std::filesystem::last_write_time(entry.path(), time);
  }
  std::filesystem::last_write_time(root, time);
}

// stdout/stderr are separate files, stdin is EOF, and the complete child process
// group is terminated at the deadline. The result file distinguishes signals,
// ordinary exits, and timeout (124); helper failures return nonzero themselves.
void run(int argc, char **argv) {
  if (argc < 7) throw std::runtime_error("run <seconds> <stdout> <stderr> <status> <executable> [args...]");
  std::size_t consumed = 0;
  const int seconds = std::stoi(argv[2], &consumed);
  if (consumed != std::strlen(argv[2]) || seconds < 1 || seconds > 3600)
    throw std::runtime_error("timeout must be 1..3600 seconds");
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  check(posix_spawn_file_actions_init(&actions), "file actions");
  check(posix_spawnattr_init(&attributes), "spawn attributes");
  try {
    check(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0), "stdin");
    check(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, argv[3], O_WRONLY | O_CREAT | O_TRUNC, 0600), "stdout");
    check(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, argv[4], O_WRONLY | O_CREAT | O_TRUNC, 0600), "stderr");
    check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP), "process group flags");
    check(posix_spawnattr_setpgroup(&attributes, 0), "process group");
    std::vector<char *> arguments;
    for (int index = 6; index < argc; ++index) arguments.push_back(argv[index]);
    arguments.push_back(nullptr);
    pid_t child = 0;
    check(posix_spawnp(&child, argv[6], &actions, &attributes, arguments.data(), environ), "spawn");
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(seconds);
    int status = 0;
    rusage usage{};
    bool timed_out = false;
    while (true) {
      const auto result = wait4(child, &status, WNOHANG, &usage);
      if (result == child) break;
      if (result < 0 && errno != EINTR) {
        const int error = errno;
        kill(-child, SIGKILL);
        while (wait4(child, &status, 0, &usage) < 0 && errno == EINTR) {}
        check(error, "waitpid");
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        kill(-child, SIGKILL);
        while (wait4(child, &status, 0, &usage) < 0) {
          if (errno != EINTR) check(errno, "waitpid after timeout");
        }
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Also reap descendants' work when the direct child exits first.
    kill(-child, SIGKILL);
    const int code = timed_out ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#if defined(__APPLE__)
    const auto rss_bytes = usage.ru_maxrss;
#else
    const auto rss_bytes = usage.ru_maxrss * 1024;
#endif
    const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto user = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    const auto system = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    write(std::string(argv[5]) + ".metrics.json", "{\"wallSeconds\":" + std::to_string(wall) +
        ",\"userSeconds\":" + std::to_string(user) + ",\"systemSeconds\":" + std::to_string(system) +
        ",\"peakRssBytes\":" + std::to_string(rss_bytes) + "}\n");
    write(argv[5], std::to_string(code) + "\n");
  } catch (...) {
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    throw;
  }
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
}
}

int main(int argc, char **argv) {
  try {
    if (argc == 5 && std::string(argv[1]) == "list") list_files(argv[2], argv[3], argv[4]);
    else if (argc == 4 && std::string(argv[1]) == "stamp") stamp(argv[2], argv[3]);
    else if (argc == 4 && std::string(argv[1]) == "replace") std::filesystem::rename(argv[2], argv[3]);
    else if (argc >= 7 && std::string(argv[1]) == "run") run(argc, argv);
    else throw std::runtime_error("expected list <root> <suffix> <output>, or run");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "neri-host: " << error.what() << '\n';
    return 1;
  }
}
