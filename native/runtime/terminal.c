#define _POSIX_C_SOURCE 200809L
#include "neri/runtime_abi.h"
#include "terminal.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* One process-wide terminal lease; saved layouts never cross the ABI. */
static struct termios saved;
static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
static struct sigaction previous[5];
static volatile sig_atomic_t interrupted;
static int active, registered;
static int64_t generation;

void neri_terminal_restore(void) {
  if (!active) return;
  (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  const char reset[] = "\033[0m\033[?25h\033[?1049l";
  (void)write(STDOUT_FILENO, reset, sizeof(reset) - 1);
  for (size_t i = 0; i < 5; ++i) (void)sigaction(signals[i], &previous[i], NULL);
  active = 0;
}

static void interrupt_session(int number) { (void)number; interrupted = 1; }

int64_t neri_rt_v1_terminal_open(void) {
  if (active || generation == INT64_MAX || !isatty(0) || !isatty(1) ||
      tcgetpgrp(0) != getpgrp() || tcgetattr(0, &saved) != 0) return 0;
  if (!registered) {
    if (atexit(neri_terminal_restore) != 0) return 0;
    registered = 1;
  }
  sigset_t blocked, old_mask;
  sigemptyset(&blocked);
  for (size_t i = 0; i < 5; ++i) sigaddset(&blocked, signals[i]);
  if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) != 0) return 0;
  struct sigaction action = {0};
  action.sa_handler = interrupt_session;
  sigemptyset(&action.sa_mask);
  size_t installed = 0;
  for (; installed < 5; ++installed)
    if (sigaction(signals[installed], &action, &previous[installed]) != 0) break;
  struct termios raw = saved;
  raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  raw.c_iflag &= (tcflag_t)~(IXON | IXOFF);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (installed != 5 || tcsetattr(0, TCSANOW, &raw) != 0) {
    while (installed > 0) { --installed; sigaction(signals[installed], &previous[installed], NULL); }
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    return 0;
  }
  active = 1;
  interrupted = 0;
  ++generation;
  const char enter[] = "\033[?1049h\033[?25l\033[2J\033[H";
  (void)write(1, enter, sizeof(enter) - 1);
  sigprocmask(SIG_SETMASK, &old_mask, NULL);
  return generation;
}

void neri_rt_v1_terminal_close(int64_t token) {
  if (active && token == generation) neri_terminal_restore();
}

int64_t neri_rt_v1_terminal_read(int64_t token, int64_t timeout) {
  if (!active || token != generation || interrupted || timeout < 0 || timeout > 60000) return -2;
  struct pollfd input = {0, POLLIN, 0};
  const int ready = poll(&input, 1, (int)timeout);
  if (interrupted) return -2;
  if (ready == 0 || (ready < 0 && errno == EINTR)) return -1;
  if (ready < 0) return -2;
  unsigned char byte;
  return read(0, &byte, 1) == 1 ? byte : -2;
}

int64_t neri_rt_v1_terminal_size(int64_t token, int64_t rows) {
  struct winsize size;
  if (!active || token != generation || ioctl(1, TIOCGWINSZ, &size) != 0) return 0;
  return rows ? size.ws_row : size.ws_col;
}

int64_t neri_rt_v1_clock_milliseconds(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
