#include "neri/runtime_abi.h"
#include "neri/host_path.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#else
#include <dirent.h>
#include <poll.h>
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

neri_int_v1 neri_rt_v1_file_wait_readable(neri_int_v1 fd, neri_int_v1 milliseconds) {
  if (fd < 0 || fd > INT_MAX || milliseconds < 0 || milliseconds > INT_MAX) {
    errno = EINVAL; return -1;
  }
#if defined(_WIN32)
  const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(static_cast<int>(fd)));
  if (handle == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
  const auto type = GetFileType(handle);
  if (type == FILE_TYPE_DISK) return 1;
  if (type != FILE_TYPE_PIPE) { errno = EINVAL; return -1; }
  const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) return 1;
      errno = EIO; return -1;
    }
    if (available != 0) return 1;
    const auto now = GetTickCount64();
    if (now >= deadline) return 0;
    const auto remaining = deadline - now;
    Sleep(static_cast<DWORD>(remaining < 10 ? remaining : 10));
  }
#else
  pollfd descriptor{static_cast<int>(fd), POLLIN, 0};
  const int result = poll(&descriptor, 1, static_cast<int>(milliseconds));
  if (result < 0) return errno == EINTR ? -2 : -1;
  if (descriptor.revents & POLLNVAL) { errno = EBADF; return -1; }
  return result == 0 ? 0 : 1;
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

