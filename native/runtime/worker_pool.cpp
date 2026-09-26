#include "worker_pool.h"
#include "neri/runtime_abi.h"
#include "../platform/readiness_channel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {
constexpr uint64_t mib = 1024 * 1024;
struct job {
  uint64_t id = 0;
  uint64_t reservation = 0;
  job *previous = nullptr;
  job *next = nullptr;
  uint32_t kind = NERI_WORKER_SUCCESS_V1;
  bool active = false;
  bool cancelled = false;
  bool done = false;
  std::vector<uint8_t> input;
  std::vector<uint8_t> output;
};
struct pool;
struct worker {
  pool *owner = nullptr;
  const void *heap = nullptr;
  job *current = nullptr;
  bool ready = false;
  bool exited = false;
  bool failed = false;
  uint32_t error_length = 0;
  std::array<uint8_t, 4096> error{};
};
struct pool {
  neri::platform::readiness_channel readiness;
  const void *heap = nullptr;
  neri_worker_options_v1 options{};
  neri_worker_entry_v1 entry = nullptr;
  std::vector<uint8_t> config;
  std::vector<std::unique_ptr<worker>> workers;
  std::vector<std::thread> threads;
  std::mutex mutex;
  std::condition_variable changed;
  std::map<uint64_t, std::unique_ptr<job>> jobs;
  uint64_t next_job = 1;
  job *queued_head = nullptr;
  job *queued_tail = nullptr;
  job *completed_head = nullptr;
  job *completed_tail = nullptr;
  uint64_t reserved = 0;
  uint32_t alive = 0;
  bool stopping = false;
};
std::atomic<int64_t> next_handle{1};
thread_local std::map<int64_t, std::unique_ptr<pool>> owned;
thread_local worker *current_worker = nullptr;

worker *active_worker() {
  return current_worker != nullptr && current_worker->heap != nullptr &&
      current_worker->heap == neri_worker_heap_identity()
      ? current_worker : nullptr;
}

pool *lookup(int64_t handle) {
  const auto found = owned.find(handle);
  if (found == owned.end() || found->second->heap != neri_worker_heap_identity()) return nullptr;
  return found->second.get();
}
void append(job *&head, job *&tail, job &j) {
  j.previous = tail;
  j.next = nullptr;
  if (tail != nullptr) tail->next = &j;
  else head = &j;
  tail = &j;
}
void unlink(job *&head, job *&tail, job &j) {
  if (j.previous != nullptr) j.previous->next = j.next;
  else head = j.next;
  if (j.next != nullptr) j.next->previous = j.previous;
  else tail = j.previous;
  j.previous = j.next = nullptr;
}
// Called under the pool mutex by both OS readiness and completion polling.
bool poll_ready(const pool &p) {
  return p.completed_head != nullptr || (p.jobs.empty() && (p.stopping || p.alive == 0));
}
void finish(pool &p, job &j, uint32_t kind) {
  if (!j.active) unlink(p.queued_head, p.queued_tail, j);
  j.kind = kind;
  j.done = true;
  append(p.completed_head, p.completed_tail, j);
  if (kind != NERI_WORKER_SUCCESS_V1) j.output.clear();
  p.readiness.set(poll_ready(p));
  p.changed.notify_all();
}
void stop_if_unavailable(pool &p) {
  for (const auto &w : p.workers) {
    if (!w->failed && !w->exited) return;
  }
  p.stopping = true;
  while (p.queued_head != nullptr) finish(p, *p.queued_head, NERI_WORKER_FAILED_V1);
  p.readiness.set(poll_ready(p));
  p.changed.notify_all();
}
void failure(worker &w, const uint8_t *text, uint64_t length) {
  if (!w.failed) {
    w.failed = true;
    w.error_length = static_cast<uint32_t>(std::min<uint64_t>(length, w.error.size()));
    if (w.error_length != 0) std::memcpy(w.error.data(), text, w.error_length);
  }
  if (w.current != nullptr) {
    finish(*w.owner, *w.current, NERI_WORKER_FAILED_V1);
    w.current = nullptr;
  }
  stop_if_unavailable(*w.owner);
  w.owner->changed.notify_all();
}
void failure(worker &w, const char *text) {
  failure(w, reinterpret_cast<const uint8_t *>(text), std::strlen(text));
}
void stop(pool &p) {
  std::lock_guard lock(p.mutex);
  p.stopping = true;
  for (auto &[id, item] : p.jobs) {
    (void)id;
    if (item->done) continue;
    item->cancelled = true;
    if (!item->active) finish(p, *item, NERI_WORKER_CANCELLED_V1);
  }
  p.readiness.set(poll_ready(p));
  p.changed.notify_all();
}
void join(pool &p, neri_worker_close_report_v1 *report) {
  stop(p);
  for (auto &thread : p.threads) if (thread.joinable()) thread.join();
  if (report != nullptr) {
    *report = {};
    for (const auto &w : p.workers) {
      if (!w->failed) continue;
      if (report->failures++ == 0) {
        report->error_length = w->error_length;
        std::memcpy(report->first_error, w->error.data(), w->error_length);
      }
    }
  }
}
void open_failure(pool *p, neri_worker_close_report_v1 *report, const char *message) {
  if (p != nullptr) join(*p, report);
  if (report != nullptr) {
    ++report->failures;
    report->error_length = static_cast<uint32_t>(std::min<size_t>(std::strlen(message), sizeof(report->first_error)));
    std::memcpy(report->first_error, message, report->error_length);
  }
}
void run(worker *w) {
  current_worker = w;
  const auto *info = neri_rt_v1_get_abi();
  const neri_runtime_abi_requirements_v1 requirements{
      sizeof(requirements), info->major, info->minor, info->features};
  const auto initialized = neri_rt_v1_initialize(&requirements) == NERI_ABI_STATUS_OK_V1;
  if (initialized) w->heap = neri_worker_heap_identity();
  if (initialized) w->owner->entry(w->owner->config.data(), w->owner->config.size());
  {
    auto &p = *w->owner;
    std::lock_guard lock(p.mutex);
    if (!initialized) failure(*w, "worker runtime initialization failed");
    else if (!w->ready && !p.stopping) failure(*w, "worker exited before ready");
    else if (w->current != nullptr) {
      if (w->current->cancelled) {
        finish(p, *w->current, NERI_WORKER_CANCELLED_V1);
        w->current = nullptr;
      } else failure(*w, "worker exited with an active job");
    }
    else if (!p.stopping && !w->failed) failure(*w, "worker exited before stop");
    w->exited = true;
    --p.alive;
    stop_if_unavailable(p);
    p.changed.notify_all();
  }
  if (initialized) neri_rt_v1_shutdown();
  current_worker = nullptr;
}
} // namespace

