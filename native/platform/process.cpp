#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "neri/runtime_abi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include "windows_support.h"
#include <windows.h>
#else
#include "posix_launch.h"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace {
constexpr uint64_t maximum_capture = 134217728;

struct parser {
  const uint8_t *data;
  size_t size;
  size_t offset = 0;
  bool byte(uint8_t &value) {
    if (offset == size) return false;
    value = data[offset++];
    return true;
  }
  bool u32(uint32_t &value) {
    if (size - offset < 4) return false;
    value = static_cast<uint32_t>(data[offset]) |
        static_cast<uint32_t>(data[offset + 1]) << 8 |
        static_cast<uint32_t>(data[offset + 2]) << 16 |
        static_cast<uint32_t>(data[offset + 3]) << 24;
    offset += 4;
    return true;
  }
  bool text(std::string &value) {
    uint32_t length = 0;
    if (!u32(length) || length > size - offset) return false;
    value.assign(reinterpret_cast<const char *>(data + offset), length);
    offset += length;
    return value.find('\0') == std::string::npos;
  }
};

struct configuration {
  bool inherit_environment = true;
  uint32_t stdout_limit = 0;
  uint32_t stderr_limit = 0;
  std::string directory;
  std::vector<std::string> arguments;
  std::vector<std::pair<std::string, std::string>> environment;
  std::vector<uint8_t> input;
  bool terminal = false;
};

bool decode(const uint8_t *data, int64_t length, configuration &value, const char *magic) {
  if (!data || length < 0 || length > 1048576) return false;
  parser input{data, static_cast<size_t>(length)};
  if (input.size < 4 || std::memcmp(input.data, magic, 4) != 0) return false;
  input.offset = 4;
  uint8_t inherit = 0;
  uint32_t argc = 0, envc = 0;
  if (!input.byte(inherit) || inherit > 1 || !input.u32(value.stdout_limit) ||
      !input.u32(value.stderr_limit) || value.stdout_limit > maximum_capture ||
      value.stderr_limit > maximum_capture || !input.text(value.directory) ||
      !input.u32(argc) || argc == 0 || argc > 1024) return false;
  value.inherit_environment = inherit != 0;
  for (uint32_t index = 0; index < argc; ++index) {
    std::string item;
    if (!input.text(item) || (index == 0 && item.empty())) return false;
    value.arguments.push_back(std::move(item));
  }
  if (!input.u32(envc) || envc > 1024) return false;
  for (uint32_t index = 0; index < envc; ++index) {
    std::string name, item;
    if (!input.text(name) || !input.text(item) || name.empty() ||
        name.find('=') != std::string::npos) return false;
    value.environment.emplace_back(std::move(name), std::move(item));
  }
  if (std::memcmp(magic, "NPR2", 4) == 0) {
    uint32_t input_length = 0;
    uint8_t terminal = 0;
    if (!input.u32(input_length) || input_length > input.size - input.offset) return false;
    value.input.assign(input.data + input.offset, input.data + input.offset + input_length);
    input.offset += input_length;
    if (!input.byte(terminal) || terminal > 1 || (terminal != 0 && input_length != 0)) return false;
    value.terminal = terminal != 0;
  }
  return input.offset == input.size;
}

struct child {
  std::mutex lock;
  std::vector<uint8_t> output;
  std::vector<uint8_t> errors;
  size_t output_limit = 0;
  size_t error_limit = 0;
  bool output_truncated = false;
  bool error_truncated = false;
  bool running = true;
  bool cancelled = false;
  std::atomic<bool> stop_readers{false};
  std::atomic<bool> stop_writer{false};
  std::atomic<bool> writer_done{false};
  int64_t exit_kind = 0;
  int64_t exit_value = 0;
  std::thread output_reader;
  std::thread error_reader;
  std::thread input_writer;
  std::vector<uint8_t> input;
#if defined(_WIN32)
  HANDLE process = NULL;
  HANDLE job = NULL;
  HANDLE output_pipe = NULL;
  HANDLE error_pipe = NULL;
  HANDLE input_pipe = NULL;
  DWORD process_group = 0;
#else
  pid_t process = 0;
  int output_pipe = -1;
  int error_pipe = -1;
  int input_pipe = -1;
#endif
};

