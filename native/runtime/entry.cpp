#include "neri/runtime_abi.h"

#include <cstdint>
#include <cstring>
#if defined(_WIN32)
#include "../platform/windows_support.h"
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#endif

extern "C" void hk1_f_q1_n4_6d61696e__p0_rv(void);

namespace {
[[noreturn]] void panic_abi_mismatch(neri_abi_status_v1 status) {
  const char *message = "Runtime ABI negotiation failed.";
  switch (status) {
  case NERI_ABI_STATUS_INVALID_ARGUMENT_V1:
    message = "Program supplied invalid runtime ABI requirements.";
    break;
  case NERI_ABI_STATUS_INCOMPATIBLE_MAJOR_V1:
    message = "Program and runtime ABI major versions differ.";
    break;
  case NERI_ABI_STATUS_RUNTIME_TOO_OLD_V1:
    message = "Runtime ABI minor version is too old for this program.";
    break;
  case NERI_ABI_STATUS_MISSING_FEATURE_V1:
    message = "Runtime is missing a feature required by this program.";
    break;
  default:
    break;
  }
  const neri_panic_v1 panic = {
      NERI_PANIC_ABI_MISMATCH_V1,
      0,
      reinterpret_cast<const uint8_t *>(message),
      std::strlen(message),
  };
  neri_rt_v1_panic(&panic);
}
} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
  // The LSP byte counts and UTF-8 console output must not undergo CRLF conversion.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  int wide_count = 0;
  auto **wide_args = CommandLineToArgvW(GetCommandLineW(), &wide_count);
  if (!wide_args) return 1;
  std::vector<std::string> storage;
  for (int index = 0; index < wide_count; ++index) storage.push_back(neri::windows::utf8(wide_args[index]));
  LocalFree(wide_args);
  std::vector<char *> arguments;
  for (auto &argument : storage) arguments.push_back(argument.data());
  arguments.push_back(nullptr);
  argc = wide_count;
  argv = arguments.data();
#endif
  volatile unsigned char abi_anchor = neri_rt_v1_abi_anchor;
  static_cast<void>(abi_anchor);
  const auto status =
      neri_rt_v1_initialize(&neri_program_v1_abi_requirements);
  if (status != NERI_ABI_STATUS_OK_V1) {
    panic_abi_mismatch(status);
  }
  neri_rt_v1_set_process_arguments(argc, argv);
  hk1_f_q1_n4_6d61696e__p0_rv();
  neri_rt_v1_shutdown();
  return 0;
}
