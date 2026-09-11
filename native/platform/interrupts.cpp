#include "neri/runtime_abi.h"
#include "../runtime/terminal.h"
#include <atomic>
#include <climits>
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#endif

// Lease operations run on the serving thread; handlers only set a lock-free flag.
static std::atomic<int> pending{0};
static_assert(std::atomic<int>::is_always_lock_free);
static bool active;
static int64_t generation;
#if defined(_WIN32)
static BOOL WINAPI interrupted(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
  pending.store(1, std::memory_order_relaxed);
  return TRUE;
}
#else
static const int signals[] = {SIGINT, SIGTERM};
static struct sigaction previous[2];
static void interrupted(int signal) {
  (void)signal;
  pending.store(1, std::memory_order_relaxed);
}
#endif

extern "C" int neri_interrupt_active(void) { return active; }
extern "C" int neri_interrupt_pending_any(void) {
  return pending.load(std::memory_order_relaxed) ? 1 : 0;
}
extern "C" void neri_interrupt_restore(void) {
  if (!active) return;
#if defined(_WIN32)
  SetConsoleCtrlHandler(interrupted, FALSE);
#else
  for (int index = 0; index < 2; ++index) {
    struct sigaction current {};
    if (sigaction(signals[index], nullptr, &current) == 0 &&
        current.sa_handler == interrupted) {
      sigaction(signals[index], &previous[index], nullptr);
    }
  }
#endif
  active = false;
}

neri_int_v1 neri_rt_v1_interrupt_open(void) {
  if (active || neri_terminal_active() || generation == INT64_MAX) return 0;
  pending.store(0, std::memory_order_relaxed);
#if defined(_WIN32)
  if (!SetConsoleCtrlHandler(interrupted, TRUE)) return 0;
#else
  for (int index = 0; index < 2; ++index) {
    if (sigaction(signals[index], nullptr, &previous[index]) != 0 ||
        previous[index].sa_handler != SIG_DFL) return 0;
  }
  struct sigaction action {};
  action.sa_handler = interrupted;
  sigemptyset(&action.sa_mask);
  for (int index = 0; index < 2; ++index) {
    if (sigaction(signals[index], &action, nullptr) != 0) {
      while (index > 0) { --index; sigaction(signals[index], &previous[index], nullptr); }
      return 0;
    }
  }
#endif
  active = true;
  return ++generation;
}

neri_int_v1 neri_rt_v1_interrupt_pending(neri_int_v1 token) {
  return active && token == generation && pending.load(std::memory_order_relaxed);
}
void neri_rt_v1_interrupt_close(neri_int_v1 token) {
  if (active && token == generation) neri_interrupt_restore();
}