void append(child &process, bool error, const uint8_t *bytes, size_t count) {
  std::lock_guard guard(process.lock);
  auto &target = error ? process.errors : process.output;
  const size_t limit = error ? process.error_limit : process.output_limit;
  bool &truncated = error ? process.error_truncated : process.output_truncated;
  const size_t retained = std::min(count, limit - target.size());
  target.insert(target.end(), bytes, bytes + retained);
  truncated = truncated || retained != count;
}

void mark_reader_stopped(child &process, bool error) {
  std::lock_guard guard(process.lock);
  (error ? process.error_truncated : process.output_truncated) = true;
}

#if defined(_WIN32)
void drain(const std::shared_ptr<child> &process, bool error, HANDLE pipe) {
  uint8_t bytes[8192];
  size_t stopped_reads = 0;
  while (true) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
    if (available == 0) {
      if (process->stop_readers.load()) break;
      Sleep(5);
      continue;
    }
    DWORD count = 0;
    if (!ReadFile(pipe, bytes, static_cast<DWORD>(std::min<size_t>(sizeof(bytes), available)), &count, nullptr) || count == 0) break;
    append(*process, error, bytes, count);
    if (process->stop_readers.load() && ++stopped_reads == 64) {
      mark_reader_stopped(*process, error); break;
    }
  }
  CloseHandle(pipe);
}

void write_input(const std::shared_ptr<child> &process, HANDLE pipe) {
  size_t offset = 0;
  while (offset < process->input.size() && !process->stop_writer.load()) {
    DWORD count = 0;
    const DWORD requested = static_cast<DWORD>(std::min<size_t>(8192, process->input.size() - offset));
    if (!WriteFile(pipe, process->input.data() + offset, requested, &count, nullptr) || count == 0) break;
    offset += count;
  }
  CloseHandle(pipe);
  process->writer_done.store(true);
}
#else
void drain(const std::shared_ptr<child> &process, bool error, int descriptor) {
  uint8_t bytes[8192];
  size_t stopped_reads = 0;
  while (true) {
    const ssize_t count = read(descriptor, bytes, sizeof(bytes));
    if (count > 0) {
      append(*process, error, bytes, static_cast<size_t>(count));
      if (process->stop_readers.load() && ++stopped_reads == 64) {
        mark_reader_stopped(*process, error); break;
      }
    }
    else if (count < 0 && errno == EINTR) continue;
    else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (process->stop_readers.load()) break;
      pollfd waiting{descriptor, POLLIN | POLLHUP, 0};
      poll(&waiting, 1, 10);
    } else break;
  }
  close(descriptor);
}

void write_input(const std::shared_ptr<child> &process, int descriptor) {
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
  size_t offset = 0;
  while (offset < process->input.size() && !process->stop_writer.load()) {
    const ssize_t count = write(descriptor, process->input.data() + offset,
                                process->input.size() - offset);
    if (count > 0) offset += static_cast<size_t>(count);
    else if (count < 0 && errno == EINTR) continue;
    else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd waiting{descriptor, POLLOUT | POLLHUP, 0};
      poll(&waiting, 1, 10);
    } else break;
  }
  close(descriptor);
  process->writer_done.store(true);
}
#endif

std::mutex registry_lock;
std::unordered_map<int64_t, std::shared_ptr<child>> registry;
std::atomic<uint64_t> next_token{1};

std::shared_ptr<child> find_child(int64_t token) {
  std::lock_guard guard(registry_lock);
  const auto found = registry.find(token);
  return found == registry.end() ? nullptr : found->second;
}

