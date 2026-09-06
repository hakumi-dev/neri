#include "../../native/platform/host.h"
#include "neri/host_path.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#include "../../native/platform/windows_support.h"
#endif

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct temporary_directory {
  std::filesystem::path path;
  temporary_directory() {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
      path = std::filesystem::temp_directory_path() /
          neri::host_path("neri plataforma espa\xc3\xb1ola " + std::to_string(seed) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(path)) return;
    }
    throw std::runtime_error("cannot create isolated platform test directory");
  }
  ~temporary_directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "cannot read platform test file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write(const std::filesystem::path &path, const std::string &contents, std::string &error) {
  return neri::platform::write_atomic(neri::path_text(path),
      reinterpret_cast<const uint8_t *>(contents.data()), contents.size(), error);
}

void test_files(const std::filesystem::path &directory) {
  const auto file = directory / neri::host_path("ni\xc3\xb1o.txt");
  std::string error = "stale error";
  require(write(file, "original", error) && error.empty(), "initial atomic write failed");
  const std::string replacement("replacement\0with binary bytes", 29);
  require(write(file, replacement, error) && error.empty(), "atomic replacement failed");
  require(read(file) == replacement, "atomic replacement changed contents");

  // A nonempty directory cannot be replaced by a file, even when run as root.
  const auto blocked = directory / "blocked";
  std::filesystem::create_directory(blocked);
  const auto original = blocked / "original";
  require(write(original, "keep me", error), "cannot prepare failed-write fixture");
  require(!write(blocked, "replacement", error) && !error.empty(), "invalid atomic replacement succeeded");
  require(read(original) == "keep me", "failed atomic replacement destroyed original");
  require(!write(file / "child", "replacement", error) && !error.empty(), "invalid temporary creation succeeded");
  require(read(file) == replacement, "failed temporary creation destroyed original");
  for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
    require(neri::path_text(entry.path()).find(".neri-") == std::string::npos, "failed write leaked temporary file");
  require(neri::platform::remove_file(neri::path_text(file), error) && error.empty(), "Unicode file removal failed");
  require(neri::platform::remove_file(neri::path_text(file), error) && error.empty(), "missing file removal is not idempotent");
}

void set_environment(const std::string &name, const char *value) {
#if defined(_WIN32)
  const auto wide_name = neri::windows::wide(name);
  const auto wide_value = value ? neri::windows::wide(value) : std::wstring();
  require(SetEnvironmentVariableW(wide_name.c_str(), value ? wide_value.c_str() : nullptr) != 0,
      "cannot set test environment variable");
#else
  require((value ? setenv(name.c_str(), value, 1) : unsetenv(name.c_str())) == 0,
      "cannot set test environment variable");
#endif
}

void test_environment() {
  const std::string name = "NERI_PLATFORM_TEST_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_\xc3\xb1";
  require(!neri::platform::environment(name), "missing environment variable has a value");
  set_environment(name, "");
  const auto empty = neri::platform::environment(name);
  require(empty && empty->empty(), "empty environment variable was treated as missing");
  const std::string value = "Espa\xc3\xb1" "a y caf\xc3\xa9";
  set_environment(name, value.c_str());
  require(neri::platform::environment(name) == value, "Unicode environment value changed");
  set_environment(name, nullptr);
  require(!neri::platform::environment(name), "deleted environment variable still exists");
}

void test_memory() {
  for (const size_t alignment : {size_t{1}, size_t{8}, size_t{64}, size_t{4096}}) {
    auto *memory = static_cast<unsigned char *>(neri::platform::allocate(8193, alignment, true));
    require(memory != nullptr, "aligned allocation failed");
    require(reinterpret_cast<uintptr_t>(memory) % alignment == 0, "allocation has wrong alignment");
    bool zeroed = true;
    for (size_t i = 0; i < 8193; ++i) zeroed = zeroed && memory[i] == 0;
    memory[0] = 42;
    memory[8192] = 17;
    neri::platform::deallocate(memory);
    require(zeroed, "zeroed allocation contains nonzero bytes");
    memory = static_cast<unsigned char *>(neri::platform::allocate(8193, alignment, false));
    require(memory != nullptr, "nonzeroed allocation failed");
    memory[8192] = 19;
    neri::platform::deallocate(memory);
  }
  neri::platform::deallocate(nullptr);
}

const std::vector<std::string> child_arguments = {
    "with spaces", "a\"quoted\"value", "trailing\\", "spaces and trailing\\",
    "\\\\server\\share\\", "slash\\\"quote", "", "$HOME", "$(echo expanded)",
    "%PATH%", "*", "semi;colon", "amp&ersand", "Espa\xc3\xb1" "a"};

void test_process(const std::filesystem::path &self, const std::filesystem::path &directory) {
#if defined(_WIN32)
  const auto child = directory / "child program.exe";
#else
  const auto child = directory / "child program";
#endif
  std::filesystem::copy_file(self, child);
  std::vector<std::string> arguments{neri::path_text(child), "--child"};
  arguments.insert(arguments.end(), child_arguments.begin(), child_arguments.end());
  std::string error = "stale error";
  require(neri::platform::run(arguments, error) == 7 && error.empty(), "process arguments or exit code changed");
  arguments = {neri::path_text(directory / "missing-program")};
  require(!neri::platform::run(arguments, error) && !error.empty(), "missing executable reported success");
}
}

int main(int argc, char **argv) {
  try {
#if defined(_WIN32)
    neri::windows::command_arguments native_arguments;
    argc = static_cast<int>(native_arguments.storage.size());
    argv = native_arguments.pointers.data();
    if (argc == 2 && std::string(argv[1]) == "--no-console") {
      require(GetConsoleWindow() == nullptr || !IsWindowVisible(GetConsoleWindow()),
          "background child opened a visible console");
      return 0;
    }
#endif
    if (argc >= 2 && std::string(argv[1]) == "--child") {
      require(static_cast<size_t>(argc) == child_arguments.size() + 2, "child argument count changed");
      for (size_t i = 0; i < child_arguments.size(); ++i)
        require(argv[i + 2] == child_arguments[i], "child argument bytes changed");
      return 7;
    }
    temporary_directory directory;
    test_files(directory.path);
    test_environment();
    test_memory();
    test_process(std::filesystem::absolute(neri::host_path(argv[0])), directory.path);
#if defined(_WIN32)
    // Model an IDE/pipe runner without opening even a temporary test console.
    FreeConsole();
    for (const bool supervise : {false, true}) {
      unsigned long status = 1, error = 0;
      require(neri::windows::run({neri::path_text(std::filesystem::absolute(
          neri::host_path(argv[0]))), "--no-console"}, status, error, supervise) && status == 0,
          "background subprocess must remain console-free");
    }
#endif
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Neri platform test failed: " << error.what() << '\n';
    return 1;
  }
}
