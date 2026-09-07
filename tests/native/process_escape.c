#include <stdint.h>
#if defined(_WIN32)
int64_t neri_test_process_escape_with_pipes(void) { return -1; }
#else
#include <sys/types.h>
#include <unistd.h>

/* The post-fork child must use only async-signal-safe native operations. */
int64_t neri_test_process_escape_with_pipes(void) {
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    if (setsid() < 0) _exit(2);
    sleep(10);
    _exit(0);
  }
  return 0;
}
#endif
