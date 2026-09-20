#include "neri/runtime_abi.h"

#include <array>
#include <cstring>
#include <string>

#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" neri_int_v1 neri_rt_v1_cache_supported(void) {
#if defined(__APPLE__) && defined(__aarch64__)
  return 1;
#else
  return 0;
#endif
}

extern "C" neri_int_v1 neri_rt_v1_cache_metadata(
    const uint8_t *path, neri_int_v1 length, neri_int_v1 follow,
    neri_int_v1 *kind, neri_int_v1 *permissions, neri_int_v1 *owned,
    uint8_t *fingerprint32) {
  if (!path || length <= 0 || length > 1048576 ||
      (follow != 0 && follow != 1) || !kind || !permissions || !owned ||
      !fingerprint32 || std::memchr(path, 0, static_cast<size_t>(length))) {
    return -1;
  }
  *kind = 0;
  *permissions = 0;
  *owned = 0;
  std::memset(fingerprint32, 0, 32);
#if defined(__APPLE__) && defined(__aarch64__)
  try {
    const std::string name(reinterpret_cast<const char *>(path),
                           static_cast<size_t>(length));
    struct stat metadata {};
    const int result = follow ? stat(name.c_str(), &metadata)
                              : lstat(name.c_str(), &metadata);
    if (result != 0) return -1;
    const std::array<uint64_t, 10> fields = {
        static_cast<uint64_t>(metadata.st_dev),
        static_cast<uint64_t>(metadata.st_ino),
        static_cast<uint64_t>(metadata.st_mode),
        static_cast<uint64_t>(metadata.st_uid),
        static_cast<uint64_t>(metadata.st_gid),
        static_cast<uint64_t>(metadata.st_mtimespec.tv_sec),
        static_cast<uint64_t>(metadata.st_mtimespec.tv_nsec),
        static_cast<uint64_t>(metadata.st_ctimespec.tv_sec),
        static_cast<uint64_t>(metadata.st_ctimespec.tv_nsec),
        static_cast<uint64_t>(metadata.st_size)};
    std::array<uint8_t, 80> serialized {};
    for (size_t field = 0; field < fields.size(); ++field) {
      for (size_t byte = 0; byte < sizeof(uint64_t); ++byte) {
        serialized[field * sizeof(uint64_t) + byte] =
            static_cast<uint8_t>(fields[field] >> (byte * 8));
      }
    }
    if (neri_rt_v1_crypto_sha256(serialized.data(), serialized.size(),
                               fingerprint32) != 0) return -1;
    *kind = S_ISREG(metadata.st_mode) ? 1 : S_ISDIR(metadata.st_mode) ? 2
          : S_ISLNK(metadata.st_mode) ? 3 : 4;
    *permissions = metadata.st_mode & 07777;
    *owned = metadata.st_uid == getuid() ? 1 : 0;
    return 0;
  } catch (...) {
    return -1;
  }
#else
  return -1;
#endif
}
