// Included inside the host's anonymous namespace; all handles close on failure.
struct windows_handle {
  HANDLE value;
  explicit windows_handle(HANDLE handle) : value(handle) {
    if (!value || value == INVALID_HANDLE_VALUE) throw std::runtime_error("Windows handle creation failed");
  }
  ~windows_handle() { CloseHandle(value); }
  windows_handle(const windows_handle &) = delete;
  windows_handle &operator=(const windows_handle &) = delete;
};
void run(int argc, char **argv) {
  if (argc < 7) throw std::runtime_error("run <seconds> <stdout> <stderr> <status> <executable> [args...]");
  size_t consumed = 0;
  const auto seconds = std::stoi(argv[2], &consumed);
  if (consumed != std::strlen(argv[2]) || seconds < 1 || seconds > 3600)
    throw std::runtime_error("timeout must be 1..3600 seconds");
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
  windows_handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr));
  windows_handle output(CreateFileW(neri::windows::wide(argv[3]).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, 0, nullptr));
  windows_handle errors(CreateFileW(neri::windows::wide(argv[4]).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, 0, nullptr));
  windows_handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    throw std::runtime_error("cannot configure process job");
  std::wstring command;
  for (int index = 6; index < argc; ++index) {
    if (!command.empty()) command += L' ';
    command += neri::windows::quote(neri::windows::wide(argv[index]));
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input.value;
  startup.hStdOutput = output.value;
  startup.hStdError = errors.value;
  PROCESS_INFORMATION process{};
  const auto started = std::chrono::steady_clock::now();
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    throw std::runtime_error("cannot start process: " + std::to_string(GetLastError()));
  windows_handle child(process.hProcess), thread(process.hThread);
  if (!AssignProcessToJobObject(job.value, child.value)) {
    TerminateProcess(child.value, 1);
    throw std::runtime_error("cannot assign process job");
  }
  if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) throw std::runtime_error("cannot resume process");
  const auto waited = WaitForSingleObject(child.value, static_cast<DWORD>(seconds) * 1000);
  const bool timeout = waited == WAIT_TIMEOUT;
  if (waited != WAIT_OBJECT_0 && !timeout) throw std::runtime_error("cannot wait for process");
  if (!TerminateJobObject(job.value, 124)) throw std::runtime_error("cannot terminate process job");
  WaitForSingleObject(child.value, INFINITE);
  DWORD status;
  FILETIME created, exited, kernel, user;
  PROCESS_MEMORY_COUNTERS memory{};
  if (!GetExitCodeProcess(child.value, &status) || !GetProcessTimes(child.value, &created, &exited, &kernel, &user) ||
      !GetProcessMemoryInfo(child.value, &memory, sizeof(memory))) throw std::runtime_error("cannot read process metrics");
  auto duration = [](FILETIME value) { return (static_cast<uint64_t>(value.dwHighDateTime) * 4294967296ULL + value.dwLowDateTime) / 10000000.0; };
  const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  write(std::string(argv[5]) + ".metrics.json", "{\"wallSeconds\":" + std::to_string(wall) +
      ",\"userSeconds\":" + std::to_string(duration(user)) + ",\"systemSeconds\":" + std::to_string(duration(kernel)) +
      ",\"peakRssBytes\":" + std::to_string(memory.PeakWorkingSetSize) + "}\n");
  write(argv[5], std::to_string(timeout ? 124 : status) + "\n");
}
