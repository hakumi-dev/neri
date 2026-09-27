#include "neri/runtime_abi.h"
#include "../runtime/terminal.h"
#include "../runtime/worker_pool.h"
#include <atomic>
#include <climits>
#include <mutex>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#endif

// Lease state is synchronized; only its owning thread may poll or release it.
// Signal handlers only set a lock-free flag and never acquire the lease mutex.
static std::atomic<int> pending{0};
static_assert(std::atomic<int>::is_always_lock_free);
static std::mutex lease_mutex;
static std::thread::id owner_thread;
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

extern "C" int neri_interrupt_active(void) {
  if (neri_worker_thread_active()) return 0;
  const std::lock_guard<std::mutex> lock(lease_mutex);
  return active;
}
extern "C" int neri_interrupt_pending_any(void) {
  if (neri_worker_thread_active()) return 0;
  const std::lock_guard<std::mutex> lock(lease_mutex);
  // The independent drain monitor must observe a pending owner interrupt.
  return active && pending.load(std::memory_order_relaxed) ? 1 : 0;
}
static void restore_locked(void) {
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
  owner_thread = std::thread::id{};
}
extern "C" void neri_interrupt_restore(void) {
  if (neri_worker_thread_active()) return;
  const std::lock_guard<std::mutex> lock(lease_mutex);
  if (!active || owner_thread != std::this_thread::get_id()) return;
  restore_locked();
}

neri_int_v1 neri_rt_v1_interrupt_open(void) {
  if (neri_worker_thread_active()) return 0;
  const std::lock_guard<std::mutex> lock(lease_mutex);
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
  owner_thread = std::this_thread::get_id();
  return ++generation;
}

neri_int_v1 neri_rt_v1_interrupt_pending(neri_int_v1 token) {
  if (neri_worker_thread_active()) return 0;
  const std::lock_guard<std::mutex> lock(lease_mutex);
  return active && owner_thread == std::this_thread::get_id() &&
      token == generation && pending.load(std::memory_order_relaxed);
}
void neri_rt_v1_interrupt_close(neri_int_v1 token) {
  if (neri_worker_thread_active()) return;
  const std::lock_guard<std::mutex> lock(lease_mutex);
  if (active && owner_thread == std::this_thread::get_id() && token == generation)
    restore_locked();
}
