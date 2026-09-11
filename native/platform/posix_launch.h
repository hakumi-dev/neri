#ifndef NERI_PLATFORM_POSIX_LAUNCH_H
#define NERI_PLATFORM_POSIX_LAUNCH_H
#if !defined(_WIN32)
#include <string>
#include <vector>
#include <sys/types.h>

namespace neri::platform {
struct posix_launch_options {
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  std::string working_directory;
  int standard_input = -1;
  int standard_output = -1;
  int standard_error = -1;
  bool process_group = false;
};

// Returns only after exec succeeds (the CLOEXEC pipe reaches EOF) or returns
// the pre-exec/exec errno. All dynamic data is prepared before fork.
bool posix_launch(const posix_launch_options &options, pid_t &process, int &error);
bool posix_pipe_cloexec(int descriptors[2], int &error);
}
#endif
#endif
