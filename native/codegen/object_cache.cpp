#include "object_cache.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>
#include <string>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <crt_externs.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#else
#include <link.h>
#include <sys/auxv.h>
#include <sys/sysmacros.h>
extern char **environ;
#endif
#endif

namespace neri::codegen {
namespace {
void event(const char *value) {
  std::cerr << "neri codegen object-cache " << value << '\n';
}
#if defined(__APPLE__) || defined(__linux__)
struct fd final {
  int value;
  explicit fd(int v = -1) : value(v) {}
  ~fd() {
    if (value >= 0)
      close(value);
  }
  fd(const fd &) = delete;
  fd &operator=(const fd &) = delete;
};
void require(bool value) {
  if (!value)
    throw std::runtime_error("Cache unavailable");
}
std::string hex(std::span<const std::uint8_t> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto b : bytes) {
    result += digits[b >> 4];
    result += digits[b & 15];
  }
  return result;
}
void hash_field(llvm::SHA256 &hash, std::string_view value) {
  hash.update(std::to_string(value.size()));
  hash.update(":");
  hash.update(value);
}
std::string digest(std::span<const std::uint8_t> bytes) {
  return hex(llvm::SHA256::hash(
      llvm::ArrayRef<std::uint8_t>(bytes.data(), bytes.size())));
}
bool same(const struct stat &a, const struct stat &b) {
#if defined(__APPLE__)
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
         a.st_size == b.st_size &&
         a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
         a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
         a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec &&
         a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
#else
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
         a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
         a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
         a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
         a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
#endif
}
std::string file_digest(const std::string &path, const struct stat &expected) {
  fd file(open(path.c_str(), O_RDONLY | O_CLOEXEC));
  require(file.value >= 0);
  struct stat before{}, after{}, named{};
  require(fstat(file.value, &before) == 0 && S_ISREG(before.st_mode) &&
          same(before, expected));
#if defined(__APPLE__)
  CC_SHA256_CTX hash{};
  require(CC_SHA256_Init(&hash) == 1);
#else
  llvm::SHA256 hash;
#endif
  std::array<std::uint8_t, 65536> bytes;
  for (;;) {
    auto count = read(file.value, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    require(count >= 0);
    if (!count)
      break;
#if defined(__APPLE__)
    require(CC_SHA256_Update(&hash, bytes.data(),
                             static_cast<CC_LONG>(count)) == 1);
#else
    hash.update(llvm::ArrayRef<std::uint8_t>(bytes.data(),
                                             static_cast<std::size_t>(count)));
#endif
  }
  require(fstat(file.value, &after) == 0 && stat(path.c_str(), &named) == 0 &&
          same(before, after) && same(before, named));
#if defined(__APPLE__)
  std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> result;
  require(CC_SHA256_Final(result.data(), &hash) == 1);
  return hex(result);
#else
  return hex(hash.final());
#endif
}
void mapped_identity(std::uintptr_t address, const struct stat &expected) {
#if defined(__APPLE__)
  proc_regionwithpathinfo region{};
  require(proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO, address, &region,
                       sizeof(region)) == sizeof(region));
  const auto &identity = region.prp_vip.vip_vi.vi_stat;
  require(identity.vst_ino == expected.st_ino &&
          identity.vst_dev == static_cast<std::uint32_t>(expected.st_dev));
#else
  std::ifstream maps("/proc/self/maps");
  require(static_cast<bool>(maps));
  std::string line;
  while (std::getline(maps, line)) {
    unsigned long long begin = 0, end = 0, offset = 0, inode = 0;
    unsigned device_major = 0, device_minor = 0;
    char permissions[5]{};
    require(std::sscanf(line.c_str(), "%llx-%llx %4s %llx %x:%x %llu", &begin,
                        &end, permissions, &offset, &device_major,
                        &device_minor, &inode) == 7);
    if (address >= begin && address < end) {
      require(inode == expected.st_ino &&
              device_major == major(expected.st_dev) &&
              device_minor == minor(expected.st_dev));
      return;
    }
  }
  require(false);
#endif
}
void safe_environment() {
#if defined(__APPLE__)
  auto environment = *_NSGetEnviron();
#else
  auto environment = environ;
#endif
  for (auto p = environment; p && *p; ++p) {
    std::string_view v(*p);
    require(!v.starts_with("DYLD_") && !v.starts_with("LD_") &&
            (!v.starts_with("LLVM_") || v.starts_with("LLVM_PREFIX=")) &&
            !v.starts_with("GLIBC_TUNABLES=") && !v.starts_with("LIBPATH=") &&
            !v.starts_with("SHLIB_PATH="));
  }
}
#if defined(__APPLE__)
const dyld_all_image_infos *active_shared_cache() {
  task_dyld_info_data_t info{};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  require(task_info(mach_task_self(), TASK_DYLD_INFO,
                    reinterpret_cast<task_info_t>(&info),
                    &count) == KERN_SUCCESS);
  require(info.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64 &&
          info.all_image_info_addr != 0 &&
          info.all_image_info_size >=
              offsetof(dyld_all_image_infos, dyldPath) + sizeof(const char *));
  const auto *all =
      reinterpret_cast<const dyld_all_image_infos *>(info.all_image_info_addr);
  require(all->version >= 15 && !all->processDetachedFromSharedRegion);
  require(std::any_of(std::begin(all->sharedCacheUUID),
                      std::end(all->sharedCacheUUID),
                      [](auto b) { return b != 0; }));
  require(all->dyldPath && all->dyldPath[0] == '/' &&
          all->dyldImageLoadAddress);
  return all;
}
#endif
std::string closure() {
  safe_environment();
  struct image {
    std::string name;
    bool cached;
    std::uintptr_t address;
  };
  std::vector<image> images;
  std::string shared;
#if defined(__APPLE__)
  const auto *all = active_shared_cache();
  shared = hex(all->sharedCacheUUID);
  images.push_back(
      {all->dyldPath,
       (all->dyldImageLoadAddress->flags & MH_DYLIB_IN_CACHE) != 0,
       reinterpret_cast<std::uintptr_t>(all->dyldImageLoadAddress)});
  const auto image_count = _dyld_image_count();
  for (std::uint32_t i = 0; i < image_count; ++i) {
    const auto *name = _dyld_get_image_name(i);
    const auto *header = _dyld_get_image_header(i);
    require(name && header && name[0] == '/');
    images.push_back({name, (header->flags & MH_DYLIB_IN_CACHE) != 0,
                      reinterpret_cast<std::uintptr_t>(header)});
  }
  require(image_count == _dyld_image_count());
#else
  struct enumeration {
    std::vector<image> *images;
    bool valid = true;
    std::string vdso;
  } entries{&images, true, {}};
  dl_iterate_phdr(
      [](dl_phdr_info *info, std::size_t, void *context) {
        auto &e = *static_cast<enumeration *>(context);
        const std::string name = info->dlpi_name ? info->dlpi_name : "";
        if (name == "linux-vdso.so.1" &&
            info->dlpi_addr == getauxval(AT_SYSINFO_EHDR)) {
          llvm::SHA256 hash;
          bool found = false;
          for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
            const auto &segment = info->dlpi_phdr[i];
            if (segment.p_type != PT_LOAD)
              continue;
            if ((segment.p_flags & PF_R) == 0 ||
                segment.p_memsz > 1024U * 1024U) {
              e.valid = false;
              return 0;
            }
            hash.update(llvm::ArrayRef<std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(info->dlpi_addr +
                                                       segment.p_vaddr),
                segment.p_memsz));
            found = true;
          }
          e.valid = e.valid && found;
          e.vdso = hex(hash.final());
          return 0;
        }
        std::uintptr_t address = 0;
        for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
          if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            address = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
            break;
          }
        }
        if (address == 0)
          e.valid = false;
        if (name.empty())
          e.images->push_back({"/proc/self/exe", false, address});
        else if (name.front() == '/')
          e.images->push_back({name, false, address});
        else
          e.valid = false;
        return 0;
      },
      &entries);
  require(entries.valid);
  shared = entries.vdso;