extern "C" {
int neri_worker_thread_active(void) { return current_worker != nullptr; }
void neri_worker_close_owned(const void *heap) {
  for (auto it = owned.begin(); it != owned.end();) {
    if (it->second->heap != heap) { ++it; continue; }
    join(*it->second, nullptr);
    it = owned.erase(it);
  }
}
int32_t neri_rt_v1_worker_pool_open(const neri_worker_options_v1 *options,
    neri_worker_entry_v1 entry, const uint8_t *config, uint64_t length, int64_t *out, neri_worker_close_report_v1 *report) {
  if (report != nullptr) *report = {};
  if (out == nullptr) return NERI_WORKER_INVALID_V1;
  *out = 0;
  const auto heap = neri_worker_heap_identity();
  if (heap == nullptr || options == nullptr || options->struct_size != sizeof(*options) ||
      entry == nullptr || (length != 0 && config == nullptr)) return NERI_WORKER_INVALID_V1;
  if (options->worker_count == 0 || options->worker_count > 64 ||
      options->max_outstanding == 0 || options->max_outstanding > 65536 ||
      options->max_input_bytes > 128 * mib || options->max_output_bytes > 128 * mib ||
      options->max_reserved_bytes > 1024 * mib || length > mib) return NERI_WORKER_LIMIT_V1;
  std::unique_ptr<pool> p;
  try {
    p = std::make_unique<pool>();
    p->readiness.open();
    p->heap = heap;
    p->options = *options;
    p->entry = entry;
    if (length != 0) p->config.assign(config, config + length);
    p->workers.reserve(options->worker_count);
    p->threads.reserve(options->worker_count);
    for (uint32_t i = 0; i < options->worker_count; ++i) {
      auto w = std::make_unique<worker>();
      w->owner = p.get();
      p->workers.push_back(std::move(w));
    }
    for (auto &w : p->workers) {
      { std::lock_guard lock(p->mutex); ++p->alive; }
      try { p->threads.emplace_back(run, w.get()); }
      catch (...) { std::lock_guard lock(p->mutex); --p->alive; throw; }
    }
    {
      std::unique_lock lock(p->mutex);
      p->changed.wait(lock, [&] {
        bool ready = true;
        for (const auto &w : p->workers) {
          if (w->failed || w->exited) return true;
          ready = ready && w->ready;
        }
        return ready;
      });
      for (const auto &w : p->workers) {
        if (w->failed || w->exited) {
          lock.unlock();
          join(*p, report);
          return NERI_WORKER_STARTUP_FAILED_V1;
        }
      }
    }
    auto id = next_handle.load();
    do {
      if (id == std::numeric_limits<int64_t>::max()) { join(*p, nullptr); return NERI_WORKER_LIMIT_V1; }
    } while (!next_handle.compare_exchange_weak(id, id + 1));
    // Allocate the registry node before transferring ownership, so failures can join.
    auto position = owned.emplace(id, nullptr).first;
    position->second = std::move(p);
    *out = id;
    return NERI_WORKER_OK_V1;
  } catch (const std::bad_alloc &error) {
    open_failure(p.get(), report, error.what());
    return NERI_WORKER_NO_MEMORY_V1;
  } catch (const std::exception &error) {
    open_failure(p.get(), report, error.what());
    return NERI_WORKER_UNAVAILABLE_V1;
  }
}
int32_t neri_rt_v1_worker_pool_submit(int64_t handle, const uint8_t *bytes, uint64_t length, uint64_t *out) {
  auto *p = lookup(handle);
  if (p == nullptr || out == nullptr || (length != 0 && bytes == nullptr)) return NERI_WORKER_INVALID_V1;
  *out = 0;
  std::lock_guard lock(p->mutex);
  if (p->stopping || p->alive == 0) return NERI_WORKER_STOPPED_V1;
  if (length > p->options.max_input_bytes) return NERI_WORKER_LIMIT_V1;
  const auto reservation = length + p->options.max_output_bytes;
  if (p->jobs.size() >= p->options.max_outstanding || reservation > p->options.max_reserved_bytes ||
      p->reserved > p->options.max_reserved_bytes - reservation) return NERI_WORKER_FULL_V1;
  if (p->next_job == UINT64_MAX) return NERI_WORKER_LIMIT_V1;
  try {
    auto j = std::make_unique<job>();
    j->id = p->next_job;
    j->reservation = reservation;
    if (length != 0) j->input.assign(bytes, bytes + length);
    auto *inserted = j.get();
    p->jobs.emplace(j->id, std::move(j));
    append(p->queued_head, p->queued_tail, *inserted);
    p->reserved += reservation;
    *out = p->next_job++;
    p->changed.notify_all();
    return NERI_WORKER_OK_V1;
  } catch (const std::bad_alloc &) { return NERI_WORKER_NO_MEMORY_V1; }
}
int32_t neri_rt_v1_worker_pool_poll(int64_t handle, uint32_t milliseconds, neri_worker_completion_v1 *out) {
  auto *p = lookup(handle);
  if (p == nullptr || out == nullptr) return NERI_WORKER_INVALID_V1;
  *out = {};
  std::unique_lock lock(p->mutex);
  p->readiness.set(poll_ready(*p));
  p->changed.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] {
    return poll_ready(*p);
  });
  p->readiness.set(poll_ready(*p));
  auto *j = p->completed_head;
  if (j == nullptr) return poll_ready(*p) ? NERI_WORKER_STOPPED_V1 : NERI_WORKER_EMPTY_V1;
  *out = {j->id, j->output.size(), j->kind, 0};
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_pool_take(int64_t handle, uint64_t ticket, uint8_t *out, uint64_t capacity) {
  auto *p = lookup(handle);
  if (p == nullptr) return NERI_WORKER_INVALID_V1;
  std::lock_guard lock(p->mutex);
  auto it = p->jobs.find(ticket);
  if (it == p->jobs.end()) return NERI_WORKER_INVALID_V1;
  auto &j = *it->second;
  if (!j.done) return NERI_WORKER_EMPTY_V1;
  if (capacity < j.output.size()) return NERI_WORKER_LIMIT_V1;
  if (!j.output.empty() && out == nullptr) return NERI_WORKER_INVALID_V1;
  if (!j.output.empty()) std::memcpy(out, j.output.data(), j.output.size());
  unlink(p->completed_head, p->completed_tail, j);
  p->reserved -= j.reservation;
  p->jobs.erase(it);
  p->readiness.set(poll_ready(*p));
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_pool_readiness(int64_t handle, int64_t *out_descriptor) {
  if (out_descriptor == nullptr) return NERI_WORKER_INVALID_V1;
  *out_descriptor = -1;
  auto *p = lookup(handle);
  if (p == nullptr) return NERI_WORKER_INVALID_V1;
  *out_descriptor = p->readiness.descriptor();
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_pool_cancel(int64_t handle, uint64_t ticket) {
  auto *p = lookup(handle);
  if (p == nullptr) return NERI_WORKER_INVALID_V1;
  std::lock_guard lock(p->mutex);
  const auto it = p->jobs.find(ticket);
  if (it == p->jobs.end()) return NERI_WORKER_INVALID_V1;
  auto &j = *it->second;
  if (!j.done) {
    j.cancelled = true;
    if (!j.active) finish(*p, j, NERI_WORKER_CANCELLED_V1);
  }
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_pool_stop(int64_t handle) {
  auto *p = lookup(handle);
  if (p == nullptr) return NERI_WORKER_INVALID_V1;
  stop(*p);
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_pool_close(int64_t handle, neri_worker_close_report_v1 *report) {
  auto *p = lookup(handle);
  if (p == nullptr || report == nullptr) return NERI_WORKER_INVALID_V1;
  join(*p, report);
  owned.erase(handle);
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_ready(void) {
  auto *w = active_worker();
  if (w == nullptr) return NERI_WORKER_INVALID_V1;
  std::lock_guard lock(w->owner->mutex);
  if (w->failed || w->ready) return NERI_WORKER_INVALID_V1;
  if (w->owner->stopping) return NERI_WORKER_STOPPED_V1;
  w->ready = true;
  w->owner->changed.notify_all();
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_receive(uint64_t *length) {
  auto *w = active_worker();
  if (w == nullptr || length == nullptr) return NERI_WORKER_INVALID_V1;
  *length = 0;
  auto &p = *w->owner;
  std::unique_lock lock(p.mutex);
  if (!w->ready || w->failed || w->current != nullptr) return NERI_WORKER_INVALID_V1;
  p.changed.wait(lock, [&] { return p.stopping || p.queued_head != nullptr; });
  if (p.stopping) return NERI_WORKER_STOPPED_V1;
  w->current = p.queued_head;
  unlink(p.queued_head, p.queued_tail, *w->current);
  w->current->active = true;
  *length = w->current->input.size();
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_read(uint8_t *out, uint64_t capacity) {
  auto *w = active_worker();
  if (w == nullptr) return NERI_WORKER_INVALID_V1;
  std::lock_guard lock(w->owner->mutex);
  if (w->current == nullptr || w->failed) return NERI_WORKER_INVALID_V1;
  const auto &input = w->current->input;
  if (capacity < input.size()) return NERI_WORKER_LIMIT_V1;
  if (!input.empty() && out == nullptr) return NERI_WORKER_INVALID_V1;
  if (!input.empty()) std::memcpy(out, input.data(), input.size());
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_reply(const uint8_t *bytes, uint64_t length) {
  auto *w = active_worker();
  if (w == nullptr || (length != 0 && bytes == nullptr)) return NERI_WORKER_INVALID_V1;
  auto &p = *w->owner;
  std::lock_guard lock(p.mutex);
  if (w->current == nullptr || w->failed) return NERI_WORKER_INVALID_V1;
  if (length > p.options.max_output_bytes) return NERI_WORKER_LIMIT_V1;
  auto &j = *w->current;
  try {
    if (!j.cancelled && length != 0) j.output.assign(bytes, bytes + length);
  } catch (const std::bad_alloc &) { return NERI_WORKER_NO_MEMORY_V1; }
  finish(p, j, j.cancelled ? NERI_WORKER_CANCELLED_V1 : NERI_WORKER_SUCCESS_V1);
  w->current = nullptr;
  return NERI_WORKER_OK_V1;
}
int32_t neri_rt_v1_worker_cancelled(void) {
  auto *w = active_worker();
  if (w == nullptr) return 0;
  std::lock_guard lock(w->owner->mutex);
  return w->owner->stopping || w->failed || (w->current != nullptr && w->current->cancelled);
}
int32_t neri_rt_v1_worker_fail(const uint8_t *detail, uint64_t length) {
  auto *w = active_worker();
  if (w == nullptr || (length != 0 && detail == nullptr)) return NERI_WORKER_INVALID_V1;
  std::lock_guard lock(w->owner->mutex);
  failure(*w, detail, length);
  return NERI_WORKER_OK_V1;
}
} // extern "C"
