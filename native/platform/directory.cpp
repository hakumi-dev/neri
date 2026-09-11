#include "neri/runtime_abi.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
extern "C" neri_int_v1 neri_rt_v1_file_directory_open(neri_int_v1, neri_int_v1 *, neri_int_v1 *os, neri_int_v1 *close_code) { if (os) *os = ERROR_NOT_SUPPORTED; if (close_code) *close_code = 0; return -2; }
extern "C" neri_int_v1 neri_rt_v1_file_directory_next(neri_int_v1, uint8_t *, neri_int_v1, neri_int_v1 *, neri_int_v1 *, neri_int_v1 *os) { if (os) *os = ERROR_NOT_SUPPORTED; return -1; }
extern "C" neri_int_v1 neri_rt_v1_file_directory_close(neri_int_v1, neri_int_v1 *os) { if (os) *os = ERROR_NOT_SUPPORTED; return -1; }
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr neri_int_v1 kind_regular = 1;
constexpr neri_int_v1 kind_directory = 2;
constexpr neri_int_v1 kind_symlink = 3;
constexpr neri_int_v1 kind_other = 4;
}

extern "C" neri_int_v1 neri_rt_v1_file_directory_open(
    neri_int_v1 parent, neri_int_v1 *token, neri_int_v1 *os_code, neri_int_v1 *close_code) {
  if (os_code) *os_code = EINVAL;
  if (close_code) *close_code = 0;
  if (parent < 0 || parent > INT_MAX || !token || !os_code || !close_code) return -1;
  const int descriptor = openat(static_cast<int>(parent), ".",
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) { *os_code = errno; return -1; }
  DIR *stream = fdopendir(descriptor);
  if (!stream) { *os_code = errno; if (close(descriptor) != 0) *close_code = errno; return -1; }
  *token = static_cast<neri_int_v1>(reinterpret_cast<intptr_t>(stream));
  *os_code = 0;
  return 0;
}

extern "C" neri_int_v1 neri_rt_v1_file_directory_next(
    neri_int_v1 token, uint8_t *name, neri_int_v1 capacity,
    neri_int_v1 *length, neri_int_v1 *kind, neri_int_v1 *os_code) {
  if (!token || !name || capacity <= 0 || !length || !kind || !os_code) return -1;
  DIR *stream = reinterpret_cast<DIR *>(static_cast<intptr_t>(token));
  while (true) {
    errno = 0;
    dirent *entry = readdir(stream);
    if (!entry) {
      *os_code = errno;
      return errno == 0 ? 1 : -1;
    }
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    const size_t count = std::strlen(entry->d_name);
    if (count > static_cast<uint64_t>(capacity)) { *os_code = ENAMETOOLONG; return -1; }
    struct stat metadata{};
    if (fstatat(dirfd(stream), entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
      *os_code = errno; return -1;
    }
    std::memcpy(name, entry->d_name, count);
    *length = static_cast<neri_int_v1>(count);
    *kind = S_ISREG(metadata.st_mode) ? kind_regular : S_ISDIR(metadata.st_mode) ? kind_directory :
        S_ISLNK(metadata.st_mode) ? kind_symlink : kind_other;
    *os_code = 0;
    return 0;
  }
}

extern "C" neri_int_v1 neri_rt_v1_file_directory_close(neri_int_v1 token,
                                                          neri_int_v1 *os_code) {
  if (!token || !os_code) return -1;
  if (closedir(reinterpret_cast<DIR *>(static_cast<intptr_t>(token))) != 0) {
    *os_code = errno;
    return -1;
  }
  *os_code = 0;
  return 0;
}
#endif