#if defined(_WIN32)
std::wstring quote(const std::wstring &argument) {
  if (argument.empty()) return L"\"\"";
  if (argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (const wchar_t value : argument) {
    if (value == L'\\') { ++slashes; continue; }
    if (value == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result += value;
  }
  result.append(slashes * 2, L'\\');
  return result + L'\"';
}

bool spawn_child(const configuration &config, const std::shared_ptr<child> &result, int64_t &os_code) {
  os_code = 0;
  SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
  HANDLE input_read = NULL, input_write = NULL, output_read = NULL, output_write = NULL, error_read = NULL, error_write = NULL;
  PROCESS_INFORMATION process{};
  HANDLE inherited_handles[3] = {NULL, NULL, NULL};
  SIZE_T attribute_size = 0;
  std::vector<uint8_t> attribute_storage;
  STARTUPINFOEXW startup{};
  std::wstring command, executable, directory, environment_block;
  const auto remember_error = [&os_code]() { os_code = static_cast<int64_t>(GetLastError()); };
  struct insensitive {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
      return _wcsicmp(left.c_str(), right.c_str()) < 0;
    }
  };
  std::map<std::wstring, std::wstring, insensitive> environment;
  if (config.terminal) { os_code = ERROR_NOT_SUPPORTED; goto failure; }
  if (!CreatePipe(&input_read, &input_write, &inherited, 0) ||
      !CreatePipe(&output_read, &output_write, &inherited, 0) ||
      !CreatePipe(&error_read, &error_write, &inherited, 0) ||
      !SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0)) { remember_error(); goto failure; }
  result->job = CreateJobObjectW(nullptr, nullptr);
  if (!result->job) { remember_error(); goto failure; }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(result->job, JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits))) { remember_error(); goto failure; }
  if (config.inherit_environment) {
    LPWCH inherited_environment = GetEnvironmentStringsW();
    if (!inherited_environment) { remember_error(); goto failure; }
    for (const wchar_t *item = inherited_environment; *item;) {
      const std::wstring entry(item);
      const size_t separator = entry.find(L'=', entry[0] == L'=' ? 1 : 0);
      if (separator != std::wstring::npos)
        environment[entry.substr(0, separator)] = entry.substr(separator + 1);
      item += entry.size() + 1;
    }
    FreeEnvironmentStringsW(inherited_environment);
  }
  for (const auto &[name, value] : config.environment)
    environment[neri::windows::wide(name)] = neri::windows::wide(value);
  for (const auto &[name, value] : environment) {
    environment_block += name; environment_block += L'=';
    environment_block += value; environment_block += L'\0';
  }
  if (environment_block.empty()) environment_block += L'\0';
  environment_block += L'\0';
  for (const auto &argument : config.arguments) {
    if (!command.empty()) command += L' ';
    command += quote(neri::windows::wide(argument));
  }
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input_read;
  startup.StartupInfo.hStdOutput = output_write;
  startup.StartupInfo.hStdError = error_write;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
  attribute_storage.resize(attribute_size);
  startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) { remember_error(); goto failure; }
  inherited_handles[0] = input_read;
  inherited_handles[1] = output_write;
  inherited_handles[2] = error_write;
  if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited_handles, sizeof(inherited_handles), nullptr, nullptr)) { remember_error(); goto failure; }
  executable = neri::windows::wide(config.arguments.front());
  directory = config.directory.empty() ? std::wstring() : neri::windows::wide(config.directory);
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                      CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | EXTENDED_STARTUPINFO_PRESENT |
                          CREATE_UNICODE_ENVIRONMENT,
                      environment_block.data(), directory.empty() ? nullptr : directory.c_str(),
                      &startup.StartupInfo, &process)) { remember_error(); goto failure; }
  result->process = process.hProcess;
  result->process_group = process.dwProcessId;
  if (!AssignProcessToJobObject(result->job, result->process)) {
    remember_error();
    TerminateProcess(result->process, 1);
    CloseHandle(process.hThread);
    goto failure;
  }
  if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    remember_error();
    TerminateJobObject(result->job, 1);
    CloseHandle(process.hThread);
    goto failure;
  }
  CloseHandle(process.hThread);
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  startup.lpAttributeList = nullptr;
  CloseHandle(input_read); CloseHandle(output_write); CloseHandle(error_write);
  result->input_pipe = input_write; result->output_pipe = output_read; result->error_pipe = error_read;
  return true;
