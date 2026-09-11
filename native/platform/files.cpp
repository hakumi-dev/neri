#include "neri/runtime_abi.h"
#include "neri/host_path.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

neri_int_v1 neri_rt_v1_file_open(const uint8_t *path, neri_int_v1 length) {
  if (length <= 0 || length > 1048576 || std::memchr(path, 0, static_cast<size_t>(length))) {
    errno = EINVAL; return -1;
  }
  try {
    const auto native = neri::host_path(std::string(reinterpret_cast<const char *>(path), static_cast<size_t>(length)));
#if defined(_WIN32)
    return _wopen(native.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#else
    return open(native.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
#endif
  } catch (...) { errno = EINVAL; return -1; }
}

neri_int_v1 neri_rt_v1_file_size(neri_int_v1 fd) {
  if (fd < 0 || fd > INT_MAX) { errno = EBADF; return -1; }
#if defined(_WIN32)
  struct _stat64 info {};
  if (_fstat64(static_cast<int>(fd), &info) != 0) return -1;
  if ((info.st_mode & _S_IFMT) != _S_IFREG) return -2;
#else
  struct stat info {};
  if (fstat(static_cast<int>(fd), &info) != 0) return -1;
  if (!S_ISREG(info.st_mode)) return -2;
#endif
  return info.st_size;
}

neri_int_v1 neri_rt_v1_file_read(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length) {
  if (fd < 0 || fd > INT_MAX || length < 0 || length > INT_MAX) { errno = EINVAL; return -1; }
#if defined(_WIN32)
  const auto count = _read(static_cast<int>(fd), bytes, static_cast<unsigned int>(length));
#else
  const auto count = read(static_cast<int>(fd), bytes, static_cast<size_t>(length));
#endif
  return count < 0 && errno == EINTR ? -2 : count;
}

neri_int_v1 neri_rt_v1_file_close(neri_int_v1 fd) {
  if (fd < 0 || fd > INT_MAX) { errno = EBADF; return -1; }
#if defined(_WIN32)
  return _close(static_cast<int>(fd));
#else
  return close(static_cast<int>(fd));
#endif
}

neri_int_v1 neri_rt_v1_file_error(void) { return errno; }

namespace {
constexpr neri_int_v1 root_missing = 1;
constexpr neri_int_v1 root_denied = 2;
constexpr neri_int_v1 root_symlink = 3;
constexpr neri_int_v1 root_wrong_type = 4;
constexpr neri_int_v1 root_other = 5;
[[maybe_unused]] constexpr neri_int_v1 root_unavailable = 6;

neri_int_v1 open_category(int error) {
  if (error == ENOENT) return root_missing;
  if (error == EACCES || error == EPERM) return root_denied;
  if (error == ELOOP) return root_symlink;
  if (error == ENOTDIR || error == EISDIR) return root_wrong_type;
  return root_other;
}

neri_int_v1 rooted_failure(int error, neri_int_v1 *os_code,
                           neri_int_v1 *category) {
  *os_code = error;
  *category = open_category(error);
  return -1;
}

bool valid_root_arguments(const uint8_t *path, neri_int_v1 length,
                          neri_int_v1 *os_code, neri_int_v1 *category) {
  if (!path || !os_code || !category || length <= 0 || length > 1048576 ||
      std::memchr(path, 0, static_cast<size_t>(length))) {
    if (os_code) *os_code = EINVAL;
    if (category) *category = root_other;
    return false;
  }
  *os_code = 0;
  *category = 0;
  return true;
}
} // namespace

extern "C" neri_int_v1 neri_rt_v1_file_root_open(const uint8_t *path,
                                       neri_int_v1 length,
                                       neri_int_v1 *os_code,
                                       neri_int_v1 *category) {
  if (!valid_root_arguments(path, length, os_code, category)) return -1;
#if defined(_WIN32)
  *os_code = ENOTSUP;
  *category = root_unavailable;
  return -1;
#else
  const std::string native(reinterpret_cast<const char *>(path),
                           static_cast<size_t>(length));
  const int fd = open(native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                      O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return rooted_failure(errno, os_code, category);
  return fd;
#endif
}

extern "C" neri_int_v1 neri_rt_v1_file_root_open_at(neri_int_v1 parent,
                                          const uint8_t *name,
                                          neri_int_v1 length,
                                          neri_int_v1 kind,
                                          neri_int_v1 *os_code,
                                          neri_int_v1 *category) {
  if (!valid_root_arguments(name, length, os_code, category) || parent < 0 ||
      parent > INT_MAX || (kind != 1 && kind != 2)) {
    if (os_code) *os_code = EINVAL;
    if (category) *category = root_other;
    return -1;
  }
#if defined(_WIN32)
  *os_code = ENOTSUP;
  *category = root_unavailable;
  return -1;
#else
  const std::string component(reinterpret_cast<const char *>(name),
                              static_cast<size_t>(length));
  int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
  if (kind == 1) flags |= O_DIRECTORY;
  const int fd = openat(static_cast<int>(parent), component.c_str(), flags);
  if (fd < 0) return rooted_failure(errno, os_code, category);
  return fd;
#endif
}
