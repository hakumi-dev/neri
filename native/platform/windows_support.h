#ifndef NERI_WINDOWS_SUPPORT_H
#define NERI_WINDOWS_SUPPORT_H
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace neri::windows {
inline std::wstring wide(std::string_view text) {
  if (text.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!count) throw std::runtime_error("Invalid UTF-8");
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
  return result;
}
inline std::string utf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (!count) throw std::runtime_error("Invalid UTF-16");
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
  return result;
}
struct command_arguments {
  std::vector<std::string> storage;
  std::vector<char *> pointers;
  command_arguments() {
    int count = 0;
    auto **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) throw std::runtime_error("Cannot read Windows command line");
    for (int index = 0; index < count; ++index) storage.push_back(utf8(arguments[index]));
    LocalFree(arguments);
    for (auto &argument : storage) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
  }
};
// The Windows C runtime doubles backslashes before quotes and at the end.
inline std::wstring quote(std::wstring_view value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
    if (c == L'"') result += L'\\';
    result += c;
    slashes = 0;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}
inline bool run(const std::vector<std::string> &arguments, unsigned long &status, unsigned long &error, bool supervise = false) {
  std::wstring command;
  for (const auto &argument : arguments) {
    if (!command.empty()) command += L' ';
    command += quote(wide(argument));
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  HANDLE job = nullptr;
  if (supervise) {
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job) { error = GetLastError(); return false; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
      error = GetLastError(); CloseHandle(job); return false;
    }
  }
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, supervise ? CREATE_SUSPENDED : 0, nullptr, nullptr, &startup, &process)) {
    error = GetLastError(); if (job) CloseHandle(job); return false;
  }
  if (supervise && (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == static_cast<DWORD>(-1))) {
    error = GetLastError();
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
    return false;
  }
  CloseHandle(process.hThread);
  const bool succeeded = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &status);
  error = succeeded ? 0 : GetLastError();
  CloseHandle(process.hProcess);
  if (job) CloseHandle(job);
  return succeeded;
}
}
#endif