failure:
  if (startup.lpAttributeList) DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (input_read) CloseHandle(input_read); if (input_write) CloseHandle(input_write);
  if (output_read) CloseHandle(output_read); if (output_write) CloseHandle(output_write);
  if (error_read) CloseHandle(error_read); if (error_write) CloseHandle(error_write);
  if (result->process) CloseHandle(result->process);
  if (result->job) CloseHandle(result->job);
  result->process = NULL; result->job = NULL;
  return false;
}
#else
bool spawn_child(const configuration &config, const std::shared_ptr<child> &result, int64_t &os_code) {
  int input[2] = {-1, -1}, output[2] = {-1, -1}, errors[2] = {-1, -1};
  std::map<std::string, std::string> environment_values;
  std::vector<std::string> environment_storage;
  int launch_error = 0;
  int nonblocking_error = 0;
  const auto make_nonblocking = [](int descriptor, int &error) {
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
      error = errno;
      return false;
    }
    return true;
  };
  if (config.inherit_environment)
    for (char **item = environ; *item; ++item) {
      const std::string entry(*item);
      const auto separator = entry.find('=');
      if (separator != std::string::npos) environment_values[entry.substr(0, separator)] = entry.substr(separator + 1);
    }
  for (const auto &[name, value] : config.environment) environment_values[name] = value;
  neri::platform::posix_launch_options options;
  options.arguments = config.arguments;
  options.working_directory = config.directory;
  options.process_group = true;
  for (const auto &[name, value] : environment_values) environment_storage.push_back(name + "=" + value);
  options.environment = std::move(environment_storage);
  if (config.terminal) {
    int master = -1, slave = -1;
    if (!neri::platform::posix_terminal_pair(master, slave, launch_error)) {
      os_code = launch_error;
      return false;
    }
    options.standard_input = options.standard_output = options.standard_error = slave;
    options.controlling_terminal = true;
    if (!make_nonblocking(master, launch_error) ||
        !neri::platform::posix_launch(options, result->process, launch_error)) {
      close(master);
      close(slave);
      os_code = launch_error;
      return false;
    }
    close(slave);
    result->output_pipe = master;
    return true;
  }
  if (!neri::platform::posix_pipe_cloexec(input, launch_error) ||
      !neri::platform::posix_pipe_cloexec(output, launch_error) ||
      !neri::platform::posix_pipe_cloexec(errors, launch_error)) goto failure;
  options.standard_input = input[0];
  options.standard_output = output[1];
  options.standard_error = errors[1];
  if (!neri::platform::posix_launch(options, result->process, launch_error)) goto failure;
  close(input[0]); input[0] = -1; options.standard_input = -1;
  close(output[1]); output[1] = -1; close(errors[1]); errors[1] = -1;
  result->input_pipe = input[1]; result->output_pipe = output[0]; result->error_pipe = errors[0];
#if defined(__APPLE__)
  if (fcntl(result->input_pipe, F_SETNOSIGPIPE, 1) < 0) {
    nonblocking_error = errno;
    close(result->input_pipe); close(result->output_pipe); close(result->error_pipe);
    kill(-result->process, SIGKILL);
    while (waitpid(result->process, nullptr, 0) < 0 && errno == EINTR) {}
    result->process = 0; result->input_pipe = result->output_pipe = result->error_pipe = -1;
    os_code = nonblocking_error;
    return false;
  }
#endif
  if (!make_nonblocking(result->input_pipe, nonblocking_error) ||
      !make_nonblocking(result->output_pipe, nonblocking_error) ||
      !make_nonblocking(result->error_pipe, nonblocking_error)) {
    close(result->input_pipe); close(result->output_pipe); close(result->error_pipe);
    kill(-result->process, SIGKILL);
    while (waitpid(result->process, nullptr, 0) < 0 && errno == EINTR) {}
    result->process = 0; result->input_pipe = result->output_pipe = result->error_pipe = -1;
    os_code = nonblocking_error;
    return false;
  }
  return true;
failure:
  os_code = launch_error != 0 ? launch_error : errno;
  for (int descriptor : input) if (descriptor >= 0) close(descriptor);
  for (int descriptor : output) if (descriptor >= 0) close(descriptor);
  for (int descriptor : errors) if (descriptor >= 0) close(descriptor);
  return false;
}
#endif

void join_readers(child &process) {
  if (process.input_writer.joinable()) {
#if defined(_WIN32)
    while (process.stop_writer.load() && !process.writer_done.load()) {
      CancelSynchronousIo(process.input_writer.native_handle());
      Sleep(1);
    }
#endif
    process.input_writer.join();
  }
  if (process.output_reader.joinable()) process.output_reader.join();
  if (process.error_reader.joinable()) process.error_reader.join();
}

