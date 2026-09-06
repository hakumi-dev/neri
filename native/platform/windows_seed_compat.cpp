// Compatibility kept solely at the platform boundary for legacy seed imports.
// Darwin cache layouts must never be emulated on Windows: report unsupported.
#include "windows_support.h"
#include <bcrypt.h>
#include <cerrno>
#include <cstdint>
#include <cstring>

extern "C" int64_t uname(void *) { return -1; }
extern "C" int64_t lstat(const char *, void *) { return -1; }
extern "C" int64_t getuid(void) { return -1; }
extern "C" char *mkdtemp(char *pattern) {
  const size_t size = std::strlen(pattern);
  if (size < 6 || std::strcmp(pattern + size - 6, "XXXXXX")) { errno = EINVAL; return nullptr; }
  for (int attempt = 0; attempt < 100; ++attempt) {
    unsigned char random[6];
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) { errno = EIO; return nullptr; }
    constexpr char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < 6; ++i) pattern[size - 6 + i] = alphabet[random[i] % 62];
    if (CreateDirectoryW(neri::windows::wide(pattern).c_str(), nullptr)) return pattern;
    if (GetLastError() != ERROR_ALREADY_EXISTS) { errno = EIO; return nullptr; }
  }
  errno = EEXIST;
  return nullptr;
}
