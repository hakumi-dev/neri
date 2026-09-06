#include "task_heap.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <thread>

namespace {
[[noreturn]] void fail(uint32_t code, const char *message) {
  const neri_panic_v1 panic{code, 0,
      reinterpret_cast<const uint8_t *>(message), std::strlen(message)};
  neri_rt_v1_panic(&panic);
}

struct group;
struct job {
  group *owner;
  neri_task_ticket *ticket;
  uint64_t begin;
  uint64_t end;
  job *next;
};

struct executor {
  explicit executor(uint32_t workers) : limit(workers) {}
  uint32_t limit;
  std::mutex mutex;
  std::condition_variable changed;
  job *ready = nullptr;
  bool stopping = false;
};

struct group {
  executor &pool;
  uint64_t count;
  uint32_t limit;
  neri_task_range_body body;
  void *context;
  neri_ref_v1 *results;
  uint64_t references_per_item;
  uint64_t pending = 0;
  uint32_t active = 0;
};

constinit thread_local executor *active_executor = nullptr;

// After publication, queue, pending and active accesses hold the pool mutex.
// A group may outlive its executing tasks while its coordinator helps children.
job *take(executor &pool) {
  auto **link = &pool.ready;
  while (*link != nullptr) {
    auto *item = *link;
    if (item->owner->active < item->owner->limit) {
      *link = item->next;
      ++item->owner->active;
      return item;
    }
    link = &item->next;
  }
  return nullptr;
}

void run_range(void *context) {
  const auto &item = *static_cast<job *>(context);
  item.owner->body(item.begin, item.end, item.owner->context);
}

void execute(executor &pool, job &item) {
  auto *owner = item.owner;
  neri_task_execute(item.ticket, run_range, &item);
  std::lock_guard lock(pool.mutex);
  --owner->active;
  --owner->pending;
  pool.changed.notify_all();
  // The coordinator may now destroy the job and group. Do not touch either.
}

void *worker(void *context) {
  auto &pool = *static_cast<executor *>(context);
  active_executor = &pool;
  std::unique_lock lock(pool.mutex);
  while (!pool.stopping) {
    auto *item = take(pool);
    if (item == nullptr) {
      pool.changed.wait(lock);
    } else {
      lock.unlock();
      execute(pool, *item);
      lock.lock();
    }
  }
  active_executor = nullptr;
  return nullptr;
}

void coordinate(neri_task_scope *scope, void *context) {
  auto &tasks = *static_cast<group *>(context);
  if (tasks.count == 0) return;
  // A bounded number of chunks avoids one heap/ticket/queue entry per element.
  const auto chunks = std::min(tasks.count, uint64_t{tasks.limit} * 4);
  const auto width = tasks.count / chunks;
  const auto remainder = tasks.count % chunks;
  auto *jobs = static_cast<job *>(std::calloc(static_cast<size_t>(chunks), sizeof(job)));
  if (jobs == nullptr) fail(NERI_PANIC_OUT_OF_MEMORY_V1, "task range allocation failed");
  tasks.pending = chunks;
  uint64_t begin = 0;
  for (uint64_t index = 0; index < chunks; ++index) {
    const auto end = begin + width + (index < remainder ? 1 : 0);
    auto *slots = tasks.references_per_item == 0 ? nullptr
        : tasks.results + begin * tasks.references_per_item;
    auto *ticket = neri_task_register_results(scope, slots,
        (end - begin) * tasks.references_per_item);
    auto &item = jobs[index];
    item = {&tasks, ticket, begin, end, nullptr};
    {
      std::lock_guard lock(tasks.pool.mutex);
      item.next = tasks.pool.ready;
      tasks.pool.ready = &item;
      tasks.pool.changed.notify_one();
    }
    begin = end;
  }
  auto &pool = tasks.pool;
  std::unique_lock lock(pool.mutex);
  while (tasks.pending != 0) {
    auto *item = take(pool);
    if (item == nullptr) {
      pool.changed.wait(lock);
    } else {
      lock.unlock();
      execute(pool, *item);
      lock.lock();
    }
  }
  lock.unlock();
  std::free(jobs);
}

void run_group(executor &pool, uint64_t count, uint32_t parallelism,
               neri_task_range_body body, void *context, neri_ref_v1 *results,
               uint64_t references_per_item) {
  group tasks{pool, count, std::min(pool.limit, parallelism), body, context,
              results, references_per_item};
  neri_task_scope_run(coordinate, &tasks);
}
} // namespace

extern "C" void neri_task_parallel_for(uint64_t count, uint32_t parallelism,
    neri_task_range_body body, void *context, neri_ref_v1 *results,
    uint64_t references_per_item) {
  if (parallelism == 0 || body == nullptr ||
      (references_per_item != 0 && count != 0 &&
       (results == nullptr || count > SIZE_MAX / sizeof(neri_ref_v1) / references_per_item))) {
    fail(NERI_PANIC_RUNTIME_CONTRACT_V1, "invalid parallel range or result span");
  }
  if (active_executor != nullptr) {
    run_group(*active_executor, count, parallelism, body, context, results, references_per_item);
    return;
  }
  const auto hardware = std::max(1U, std::thread::hardware_concurrency());
  const auto limit = static_cast<uint32_t>(std::min<uint64_t>(
      std::min(parallelism, hardware), std::max(uint64_t{1}, count)));
  executor pool{limit};
  auto *threads = limit == 1 ? nullptr
      : static_cast<pthread_t *>(std::calloc(limit - 1, sizeof(pthread_t)));
  if (limit > 1 && threads == nullptr) {
    fail(NERI_PANIC_OUT_OF_MEMORY_V1, "task worker allocation failed");
  }
  active_executor = &pool;
  for (uint32_t index = 0; index + 1 < limit; ++index) {
    if (pthread_create(&threads[index], nullptr, worker, &pool) != 0) {
      fail(NERI_PANIC_OUT_OF_MEMORY_V1, "task worker creation failed");
    }
  }
  run_group(pool, count, parallelism, body, context, results, references_per_item);
  {
    std::lock_guard lock(pool.mutex);
    pool.stopping = true;
    pool.changed.notify_all();
  }
  for (uint32_t index = 0; index + 1 < limit; ++index) {
    if (pthread_join(threads[index], nullptr) != 0) {
      fail(NERI_PANIC_RUNTIME_CONTRACT_V1, "task worker join failed");
    }
  }
  active_executor = nullptr;
  std::free(threads);
}