bool refresh(const std::shared_ptr<child> &process, int64_t &os_code) {
  std::lock_guard guard(process->lock);
  if (!process->running) return true;
#if defined(_WIN32)
  const DWORD waited = WaitForSingleObject(process->process, 0);
  if (waited == WAIT_TIMEOUT) return true;
  if (waited != WAIT_OBJECT_0) { os_code = GetLastError(); return false; }
  DWORD code = 0;
  if (!GetExitCodeProcess(process->process, &code)) { os_code = GetLastError(); return false; }
  TerminateJobObject(process->job, code);
  process->exit_kind = process->cancelled ? 3 : 1;
  process->exit_value = code;
#else
  int status = 0;
  const pid_t waited = waitpid(process->process, &status, WNOHANG);
  if (waited == 0) return true;
  if (waited < 0) { if (errno == EINTR) return true; os_code = errno; return false; }
  kill(-process->process, SIGKILL);
  process->exit_kind = process->cancelled ? 3 : WIFEXITED(status) ? 1 : 2;
  process->exit_value = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
#endif
  process->running = false;
  process->stop_writer.store(true);
  process->stop_readers.store(true);
  return true;
}
}

int64_t spawn_process(const uint8_t *config, int64_t length, int64_t *token,
                      int64_t *os_code, const char *magic) {
  try {
  configuration decoded;
  if (!token || !os_code) return -1;
  *os_code = 0;
  if (!decode(config, length, decoded, magic)) return -1;
  auto process = std::make_shared<child>();
  process->input = std::move(decoded.input);
  process->output_limit = decoded.stdout_limit;
  process->error_limit = decoded.stderr_limit;
  process->output.reserve(decoded.stdout_limit);
  process->errors.reserve(decoded.stderr_limit);
  const uint64_t candidate = next_token.fetch_add(1);
  if (candidate == 0 || candidate > INT64_MAX) return -1;
  const int64_t id = static_cast<int64_t>(candidate);
  if (!spawn_child(decoded, process, *os_code)) return -1;
  bool output_started = false;
  bool error_started = false;
  bool input_started = false;
  try {
    process->output_reader = std::thread(drain, process, false, process->output_pipe);
    output_started = true;
    if (!decoded.terminal) {
      process->error_reader = std::thread(drain, process, true, process->error_pipe);
      error_started = true;
      process->input_writer = std::thread(write_input, process, process->input_pipe);
      input_started = true;
    }
    std::lock_guard guard(registry_lock);
    registry.emplace(id, process);
  } catch (...) {
    process->stop_readers.store(true);
    process->stop_writer.store(true);
#if defined(_WIN32)
    if (process->job) TerminateJobObject(process->job, 1);
    if (process->process) WaitForSingleObject(process->process, 5000);
    if (!output_started && process->output_pipe) CloseHandle(process->output_pipe);
    if (!error_started && process->error_pipe) CloseHandle(process->error_pipe);
    if (!input_started && process->input_pipe) CloseHandle(process->input_pipe);
#else
    if (process->process) kill(-process->process, SIGKILL);
    if (process->process) while (waitpid(process->process, nullptr, 0) < 0 && errno == EINTR) {}
    if (!output_started && process->output_pipe >= 0) close(process->output_pipe);
    if (!error_started && process->error_pipe >= 0) close(process->error_pipe);
    if (!input_started && process->input_pipe >= 0) close(process->input_pipe);
#endif
    join_readers(*process);
#if defined(_WIN32)
    if (process->process) CloseHandle(process->process);
    if (process->job) CloseHandle(process->job);
#endif
    return -1;
  }
  *token = id;
  return 0;
  } catch (...) { return -1; }
}

extern "C" int64_t neri_rt_v1_process_spawn(const uint8_t *config, int64_t length,
                                               int64_t *token, int64_t *os_code) {
  return spawn_process(config, length, token, os_code, "NPR1");
}

extern "C" int64_t neri_rt_v1_process_spawn_input(const uint8_t *config, int64_t length,
                                                     int64_t *token, int64_t *os_code) {
  return spawn_process(config, length, token, os_code, "NPR2");
}

