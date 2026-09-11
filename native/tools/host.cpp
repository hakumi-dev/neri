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
#include "neri/host_path.h"
#if defined(_WIN32)
#include "../platform/windows_support.h"
#include <psapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

extern char **environ;
#endif

namespace {
#if !defined(_WIN32)
void check(int error, const char *operation) {
  if (error != 0) throw std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}
#endif

void write(const std::string &path, const std::string &value) {
  std::ofstream stream(neri::host_path(path), std::ios::binary | std::ios::trunc);
  stream << value;
  stream.close();
  if (!stream) throw std::runtime_error("cannot write " + path);
}

void list_files(const char *root, const char *suffix, const char *output) {
  std::vector<std::string> paths;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(neri::host_path(root))) {
    if (!entry.is_regular_file()) continue;
    const auto path = neri::path_text(entry.path());
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

bool generated_directory(const std::filesystem::path &path) {
  const auto name = neri::path_text(path.filename());
  return name == ".git" || name == ".neri" || name == ".cache" || name == ".idea" ||
      name == ".bootstrap" || name == "build" || name == "out" || name == "dist" ||
      name == "target" || name == "bin";
}

std::filesystem::path project_source_root(const char *project, const char *relative) {
  const std::filesystem::path project_root = std::filesystem::absolute(neri::host_path(project));
  const std::filesystem::path suffix = neri::host_path(relative);
  if (suffix.empty() || suffix.is_absolute())
    throw std::runtime_error("project source root must be relative");
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(project_root)))
    throw std::runtime_error("project source root must not be a symlink");
  if (suffix == ".") return project_root;
  std::filesystem::path root = project_root;
  for (const auto &component : suffix) {
    const auto name = neri::path_text(component);
    if (name.empty() || name == "." || name == "..")
      throw std::runtime_error("project source root contains traversal");
    root /= component;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(root)))
      throw std::runtime_error("project source root contains a symlink");
  }
  const auto canonical_project = std::filesystem::weakly_canonical(project_root);
  const auto canonical_root = std::filesystem::weakly_canonical(root);
  const auto relative_root = canonical_root.lexically_relative(canonical_project);
  if (relative_root.empty() || relative_root == ".")
    throw std::runtime_error("project source root escapes the project");
  for (const auto &component : relative_root) {
    if (component == "..") throw std::runtime_error("project source root escapes the project");
  }
  return root;
}

void list_project_files(const char *project, const char *relative, const char *suffix, const char *output) {
  std::vector<std::string> paths;
  const auto root = project_source_root(project, relative);
  for (auto entry = std::filesystem::recursive_directory_iterator(root);
       entry != std::filesystem::recursive_directory_iterator(); ++entry) {
    if (entry->is_symlink()) {
      if (entry->is_directory()) entry.disable_recursion_pending();
      continue;
    }
    if (entry->is_directory()) {
      if (generated_directory(entry->path()) ||
          (entry->path() != root && (std::filesystem::exists(entry->path() / "manifest.json") ||
                                     std::filesystem::exists(entry->path() / "neri.json"))))
        entry.disable_recursion_pending();
      continue;
    }
    if (!entry->is_regular_file()) continue;
    const auto path = neri::path_text(entry->path());
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

void canonical_path(const char *path, const char *output) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(neri::host_path(path), error);
  if (error || canonical.empty())
    throw std::runtime_error("cannot canonicalize project path");
  write(output, neri::path_text(canonical));
}

void stamp(const char *root, const char *epoch) {
  std::size_t consumed = 0;
  const auto seconds = std::stoll(epoch, &consumed);
  if (consumed != std::strlen(epoch) || seconds < 0)
    throw std::runtime_error("invalid timestamp");
#if defined(_WIN32)
  const auto time = std::chrono::clock_cast<std::filesystem::file_time_type::clock>(
#else
  const auto time = std::filesystem::file_time_type::clock::from_sys(
#endif
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
#if defined(_WIN32)
#include "host_windows.inc"
#else
void run(int argc, char **argv) {
  if (argc < 7) throw std::runtime_error("run <seconds> <stdout> <stderr> <status> <executable> [args...]");
  const bool has_input = argc >= 9 && std::string(argv[6]) == "--stdin";
  const int executable_index = has_input ? 8 : 6;
  if (argc <= executable_index)
    throw std::runtime_error("run requires an executable after --stdin <path>");
  std::size_t consumed = 0;
  const int seconds = std::stoi(argv[2], &consumed);
  if (consumed != std::strlen(argv[2]) || seconds < 1 || seconds > 3600)
    throw std::runtime_error("timeout must be 1..3600 seconds");
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  check(posix_spawn_file_actions_init(&actions), "file actions");
  check(posix_spawnattr_init(&attributes), "spawn attributes");
  try {
    check(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                           has_input ? argv[7] : "/dev/null", O_RDONLY, 0), "stdin");
    check(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, argv[3], O_WRONLY | O_CREAT | O_TRUNC, 0600), "stdout");
    check(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, argv[4], O_WRONLY | O_CREAT | O_TRUNC, 0600), "stderr");
    check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP), "process group flags");
    check(posix_spawnattr_setpgroup(&attributes, 0), "process group");
    std::vector<char *> arguments;
    for (int index = executable_index; index < argc; ++index) arguments.push_back(argv[index]);
    arguments.push_back(nullptr);
    pid_t child = 0;
    check(posix_spawnp(&child, argv[executable_index], &actions, &attributes, arguments.data(), environ), "spawn");
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
#endif
}

int main(int argc, char **argv) {
  try {
#if defined(_WIN32)
    neri::windows::command_arguments arguments;
    argc = static_cast<int>(arguments.storage.size());
    argv = arguments.pointers.data();
#endif
    if (argc == 5 && std::string(argv[1]) == "list") list_files(argv[2], argv[3], argv[4]);
    else if (argc == 6 && std::string(argv[1]) == "list-project") list_project_files(argv[2], argv[3], argv[4], argv[5]);
    else if (argc == 4 && std::string(argv[1]) == "canonical-path") canonical_path(argv[2], argv[3]);
    else if (argc == 4 && std::string(argv[1]) == "stamp") stamp(argv[2], argv[3]);
    else if (argc == 4 && std::string(argv[1]) == "replace") std::filesystem::rename(argv[2], argv[3]);
    else if (argc == 3 && std::string(argv[1]) == "remove-directory") {
      const auto directory = neri::host_path(argv[2]);
      if (!std::filesystem::is_directory(directory) || !std::filesystem::remove(directory))
        throw std::runtime_error("cannot remove empty directory");
    }
    else if (argc >= 7 && std::string(argv[1]) == "run") run(argc, argv);
    else throw std::runtime_error("expected list <root> <suffix> <output>, or run");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "neri-host: " << error.what() << '\n';
    return 1;
  }
}
