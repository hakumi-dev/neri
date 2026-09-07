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
