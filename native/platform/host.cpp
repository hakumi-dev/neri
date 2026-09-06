#include "host.h"
#include "neri/host_path.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <locale.h>
#include <system_error>
#if defined(_WIN32)
#include "windows_support.h"
#include <io.h>
#include <process.h>
#include <malloc.h>
#define fsync _commit
#define getpid _getpid
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace neri::platform {
namespace { std::atomic<uint64_t> temporary_file_counter{0}; }
double parse_float(const char *text, char **end) {
#if defined(_WIN32)
  static const auto locale = _create_locale(LC_NUMERIC, "C");
  if (!locale) { errno = EINVAL; *end = const_cast<char *>(text); return 0; }
  return _strtod_l(text, end, locale);
#else
  static const auto locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
  if (!locale) { errno = EINVAL; *end = const_cast<char *>(text); return 0; }
  return strtod_l(text, end, locale);
#endif
}
void *allocate(size_t size, size_t alignment, bool zeroed) {
#if defined(_WIN32)
  void *pointer = _aligned_malloc(size, std::max(alignment, sizeof(void *)));
#else
  if (alignment <= alignof(std::max_align_t)) return zeroed ? std::calloc(1, size) : std::malloc(size);
  void *pointer = nullptr;
  if (posix_memalign(&pointer, alignment, size) != 0) return nullptr;
#endif
  if (pointer && zeroed) std::memset(pointer, 0, size);
  return pointer;
}
void deallocate(void *pointer) {
#if defined(_WIN32)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
[[nodiscard]] bool write_all(int descriptor, const uint8_t *bytes,
                             size_t size) {
  while (size != 0U) {
#if defined(_WIN32)
    const auto written = ::_write(descriptor, bytes, static_cast<unsigned int>(std::min(size, size_t{INT_MAX})));
#else
    const auto written = ::write(descriptor, bytes, size);
#endif
    if (written <= 0) {
      if (written == 0) { errno = EIO; return false; }
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    bytes += written;
    size -= static_cast<size_t>(written);
  }
  return true;
}

[[nodiscard]] bool write_atomic(std::string_view path, const uint8_t *bytes,
                                size_t size, std::string &error) {
  std::string temporary;
  int descriptor = -1;
  for (unsigned int attempt = 0; attempt < 100U; ++attempt) {
    temporary = std::string(path) + ".neri-" +
                std::to_string(static_cast<unsigned long long>(::getpid())) +
                "-" + std::to_string(temporary_file_counter.fetch_add(
                    1, std::memory_order_relaxed)) + ".tmp";
#if defined(_WIN32)
    descriptor = ::_wopen(neri::windows::wide(temporary).c_str(), O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
#else
    descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (descriptor >= 0 || errno != EEXIST) {
      break;
    }
  }
  if (descriptor < 0) {
    error = "temporary file creation failed: " + std::string(std::strerror(errno));
    return false;
  }

  bool succeeded = write_all(descriptor, bytes, size);
  int failure = succeeded ? 0 : errno;
  if (succeeded && ::fsync(descriptor) != 0) {
    succeeded = false;
    failure = errno;
  }
  if (::close(descriptor) != 0 && succeeded) {
    succeeded = false;
    failure = errno;
  }
#if defined(_WIN32)
  if (succeeded && !MoveFileExW(neri::windows::wide(temporary).c_str(), neri::windows::wide(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    errno = EIO;
#else
  if (succeeded && ::rename(temporary.c_str(), std::string(path).c_str()) != 0) {
#endif
    succeeded = false;
    failure = errno;
  }
  if (!succeeded) {
#if defined(_WIN32)
    static_cast<void>(::_wunlink(neri::windows::wide(temporary).c_str()));
#else
    static_cast<void>(::unlink(temporary.c_str()));
#endif
    error = "atomic file write failed: " + std::string(std::strerror(failure));
    return false;
  }
  error.clear();
  return true;
}


bool remove_file(std::string_view path, std::string &error) {
#if defined(_WIN32)
  const auto result = _wunlink(windows::wide(path).c_str());
#else
  const auto result = unlink(std::string(path).c_str());
#endif
  if (result != 0 && errno != ENOENT) { error = "file removal failed: " + std::string(std::strerror(errno)); return false; }
  error.clear();
  return true;
}
std::optional<std::string> environment(std::string_view name) {
#if defined(_WIN32)
  const auto variable = windows::wide(name);
  SetLastError(ERROR_SUCCESS);
  const DWORD size = GetEnvironmentVariableW(variable.c_str(), nullptr, 0);
  if (!size) return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>("");
  std::wstring value(size, L'\0');
  const DWORD length = GetEnvironmentVariableW(variable.c_str(), value.data(), size);
  if (length >= size) return environment(name);
  value.resize(length);
  return windows::utf8(value);
#else
  const char *value = std::getenv(std::string(name).c_str());
  return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}
std::optional<int64_t> run(std::vector<std::string> &arguments, std::string &error) {
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1U);
  for (auto &item : arguments) {
    argv.push_back(item.data());
  }
  argv.push_back(nullptr);

#if defined(_WIN32)
  unsigned long status = 0, system_error = 0;
  if (!neri::windows::run(arguments, status, system_error)) {
    error = "process failed: " + std::system_category().message(static_cast<int>(system_error));
    return std::nullopt;
  }
  error.clear();
  return status;
#else
  pid_t process = 0;
  const int spawn_error = ::posix_spawnp(&process, arguments.front().c_str(),
                                         nullptr, nullptr, argv.data(), environ);
  if (spawn_error != 0) {
    error = "process start failed: " + std::string(std::strerror(spawn_error));
    return std::nullopt;
  }
  int status = 0;
  while (::waitpid(process, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    error = "process wait failed: " + std::string(std::strerror(errno));
    return std::nullopt;
  }
  error.clear();
  return WIFEXITED(status) ? WEXITSTATUS(status)
                                   : WIFSIGNALED(status)
                                         ? 128 + WTERMSIG(status)
                                         : status;
#endif
}
}
