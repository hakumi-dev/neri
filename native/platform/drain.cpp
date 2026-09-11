#include "neri/runtime_abi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

extern "C" int neri_interrupt_pending_any(void);

namespace {
struct watcher {
  int64_t timeout = 0;
  bool watch_interrupt = false;
  std::atomic<bool> requested{false};
  std::atomic<bool> stopped{false};
  std::thread monitor;
};

std::mutex registry_lock;
std::unordered_map<int64_t, std::shared_ptr<watcher>> registry;
std::atomic<uint64_t> next_token{1};

std::shared_ptr<watcher> find_watcher(int64_t token) {
  std::lock_guard guard(registry_lock);
  const auto found = registry.find(token);
  return found == registry.end() ? nullptr : found->second;
}

void monitor(const std::shared_ptr<watcher> &value) {
  bool armed = false;
  std::chrono::steady_clock::time_point deadline;
  while (!value->stopped.load(std::memory_order_acquire)) {
    if (!armed && (value->requested.load(std::memory_order_acquire) ||
                   (value->watch_interrupt && neri_interrupt_pending_any()))) {
      deadline = std::chrono::steady_clock::now() +
          std::chrono::milliseconds(value->timeout);
      armed = true;
    }
    if (armed && std::chrono::steady_clock::now() >= deadline &&
        !value->stopped.load(std::memory_order_acquire)) {
      std::_Exit(124);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

int64_t close_watcher(int64_t token) {
  std::shared_ptr<watcher> value;
  {
    std::lock_guard guard(registry_lock);
    const auto found = registry.find(token);
    if (found == registry.end()) return 0;
    value = found->second;
    registry.erase(found);
  }
  value->stopped.store(true, std::memory_order_release);
  if (value->monitor.joinable()) value->monitor.join();
  return 0;
}
}

extern "C" int64_t neri_rt_v1_drain_open(int64_t timeout_milliseconds,
                                            int64_t watch_interrupt) {
  if (timeout_milliseconds < 1 || timeout_milliseconds > 60000 ||
      (watch_interrupt != 0 && watch_interrupt != 1)) return 0;
  try {
    auto value = std::make_shared<watcher>();
    value->timeout = timeout_milliseconds;
    value->watch_interrupt = watch_interrupt != 0;
    const uint64_t candidate = next_token.fetch_add(1);
    if (candidate == 0 || candidate > INT64_MAX) return 0;
    const int64_t token = static_cast<int64_t>(candidate);
    value->monitor = std::thread(monitor, value);
    try {
      std::lock_guard guard(registry_lock);
      registry.emplace(token, value);
    } catch (...) {
      value->stopped.store(true, std::memory_order_release);
      value->monitor.join();
      return 0;
    }
    return token;
  } catch (...) {
    return 0;
  }
}

extern "C" int64_t neri_rt_v1_drain_request(int64_t token) {
  auto value = find_watcher(token);
  if (!value) return -1;
  value->requested.store(true, std::memory_order_release);
  return 0;
}

extern "C" int64_t neri_rt_v1_drain_close(int64_t token) {
  return close_watcher(token);
}

namespace {
struct drain_cleanup {
  ~drain_cleanup() {
    while (true) {
      int64_t token = 0;
      {
        std::lock_guard guard(registry_lock);
        if (registry.empty()) break;
        token = registry.begin()->first;
      }
      close_watcher(token);
    }
  }
};
drain_cleanup cleanup;
}
