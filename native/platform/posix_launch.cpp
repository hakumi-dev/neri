#if !defined(_WIN32)
#include "posix_launch.h"
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <mutex>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace neri::platform {
namespace {
std::mutex launch_lock;

#if !defined(__linux__)
bool cloexec(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFD);
  return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}
#endif

bool move_above_standard(int &descriptor) {
  if (descriptor > STDERR_FILENO) return true;
  const int replacement = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  if (replacement < 0) return false;
  close(descriptor);
  descriptor = replacement;
  return true;
}

std::vector<std::string> candidates(const std::vector<std::string> &arguments,
                                    const std::vector<std::string> &environment) {
  if (arguments.front().find('/') != std::string::npos) return {arguments.front()};
  std::string path = "/bin:/usr/bin";
  for (const auto &entry : environment)
    if (entry.starts_with("PATH=")) path = entry.substr(5);
  std::vector<std::string> result;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = path.find(':', start);
    const std::string directory = path.substr(start, end == std::string::npos ? end : end - start);
    result.push_back((directory.empty() ? "." : directory) + "/" + arguments.front());
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return result;
}
}

bool posix_pipe_cloexec(int descriptors[2], int &error) {
  std::lock_guard guard(launch_lock);
#if defined(__linux__)
  if (pipe2(descriptors, O_CLOEXEC) != 0) { error = errno; return false; }
#else
  if (pipe(descriptors) != 0 || !cloexec(descriptors[0]) || !cloexec(descriptors[1])) {
    error = errno;
    if (descriptors[0] >= 0) close(descriptors[0]);
    if (descriptors[1] >= 0) close(descriptors[1]);
    descriptors[0] = descriptors[1] = -1;
    return false;
  }
#endif
  if (!move_above_standard(descriptors[0]) || !move_above_standard(descriptors[1])) {
    error = errno;
    close(descriptors[0]); close(descriptors[1]);
    descriptors[0] = descriptors[1] = -1;
    return false;
  }
  error = 0;
  return true;
}

bool posix_launch(const posix_launch_options &options, pid_t &process, int &error) {
  process = 0;
  error = 0;
  if (options.arguments.empty() || options.arguments.front().empty()) {
    error = EINVAL;
    return false;
  }
  std::vector<char *> arguments;
  std::vector<char *> environment;
  for (const auto &item : options.arguments) arguments.push_back(const_cast<char *>(item.c_str()));
  for (const auto &item : options.environment) environment.push_back(const_cast<char *>(item.c_str()));
  arguments.push_back(nullptr);
  environment.push_back(nullptr);
  const auto paths = candidates(options.arguments, options.environment);
  int redirects[3] = {options.standard_input, options.standard_output, options.standard_error};
  int owned_redirects[3] = {-1, -1, -1};
  const auto close_redirects = [&owned_redirects]() {
    for (int descriptor : owned_redirects) if (descriptor >= 0) close(descriptor);
  };
  for (size_t index = 0; index < 3; ++index) {
    if (redirects[index] >= 0 && redirects[index] <= STDERR_FILENO) {
      owned_redirects[index] = fcntl(redirects[index], F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
      if (owned_redirects[index] < 0) {
        error = errno;
        close_redirects();
        return false;
      }
      redirects[index] = owned_redirects[index];
    }
  }
  int startup[2] = {-1, -1};
  std::unique_lock guard(launch_lock);
#if defined(__linux__)
  if (pipe2(startup, O_CLOEXEC) != 0) { error = errno; close_redirects(); return false; }
#else
  if (pipe(startup) != 0 || !cloexec(startup[0]) || !cloexec(startup[1])) {
    error = errno; if (startup[0] >= 0) close(startup[0]);
    if (startup[1] >= 0) close(startup[1]); close_redirects(); return false;
  }
#endif
  if (!move_above_standard(startup[0]) || !move_above_standard(startup[1])) {
    error = errno; close(startup[0]); close(startup[1]); close_redirects(); return false;
  }
  process = fork();
  if (process < 0) {
    error = errno;
    close(startup[0]); close(startup[1]);
    close_redirects();
    return false;
  }
  if (process == 0) {
    close(startup[0]);
    int failure = 0;
    if ((options.process_group && setpgid(0, 0) != 0) ||
        (!options.working_directory.empty() && chdir(options.working_directory.c_str()) != 0) ||
        (redirects[0] >= 0 && dup2(redirects[0], STDIN_FILENO) < 0) ||
        (redirects[1] >= 0 && dup2(redirects[1], STDOUT_FILENO) < 0) ||
        (redirects[2] >= 0 && dup2(redirects[2], STDERR_FILENO) < 0)) {
      failure = errno;
    } else {
      bool denied = false;
      for (const auto &path : paths) {
        execve(path.c_str(), arguments.data(), environment.data());
        failure = errno;
        if (failure == EACCES) { denied = true; continue; }
        if (failure != ENOENT && failure != ENOTDIR) break;
      }
      if ((failure == ENOENT || failure == ENOTDIR) && denied) failure = EACCES;
    }
    if (failure == 0) failure = ENOENT;
    size_t offset = 0;
    while (offset < sizeof(failure)) {
      const ssize_t count = write(startup[1],
          reinterpret_cast<const uint8_t *>(&failure) + offset, sizeof(failure) - offset);
      if (count > 0) offset += static_cast<size_t>(count);
      else if (count < 0 && errno == EINTR) continue;
      else break;
    }
    _exit(127);
  }
  guard.unlock();
  close_redirects();
  close(startup[1]);
  ssize_t count;
  do { count = read(startup[0], &error, sizeof(error)); }
  while (count < 0 && errno == EINTR);
  const int read_error = count < 0 ? errno : 0;
  close(startup[0]);
  if (count == 0) return true;
  if (options.process_group) kill(-process, SIGKILL);
  else kill(process, SIGKILL);
  int ignored = 0;
  while (waitpid(process, &ignored, 0) < 0 && errno == EINTR) {}
  process = 0;
  if (count < 0) error = read_error;
  else if (count != sizeof(error)) error = EIO;
  return false;
}
}
#endif
