#include "../platform/windows_support.h"
#include "neri/host_path.h"
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  try {
    auto set_environment = [](const wchar_t *name, const std::wstring &value) {
      if (!SetEnvironmentVariableW(name, value.c_str()))
        throw std::runtime_error("Cannot configure toolchain environment: " + std::to_string(GetLastError()));
    };
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length == executable.size()) throw std::runtime_error("Cannot locate Neri installation");
    executable.resize(length);
    auto prefix = std::filesystem::path(executable).parent_path().parent_path();
    if (std::ifstream selection(prefix / "toolchain.txt", std::ios::binary); selection) {
      std::string name;
      std::getline(selection, name);
      if (!name.empty() && name.back() == '\r') name.pop_back();
      const std::string marker = "windows-x86_64-";
      if (!name.starts_with(marker) || name.size() != marker.size() + 64 ||
          name.find_first_not_of("0123456789abcdef", marker.size()) != std::string::npos)
        throw std::runtime_error("Invalid active toolchain; reinstall Neri");
      prefix /= std::filesystem::path("toolchains") / name;
    }
    std::ifstream config(prefix / "libexec/toolchain.env", std::ios::binary);
    if (!config) throw std::runtime_error("Missing libexec/toolchain.env; reinstall Neri");
    std::string line;
    while (std::getline(config, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const auto separator = line.find('=');
      if (separator == std::string::npos) continue;
      const auto name = line.substr(0, separator);
      const auto value = line.substr(separator + 1);
      if (name != "LLVM_PREFIX" && name != "LIB" && name != "INCLUDE") throw std::runtime_error("Invalid toolchain environment");
      set_environment(neri::windows::wide(name).c_str(), neri::windows::wide(value));
    }
    wchar_t llvm[32768];
    const auto llvm_size = GetEnvironmentVariableW(L"LLVM_PREFIX", llvm, 32768);
    if (!llvm_size || llvm_size >= 32768) throw std::runtime_error("Missing LLVM_PREFIX");
    const auto llvm_path = std::filesystem::path(llvm);
    const auto linker = llvm_path / "bin/clang++.exe";
    if (!std::filesystem::exists(linker)) throw std::runtime_error("Configured LLVM compiler is missing");
    auto configure = [&](const wchar_t *name, const std::filesystem::path &path) {
      set_environment(name, neri::windows::wide(neri::path_text(path)));
    };
    configure(L"NERI_STDLIB", prefix / "stdlib");
    configure(L"NERI_CODEGEN", prefix / "libexec/neri-codegen.exe");
    configure(L"NERI_HOST", prefix / "libexec/neri-host.exe");
    configure(L"NERI_RUNTIME_MANIFEST", prefix / "lib/neri-runtime-windows-x86_64.json");
    configure(L"NERI_LINKER", linker);
    // LLVM's sibling tools must be available even in a fresh, non-developer shell.
    std::wstring path(32768, L'\0');
    const auto path_size = GetEnvironmentVariableW(L"PATH", path.data(), static_cast<DWORD>(path.size()));
    if (path_size >= path.size()) throw std::runtime_error("PATH is too long");
    path.resize(path_size);
    path = (llvm_path / "bin").wstring() + L";" + path;
    set_environment(L"PATH", path);
    std::vector<std::string> arguments{neri::path_text(prefix / "libexec/neri-compiler.exe")};
    int count;
    auto **wide_arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide_arguments) throw std::runtime_error("Cannot read command line");
    for (int index = 1; index < count; ++index) arguments.push_back(neri::windows::utf8(wide_arguments[index]));
    LocalFree(wide_arguments);
    unsigned long status = 0, error = 0;
    if (!neri::windows::run(arguments, status, error, true)) throw std::runtime_error("Cannot start Neri: " + std::to_string(error));
    return static_cast<int>(status);
  } catch (const std::exception &error) {
    std::cerr << "neri: " << error.what() << '\n';
    return 1;
  }
}