namespace {
constexpr neri_int_v1 remove_file_kind = 1;
constexpr neri_int_v1 remove_directory_kind = 2;
constexpr neri_int_v1 remove_tree_kind = 3;
constexpr neri_int_v1 status_missing = 0;
constexpr neri_int_v1 status_file = 1;
constexpr neri_int_v1 status_directory = 2;
constexpr neri_int_v1 status_symlink = 3;
constexpr neri_int_v1 status_other = 4;

bool valid_path(const uint8_t *path, neri_int_v1 length) {
  return path && length > 0 && length <= 1048576 &&
      !std::memchr(path, 0, static_cast<size_t>(length));
}

bool valid_removal_path(const uint8_t *path, neri_int_v1 length) {
  if (!valid_path(path, length)) return false;
  const std::string value(reinterpret_cast<const char *>(path),
                          static_cast<size_t>(length));
  if (value.back() == '/' || value.back() == '\\') return false;
  const size_t separator = value.find_last_of("/\\");
  const std::string component = value.substr(separator == std::string::npos ? 0 : separator + 1);
  return component != "." && component != "..";
}

std::string path_bytes(const uint8_t *path, neri_int_v1 length) {
  return {reinterpret_cast<const char *>(path), static_cast<size_t>(length)};
}

#if !defined(_WIN32)
int remove_tree_directory(int descriptor, int depth) {
  if (depth >= 256) {
    close(descriptor);
    errno = ELOOP;
    return -1;
  }
  DIR *stream = fdopendir(descriptor);
  if (!stream) {
    const int error = errno;
    close(descriptor);
    errno = error;
    return -1;
  }
  int result = 0;
  int error = 0;
  while (result == 0) {
    errno = 0;
    dirent *entry = readdir(stream);
    if (!entry) {
      if (errno != 0) {
        result = -1;
        error = errno;
      }
      break;
    }
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    struct stat metadata {};
    if (fstatat(dirfd(stream), entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
      result = -1;
      error = errno;
      break;
    }
    if (S_ISDIR(metadata.st_mode)) {
      const int child = openat(dirfd(stream), entry->d_name,
          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
      if (child < 0 || remove_tree_directory(child, depth + 1) != 0 ||
          unlinkat(dirfd(stream), entry->d_name, AT_REMOVEDIR) != 0) {
        result = -1;
        error = errno;
        break;
      }
    } else if (unlinkat(dirfd(stream), entry->d_name, 0) != 0) {
      result = -1;
      error = errno;
      break;
    }
  }
  if (closedir(stream) != 0 && result == 0) {
    result = -1;
    error = errno;
  }
  if (result != 0) errno = error;
  return result;
}

int make_directories(const std::string &path) {
  size_t begin = path.starts_with('/') ? 1 : 0;
  while (begin < path.size()) {
    const size_t end = path.find('/', begin);
    const std::string prefix = path.substr(0, end == std::string::npos ? path.size() : end);
    if (!prefix.empty() && mkdir(prefix.c_str(), 0700) != 0) {
      if (errno != EEXIST) return -1;
      struct stat metadata {};
      if (stat(prefix.c_str(), &metadata) != 0) return -1;
      if (!S_ISDIR(metadata.st_mode)) {
        errno = ENOTDIR;
        return -1;
      }
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return 0;
}
#endif
} // namespace

extern "C" neri_int_v1 neri_rt_v1_file_mutation_temp_directory(
    const uint8_t *prefix, neri_int_v1 prefix_length, uint8_t *output,
    neri_int_v1 capacity, neri_int_v1 *output_length, neri_int_v1 *os_code) {
  if (!valid_path(prefix, prefix_length) || !output || capacity <= 0 ||
      !output_length || !os_code) {
    if (os_code) *os_code = EINVAL;
    return -1;
  }
  const std::string label = path_bytes(prefix, prefix_length);
  if (label.find_first_of("/\\") != std::string::npos) {
    *os_code = EINVAL;
    return -1;
  }
#if defined(_WIN32)
  wchar_t directory[MAX_PATH + 1] {};
  const DWORD count = GetTempPathW(MAX_PATH, directory);
  if (count == 0 || count >= MAX_PATH) { *os_code = GetLastError(); return -1; }
  uint8_t random[16] {};
  for (int attempt = 0; attempt < 128; ++attempt) {
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
      *os_code = ERROR_GEN_FAILURE; return -1;
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string name = label;
    for (const auto byte : random) { name += hex[byte >> 4]; name += hex[byte & 15]; }
    const auto candidate = neri::host_path(neri::path_text(std::filesystem::path(directory)) + name);
    if (CreateDirectoryW(candidate.c_str(), nullptr)) {
      const auto text = neri::path_text(candidate);
      if (text.size() > static_cast<size_t>(capacity)) { RemoveDirectoryW(candidate.c_str()); *os_code = ENAMETOOLONG; return -1; }
      std::memcpy(output, text.data(), text.size()); *output_length = text.size(); *os_code = 0; return 0;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) { *os_code = GetLastError(); return -1; }
  }
  *os_code = ERROR_FILE_EXISTS; return -1;
#else
  std::string pattern;
  const char *temporary = std::getenv("TMPDIR");
  if (temporary && temporary[0] && temporary[0] != '/') {
    *os_code = EINVAL;
    return -1;
  }
  pattern = temporary && temporary[0] ? temporary : "/tmp";
  if (pattern.back() != '/') pattern += '/';
  pattern += label + "XXXXXX";
  std::string mutable_pattern = pattern;
  if (!mkdtemp(mutable_pattern.data())) {
    *os_code = errno;
    return -1;
  }
  if (mutable_pattern.size() > static_cast<size_t>(capacity)) {
    rmdir(mutable_pattern.c_str());
    *os_code = ENAMETOOLONG;
    return -1;
  }
  std::memcpy(output, mutable_pattern.data(), mutable_pattern.size());
  *output_length = mutable_pattern.size();
  *os_code = 0;
  return 0;
#endif
}

extern "C" neri_int_v1 neri_rt_v1_file_mutation_mkdir(const uint8_t *path,
    neri_int_v1 length, neri_int_v1 parents, neri_int_v1 *os_code) {
  if (!valid_path(path, length) || !os_code || (parents != 0 && parents != 1)) {
    if (os_code) *os_code = EINVAL; return -1;
  }
  try {
    const auto native = neri::host_path(path_bytes(path, length));
#if defined(_WIN32)
    const bool made = parents ? std::filesystem::create_directories(native) : std::filesystem::create_directory(native);
    if (!made && !(parents && std::filesystem::is_directory(native))) {
      *os_code = ERROR_ALREADY_EXISTS;
      return -1;
    }
#else
    const int status = parents ? make_directories(native) : mkdir(native.c_str(), 0700);
    if (status != 0) { *os_code = errno; return -1; }
#endif
    *os_code = 0; return 0;
  } catch (...) { *os_code = EINVAL; return -1; }
}

extern "C" neri_int_v1 neri_rt_v1_file_mutation_rename(const uint8_t *source,
    neri_int_v1 source_length, const uint8_t *destination,
    neri_int_v1 destination_length, neri_int_v1 *os_code) {
  if (!valid_path(source, source_length) || !valid_path(destination, destination_length) || !os_code) {
    if (os_code) *os_code = EINVAL; return -1;
  }
  try {
    const auto from = neri::host_path(path_bytes(source, source_length));
    const auto to = neri::host_path(path_bytes(destination, destination_length));
#if defined(_WIN32)
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) { *os_code = GetLastError(); return -1; }
#else
    if (::rename(from.c_str(), to.c_str()) != 0) { *os_code = errno; return -1; }
#endif
    *os_code = 0; return 0;
  } catch (...) { *os_code = EINVAL; return -1; }
}

extern "C" neri_int_v1 neri_rt_v1_file_mutation_remove(const uint8_t *path,
    neri_int_v1 length, neri_int_v1 kind, neri_int_v1 *os_code) {
  if (!valid_removal_path(path, length) || !os_code || kind < remove_file_kind || kind > remove_tree_kind) {
    if (os_code) *os_code = EINVAL; return -1;
  }
  try {
    const auto native = neri::host_path(path_bytes(path, length));
#if defined(_WIN32)
    std::error_code error;
    const auto state = std::filesystem::symlink_status(native, error);
    if (error) { *os_code = error.value(); return -1; }
    const bool directory = std::filesystem::is_directory(state) && !std::filesystem::is_symlink(state);
    bool removed = false;
    if (kind == remove_file_kind) removed = !directory && std::filesystem::remove(native, error);
    else if (kind == remove_directory_kind) removed = directory && std::filesystem::remove(native, error);
    else removed = std::filesystem::remove_all(native, error) != 0;
    if (!removed || error) { *os_code = error ? error.value() : ERROR_DIRECTORY; return -1; }
#else
    int status = -1;
    if (kind == remove_file_kind) status = unlink(native.c_str());
    else if (kind == remove_directory_kind) status = rmdir(native.c_str());
    else {
      struct stat metadata {};
      if (lstat(native.c_str(), &metadata) == 0) {
        if (S_ISDIR(metadata.st_mode)) {
          const int descriptor = open(native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
          if (descriptor >= 0 && remove_tree_directory(descriptor, 0) == 0) status = rmdir(native.c_str());
        } else status = unlink(native.c_str());
      }
    }
    if (status != 0) { *os_code = errno; return -1; }
#endif
    *os_code = 0; return 0;
  } catch (...) { *os_code = EINVAL; return -1; }
}

extern "C" neri_int_v1 neri_rt_v1_file_mutation_status(const uint8_t *path,
    neri_int_v1 length, neri_int_v1 *exists, neri_int_v1 *kind,
    neri_int_v1 *executable, neri_int_v1 *symlink, neri_int_v1 *os_code) {
  if (!valid_path(path, length) || !exists || !kind || !executable || !symlink || !os_code) {
    if (os_code) *os_code = EINVAL; return -1;
  }
  try {
    const auto native = neri::host_path(path_bytes(path, length));
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { *exists = 0; *kind = status_missing; *executable = 0; *symlink = 0; *os_code = 0; return 0; }
      *os_code = error; return -1;
    }
    *exists = 1; *symlink = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    *kind = *symlink ? status_symlink : (attributes & FILE_ATTRIBUTE_DIRECTORY) ? status_directory : status_file;
    const auto extension = native.extension().wstring();
    *executable = *kind == status_file && (extension == L".exe" || extension == L".com" || extension == L".bat" || extension == L".cmd");
#else
    struct stat metadata {};
    if (lstat(native.c_str(), &metadata) != 0) {
      if (errno == ENOENT || errno == ENOTDIR) { *exists = 0; *kind = status_missing; *executable = 0; *symlink = 0; *os_code = 0; return 0; }
      *os_code = errno; return -1;
    }
    *exists = 1; *symlink = S_ISLNK(metadata.st_mode); *executable = S_ISREG(metadata.st_mode) && (metadata.st_mode & 0111) != 0;
    *kind = S_ISREG(metadata.st_mode) ? status_file : S_ISDIR(metadata.st_mode) ? status_directory : S_ISLNK(metadata.st_mode) ? status_symlink : status_other;
#endif
    *os_code = 0; return 0;
  } catch (...) { *os_code = EINVAL; return -1; }
}

extern "C" neri_int_v1 neri_rt_v1_file_mutation_copy(const uint8_t *source,
    neri_int_v1 source_length, const uint8_t *destination,
    neri_int_v1 destination_length, neri_int_v1 *os_code) {
  if (!valid_path(source, source_length) || !valid_path(destination, destination_length) || !os_code) {
    if (os_code) *os_code = EINVAL; return -1;
  }
  try {
    const auto from = neri::host_path(path_bytes(source, source_length));
    const auto to = neri::host_path(path_bytes(destination, destination_length));
#if defined(_WIN32)
    const auto source_attributes = GetFileAttributesW(from.c_str());
    if (source_attributes == INVALID_FILE_ATTRIBUTES) { *os_code = GetLastError(); return -1; }
    if ((source_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { *os_code = ERROR_NOT_SUPPORTED; return -1; }
    if (!CopyFileW(from.c_str(), to.c_str(), TRUE)) { *os_code = GetLastError(); return -1; }
#else
    const int input = open(from.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (input < 0) { *os_code = errno; return -1; }
    struct stat metadata {};
    const int metadata_status = fstat(input, &metadata);
    if (metadata_status != 0 || !S_ISREG(metadata.st_mode)) {
      const int error = metadata_status != 0 ? errno : EINVAL;
      close(input);
      *os_code = error;
      return -1;
    }
    const int output = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, metadata.st_mode & 0777);
    if (output < 0) { const int error = errno; close(input); *os_code = error; return -1; }
    uint8_t buffer[16384];
    int failure = 0;
    while (!failure) {
      const ssize_t read_count = read(input, buffer, sizeof(buffer));
      if (read_count == 0) break;
      if (read_count < 0) {
        if (errno == EINTR) continue;
        failure = errno;
        break;
      }
      ssize_t offset = 0;
      while (offset < read_count) {
        const ssize_t written = write(output, buffer + offset,
                                      static_cast<size_t>(read_count - offset));
        if (written > 0) {
          offset += written;
        } else if (written < 0 && errno == EINTR) {
          continue;
        } else {
          failure = written == 0 ? EIO : errno;
          break;
        }
      }
    }
    if (close(input) != 0 && !failure) failure = errno;
    if (close(output) != 0 && !failure) failure = errno;
    if (failure) {
      unlink(to.c_str());
      *os_code = failure;
      return -1;
    }
#endif
    *os_code = 0; return 0;
  } catch (...) { *os_code = EINVAL; return -1; }
}
