#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

/* PTY/process supervision is unavailable through host.run. Behavioral assertions
 * belong to the Neri child; this adapter verifies OS terminal restoration. */
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  int master, slave;
  struct winsize size = {.ws_row = 30, .ws_col = 80};
  if (openpty(&master, &slave, NULL, NULL, &size) != 0) return 2;
  struct termios before = {0}, after = {0};
  if (tcgetattr(slave, &before) != 0) return 2;
  const pid_t child = fork();
  if (child == 0) {
    close(master);
    if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0) _exit(2);
    for (int fd = 0; fd < 3; ++fd) if (dup2(slave, fd) < 0) _exit(2);
    if (slave > 2) close(slave);
    execl(argv[1], argv[1], (char *)NULL);
    _exit(2);
  }
  if (child < 0) return 2;
  char output[8192] = {0};
  size_t used = 0;
  int sent = 0, interrupted = 0, finished = 0, status = 0, acknowledged = 0, restored = 0;
  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    struct pollfd input = {master, POLLIN, 0};
    if (poll(&input, 1, 20) > 0 && (input.revents & POLLIN) && used < sizeof(output) - 1) {
      const ssize_t count = read(master, output + used, sizeof(output) - 1 - used);
      if (count > 0) { used += (size_t)count; output[used] = 0; }
    }
    if (!sent && strstr(output, "READY")) {
      (void)write(master, "\033[Aw", 4);
      sent = 1;
    }
    if (!interrupted && strstr(output, "INTERRUPT")) {
      kill(child, SIGINT);
      interrupted = 1;
    }
    if (!acknowledged && strstr(output, "DONE")) {
      /* PENDIN is a kernel reprocessing flag after toggling canonical input. */
      restored = tcgetattr(slave, &after) == 0 &&
        before.c_iflag == after.c_iflag && before.c_oflag == after.c_oflag &&
        before.c_cflag == after.c_cflag && (before.c_lflag & (tcflag_t)~PENDIN) == (after.c_lflag & (tcflag_t)~PENDIN) &&
        memcmp(before.c_cc, after.c_cc, sizeof(before.c_cc)) == 0;
      (void)write(master, "finish\n", 7);
      acknowledged = 1;
    }
    if (waitpid(child, &status, WNOHANG) == child) { finished = 1; break; }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - start.tv_sec >= 10) break;
  }
  if (!finished) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
  fcntl(master, F_SETFL, O_NONBLOCK);
  while (used < sizeof(output) - 1) {
    const ssize_t count = read(master, output + used, sizeof(output) - 1 - used);
    if (count <= 0) break;
    used += (size_t)count;
  }
  output[used] = 0;
  close(master);
  close(slave);
  if (!finished || !WIFEXITED(status) || WEXITSTATUS(status) || !strstr(output, "DONE") || !restored) {
    fprintf(stderr, "termios flags before=%lx/%lx/%lx/%lx after=%lx/%lx/%lx/%lx\n",
      (unsigned long)before.c_iflag, (unsigned long)before.c_oflag, (unsigned long)before.c_cflag, (unsigned long)before.c_lflag,
      (unsigned long)after.c_iflag, (unsigned long)after.c_oflag, (unsigned long)after.c_cflag, (unsigned long)after.c_lflag);
    fprintf(stderr, "Terminal contract failed (restored=%d): %s\n", restored, output);
    return 1;
  }
  puts("Terminal contracts passed");
  return 0;
}