#endif
  require(!images.empty());
  std::sort(images.begin(), images.end(),
            [](const auto &a, const auto &b) { return a.name < b.name; });
  images.erase(std::unique(images.begin(), images.end(),
                           [](const auto &a, const auto &b) {
                             return a.name == b.name && a.cached == b.cached &&
                                    a.address == b.address;
                           }),
               images.end());
  llvm::SHA256 hash;
  hash_field(hash, "neri-bootstrap-object-cache-v1");
  hash_field(hash, shared);
  for (const auto &[name, cached, address] : images) {
    hash_field(hash, name);
    struct stat st{};
    if (stat(name.c_str(), &st) == 0) {
      if (!cached)
        mapped_identity(address, st);
      hash_field(hash, file_digest(name, st));
    } else {
      require(cached && errno == ENOENT && !shared.empty());
      hash_field(hash, "dyld-shared-cache:" + shared);
    }
  }
  return hex(hash.final());
}
void private_directory(int descriptor) {
  struct stat st{};
  require(descriptor >= 0 && fstat(descriptor, &st) == 0 &&
          S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
          (st.st_mode & 0777) == 0700);
}
int open_root(const std::filesystem::path &path) {
  require(path.is_absolute());
  int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  require(current >= 0);
  try {
    const auto relative = path.relative_path();
    for (auto it = relative.begin(); it != relative.end(); ++it) {
      const auto name = it->string();
      require(name != ".." && name != "." && !name.empty());
      require(mkdirat(current, name.c_str(), 0700) == 0 || errno == EEXIST);
      const int next = openat(current, name.c_str(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      require(next >= 0);
      close(current);
      current = next;
    }
    private_directory(current);
    return current;
  } catch (...) {
    close(current);
    throw;
  }
}
std::vector<std::uint8_t> read_entry(int dir, const char *name,
                                     std::size_t limit) {
  fd file(openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  struct stat st{};
  require(file.value >= 0 && fstat(file.value, &st) == 0 &&
          S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_nlink == 1 &&
          (st.st_mode & 077) == 0 && st.st_size >= 0 &&
          static_cast<std::uint64_t>(st.st_size) <= limit);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
  std::size_t done = 0;
  while (done < bytes.size()) {
    auto n = read(file.value, bytes.data() + done, bytes.size() - done);
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    done += static_cast<std::size_t>(n);
  }
  struct stat after{};
  require(fstat(file.value, &after) == 0 && same(st, after));
  return bytes;
}
void write_entry(int dir, const char *name,
                 std::span<const std::uint8_t> bytes) {
  fd file(openat(dir, name,
                 O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  require(file.value >= 0);
  while (!bytes.empty()) {
    auto n = write(file.value, bytes.data(), bytes.size());
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    bytes = bytes.subspan(static_cast<std::size_t>(n));
  }
  require(fsync(file.value) == 0);
}
#endif
} // namespace
std::string cache_identity() {
#if defined(__APPLE__) || defined(__linux__)
  try {
    return closure();
  } catch (...) {
    return {};
  }
#else
  return {};
#endif
}

std::string shared_cache_identity(std::span<const std::string> paths) {
#if defined(__APPLE__)
  try {
    safe_environment();
    require(!paths.empty() && paths.size() <= 4096);
    std::size_t bytes = 0;
    for (const auto &path : paths) {
      require(!path.empty() && path.front() == '/' &&
              path.size() <= 1048576U - bytes &&
              std::none_of(path.begin(), path.end(),
                           [](unsigned char c) { return c < 32 || c == 127; }));
      bytes += path.size();
    }
    const auto shared = hex(active_shared_cache()->sharedCacheUUID);
    std::vector<std::string> names(paths.begin(), paths.end());
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    llvm::SHA256 hash;
    hash_field(hash, "neri-shared-cache-paths-v2");
    hash_field(hash, shared);
    for (const auto &path : names) {
      struct stat st{};
      const auto status = stat(path.c_str(), &st);
      // Membership is independent of an on-disk override. The caller binds
      // that file's contents or absence separately from the active cache UUID.
      require(((status == 0 && S_ISREG(st.st_mode)) ||
               (status != 0 && errno == ENOENT)) &&
              _dyld_shared_cache_contains_path(path.c_str()));
      hash_field(hash, path);
    }
    require(hex(active_shared_cache()->sharedCacheUUID) == shared);
    return hex(hash.final());
  } catch (...) {
    return {};
  }
#else
  static_cast<void>(paths);
  return {};
#endif
}

struct object_cache::state {
#if defined(__APPLE__) || defined(__linux__)
  fd root;
  fd lock;
  std::string dependency;
  std::string key;
  state(const std::filesystem::path &path)
      : root(open_root(path)),
        lock(openat(root.value, "lock",
                    O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)) {
    struct stat st{};
    require(lock.value >= 0 && fstat(lock.value, &st) == 0 &&
            S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_nlink == 1 &&
            (st.st_mode & 077) == 0 && flock(lock.value, LOCK_EX) == 0);
  }
#endif
};
object_cache::object_cache(const std::filesystem::path &path,
                           std::span<const std::uint8_t> input,
                           target_platform target,
                           optimization_mode optimization) {
  if (path.empty())
    return;
#if defined(__APPLE__) || defined(__linux__)
  try {
    auto value = std::make_unique<state>(path);
    value->dependency = closure();
    llvm::SHA256 hash;
    hash_field(hash, value->dependency);
    hash_field(hash, target_name(target));
    hash_field(hash, optimization_name(optimization));
    hash_field(hash, digest(input));
    value->key = hex(hash.final());
    state_ = std::move(value);
  } catch (...) {
    event("unavailable");
  }
#else
  static_cast<void>(input);
  static_cast<void>(target);
  static_cast<void>(optimization);
  event("unavailable");
#endif
}
object_cache::~object_cache() = default;
bool object_cache::restore(artifact &result) {
#if defined(__APPLE__) || defined(__linux__)
  if (!state_)
    return false;
  try {
    fd entry(openat(state_->root.value, state_->key.c_str(),
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (entry.value >= 0) {
      private_directory(entry.value);
      auto bytes = read_entry(entry.value, "object", 512U * 1024U * 1024U);
      auto receipt = read_entry(entry.value, "receipt", 256);
      const auto expected = state_->key + "\n" + digest(bytes) + "\n";
      require(std::string(receipt.begin(), receipt.end()) == expected &&
              !bytes.empty());
      require(closure() == state_->dependency);
      result = {std::move(bytes), false};
      event("hit");
      return true;
    }
  } catch (...) {
    // A corrupt entry is replaced only while holding the common writer lock.
  }
  try {
    require(closure() == state_->dependency);
  } catch (...) {
    state_.reset();
    event("unavailable");
    return false;
  }
  event("miss");
#else
  static_cast<void>(result);
#endif
  return false;
}
void object_cache::publish(const artifact &value) {
#if defined(__APPLE__) || defined(__linux__)
  if (!state_)
    return;
  std::string staging;
  try {
    require(closure() == state_->dependency && !value.text &&
            !value.bytes.empty());
    for (unsigned i = 0; i < 100; ++i) {
      staging = ".stage-" + std::to_string(getpid()) + "-" + std::to_string(i);
      if (mkdirat(state_->root.value, staging.c_str(), 0700) == 0)
        break;
      require(errno == EEXIST);
      staging.clear();
    }
    require(!staging.empty());
    fd dir(openat(state_->root.value, staging.c_str(),
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    private_directory(dir.value);
    write_entry(dir.value, "object", value.bytes);
    const auto receipt = state_->key + "\n" + digest(value.bytes) + "\n";
    write_entry(
        dir.value, "receipt",
        std::span(reinterpret_cast<const std::uint8_t *>(receipt.data()),
                  receipt.size()));
    require(fsync(dir.value) == 0);
    const auto stored = read_entry(dir.value, "object", 512U * 1024U * 1024U);
    const auto stored_receipt = read_entry(dir.value, "receipt", 256);
    require(digest(stored) == digest(value.bytes) &&
            std::string(stored_receipt.begin(), stored_receipt.end()) ==
                receipt &&
            closure() == state_->dependency);
    // Move an invalid entry aside, never recursively follow its contents.
    const auto old = staging + "-old";
    const int moved = renameat(state_->root.value, state_->key.c_str(),
                               state_->root.value, old.c_str());
    require(moved == 0 || errno == ENOENT);
    require(renameat(state_->root.value, staging.c_str(), state_->root.value,
                     state_->key.c_str()) == 0);
    staging.clear();
    if (moved == 0) {
      fd previous(openat(state_->root.value, old.c_str(),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      if (previous.value >= 0) {
        unlinkat(previous.value, "object", 0);
        unlinkat(previous.value, "receipt", 0);
        unlinkat(state_->root.value, old.c_str(), AT_REMOVEDIR);
      } else
        unlinkat(state_->root.value, old.c_str(), 0);
    }
    static_cast<void>(fsync(state_->root.value));
  } catch (...) {
    // Publication is optional; the emitted artifact remains valid.
  }
  if (!staging.empty()) {
    fd dir(openat(state_->root.value, staging.c_str(),
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dir.value >= 0) {
      unlinkat(dir.value, "object", 0);
      unlinkat(dir.value, "receipt", 0);
    }
    unlinkat(state_->root.value, staging.c_str(), AT_REMOVEDIR);
  }
#else
  static_cast<void>(value);
#endif
}
} // namespace neri::codegen