extern "C" int64_t neri_rt_v1_process_poll(int64_t token, int64_t wait_ms,
                                              int64_t *state, int64_t *os_code) {
  if (!state || !os_code || wait_ms < 0 || wait_ms > 60000) return -1;
  *os_code = 0;
  auto process = find_child(token);
  if (!process) return -1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
  do {
    if (!refresh(process, *os_code)) return -1;
    { std::lock_guard guard(process->lock); if (!process->running) break; }
    if (wait_ms == 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  bool finished = false;
  { std::lock_guard guard(process->lock); finished = !process->running; }
  if (finished) join_readers(*process);
  std::lock_guard guard(process->lock);
  state[0] = process->running ? 0 : 1;
  state[1] = process->exit_kind;
  state[2] = process->exit_value;
  state[3] = static_cast<int64_t>(process->output.size());
  state[4] = static_cast<int64_t>(process->errors.size());
  state[5] = process->output_truncated;
  state[6] = process->error_truncated;
  return 0;
}

extern "C" int64_t neri_rt_v1_process_read(int64_t token, int64_t channel,
                                              int64_t offset, uint8_t *output,
                                              int64_t capacity, int64_t *count) {
  if ((channel != 1 && channel != 2) || offset < 0 || capacity < 0 || !count ||
      (capacity != 0 && !output)) return -1;
  auto process = find_child(token);
  if (!process) return -1;
  std::lock_guard guard(process->lock);
  const auto &source = channel == 1 ? process->output : process->errors;
  if (static_cast<uint64_t>(offset) > source.size()) return -1;
  const size_t available = source.size() - static_cast<size_t>(offset);
  const size_t copied = std::min(available, static_cast<size_t>(capacity));
  if (copied) std::memcpy(output, source.data() + offset, copied);
  *count = static_cast<int64_t>(copied);
  return 0;
}

extern "C" int64_t neri_rt_v1_process_interrupt(int64_t token, int64_t *os_code) {
  if (!os_code) return -1;
  *os_code = 0;
  auto process = find_child(token);
  if (!process) return -1;
  if (!refresh(process, *os_code)) return -1;
  { std::lock_guard guard(process->lock); if (!process->running) return 0; }
#if defined(_WIN32)
  if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process->process_group)) {
    *os_code = GetLastError();
    return -1;
  }
#else
  if (kill(-process->process, SIGINT) != 0 && errno != ESRCH) {
    *os_code = errno;
    return -1;
  }
#endif
  return 0;
}

extern "C" int64_t neri_rt_v1_process_cancel(int64_t token, int64_t *os_code) {
  if (!os_code) return -1;
  *os_code = 0;
  auto process = find_child(token);
  if (!process) return -1;
  { std::lock_guard guard(process->lock); if (!process->running) return 0; process->cancelled = true; }
#if defined(_WIN32)
  if (!TerminateJobObject(process->job, 1)) { *os_code = GetLastError(); return -1; }
  if (WaitForSingleObject(process->process, INFINITE) != WAIT_OBJECT_0) { *os_code = GetLastError(); return -1; }
#else
  if (kill(-process->process, SIGKILL) != 0 && errno != ESRCH) { *os_code = errno; return -1; }
  int status = 0;
  while (waitpid(process->process, &status, 0) < 0) if (errno != EINTR) { *os_code = errno; return -1; }
#endif
  { std::lock_guard guard(process->lock); process->running = false; process->exit_kind = 3; process->exit_value = 1; }
  process->stop_writer.store(true);
  process->stop_readers.store(true);
  join_readers(*process);
  return 0;
}

extern "C" int64_t neri_rt_v1_process_dispose(int64_t token, int64_t *os_code) {
  if (!os_code) return -1;
  *os_code = 0;
  auto process = find_child(token);
  if (!process) return 0;
  if (neri_rt_v1_process_cancel(token, os_code) != 0) return -1;
  // interrupt() can observe completion through refresh() before any poll joins
  // the I/O threads. Disposal owns those threads even for an exited child.
  join_readers(*process);
  { std::lock_guard guard(registry_lock); registry.erase(token); }
#if defined(_WIN32)
  if (process->process && !CloseHandle(process->process)) {
    *os_code = GetLastError();
    if (process->job) CloseHandle(process->job);
    return -1;
  }
  if (process->job && !CloseHandle(process->job)) {
    *os_code = GetLastError();
    return -1;
  }
#endif
  return 0;
}

namespace {
struct process_registry_cleanup {
  ~process_registry_cleanup() {
    while (true) {
      int64_t token = 0;
      {
        std::lock_guard guard(registry_lock);
        if (registry.empty()) break;
        token = registry.begin()->first;
      }
      int64_t ignored_os_code = 0;
      if (neri_rt_v1_process_dispose(token, &ignored_os_code) != 0) {
        std::lock_guard guard(registry_lock);
        registry.erase(token);
      }
    }
  }
};
process_registry_cleanup cleanup_process_registry;
}
