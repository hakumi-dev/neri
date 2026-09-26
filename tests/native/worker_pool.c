#include "neri/runtime_abi.h"
#include "task_heap.h"
#include "worker_pool.h"

#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native entry callbacks and thread/context boundary probes are not yet
 * expressible through Neri's public language ABI. */
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "worker_pool:%d: %s\n", __LINE__, #value); abort(); } } while (0)

static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static unsigned entered;
static int released;
static int64_t interrupt_token;
static void entry(const uint8_t *, uint64_t);
static void reject_nested(void *argument) {
  (void)argument;
  const neri_worker_options_v1 limits = {sizeof(limits), 1, 1, 1, 1, 2};
  uint8_t config = 42;
  int64_t handle;
  uint64_t length;

  CHECK(neri_rt_v1_worker_pool_open(&limits, entry, &config, 1, &handle, NULL) == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_ready() == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_receive(&length) == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_read(NULL, 0) == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_reply(NULL, 0) == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_fail(NULL, 0) == NERI_WORKER_INVALID_V1);
}
static void nested_coordinator(neri_task_scope *scope, void *argument) {
  neri_task_execute(neri_task_register(scope), reject_nested, argument);
  neri_task_execute(neri_task_register_results(scope, argument, 1), reject_nested, argument);
}
static void nested_probe(void) {
  neri_ref_v1 result = NULL;
  neri_task_scope_run(nested_coordinator, &result);
}
static const neri_type_descriptor_v1 value_type = {
  sizeof(neri_type_descriptor_v1), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
  NERI_TYPE_KIND_CLASS_V1, 0, sizeof(uint64_t), _Alignof(uint64_t),
  0, 0, NULL, NULL, "pool-value", 0, NULL, NULL, NERI_SCALAR_KIND_NONE_V1, 0
};
static void initialize(void) {
  const neri_runtime_abi_requirements_v1 requirements = {
    sizeof(requirements), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
    NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_ROOT_FRAMES
  };

  CHECK(neri_rt_v1_initialize(&requirements) == NERI_ABI_STATUS_OK_V1);
}
static void reset_gate(void) {
  CHECK(pthread_mutex_lock(&gate_mutex) == 0);
  entered = 0;
  released = 0;

  CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
}
static void wait_entered(unsigned count) {
  CHECK(pthread_mutex_lock(&gate_mutex) == 0);
  while (entered < count) CHECK(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
  CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
}
static void release_gate(void) {
  CHECK(pthread_mutex_lock(&gate_mutex) == 0);
  ++released;

  CHECK(pthread_cond_broadcast(&gate_condition) == 0);
  CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
}
static void entry(const uint8_t *config, uint64_t config_length) {
  CHECK(neri_worker_thread_active());
  CHECK(config_length == 1);
  CHECK(neri_rt_v1_interrupt_open() == 0);
  CHECK(neri_rt_v1_interrupt_pending(interrupt_token) == 0);
  CHECK(neri_rt_v1_terminal_open() == 0);
  CHECK(neri_rt_v1_terminal_read(1, 0) == -2);
  CHECK(neri_rt_v1_terminal_size(1, 0) == 0);
  neri_rt_v1_interrupt_close(interrupt_token);
  neri_rt_v1_terminal_close(1);
  if (config[0] == 0) {
    CHECK(pthread_mutex_lock(&gate_mutex) == 0);
    const int first = ++entered == 1;
    if (first) {
      CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
      CHECK(neri_rt_v1_worker_ready() == NERI_WORKER_OK_V1);
      release_gate();
      uint64_t length;

      CHECK(neri_rt_v1_worker_receive(&length) == NERI_WORKER_STOPPED_V1);
      return;
    }
    while (!released) CHECK(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
    CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
    CHECK(neri_rt_v1_worker_fail((const uint8_t *)"startup", 7) == NERI_WORKER_OK_V1);
    return;
  }
  neri_ref_v1 root = NULL;
  neri_gc_root_frame_v1 frame = {NULL, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  root = neri_rt_v1_gc_alloc(&value_type, sizeof(uint64_t), _Alignof(uint64_t));
  *(uint64_t *)((uint8_t *)root + sizeof(*root)) = config[0];

  CHECK(neri_rt_v1_worker_ready() == NERI_WORKER_OK_V1);
  nested_probe();
  uint64_t length;
  int32_t status;
  while ((status = neri_rt_v1_worker_receive(&length)) == NERI_WORKER_OK_V1) {
    uint8_t bytes[16];

    CHECK(length <= sizeof(bytes));
    CHECK(neri_rt_v1_worker_read(bytes, sizeof(bytes)) == NERI_WORKER_OK_V1);
    CHECK(neri_rt_v1_worker_receive(&length) == NERI_WORKER_INVALID_V1);
    nested_probe();
    neri_rt_v1_gc_collect();
    neri_gc_stats_v1 stats = {sizeof(stats), 0, 0, 0, 0, 0};
    neri_rt_v1_gc_get_stats(&stats);

    CHECK(stats.managed_object_count == 1);
    CHECK(*(uint64_t *)((uint8_t *)root + sizeof(*root)) == config[0]);
    CHECK(pthread_mutex_lock(&gate_mutex) == 0);
    ++entered;

    CHECK(pthread_cond_broadcast(&gate_condition) == 0);
    if (bytes[0] == 'G' || bytes[0] == 'E' || bytes[0] == 'F') {
      while (!released) CHECK(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
    }
    CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
    if (bytes[0] == 'F') {
      CHECK(neri_rt_v1_worker_fail((const uint8_t *)"terminal", 8) == NERI_WORKER_OK_V1);
      CHECK(pthread_mutex_lock(&gate_mutex) == 0);
      ++entered;

      CHECK(pthread_cond_broadcast(&gate_condition) == 0);
      while (released < 2) CHECK(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
      CHECK(pthread_mutex_unlock(&gate_mutex) == 0);
      neri_rt_v1_gc_root_frame_leave(&frame);
      return;
    }
    if (bytes[0] == 'E') {
      neri_rt_v1_gc_root_frame_leave(&frame);
      return;
    }
    if (bytes[0] == 'C' || bytes[0] == 'U') {
      while (!neri_rt_v1_worker_cancelled()) sched_yield();
      if (bytes[0] == 'U') {
        neri_rt_v1_gc_root_frame_leave(&frame);
        return;
      }
    }
    bytes[1] = config[0];

    CHECK(neri_rt_v1_worker_reply(bytes, 17) == NERI_WORKER_LIMIT_V1);
    CHECK(neri_rt_v1_worker_reply(bytes, 2) == NERI_WORKER_OK_V1);
  }
  CHECK(status == NERI_WORKER_STOPPED_V1);
  neri_rt_v1_gc_root_frame_leave(&frame);
  if (config[0] == 99) {
    CHECK(neri_rt_v1_worker_fail((const uint8_t *)"cleanup", 7) == NERI_WORKER_OK_V1);
  }
}
static neri_worker_options_v1 options(unsigned workers) {
  const neri_worker_options_v1 value = {sizeof(value), workers, 2, 16, 16, 36};
  return value;
}
static int64_t open_pool(unsigned workers, uint8_t config) {
  int64_t handle;
  neri_worker_options_v1 limits = options(workers);

  CHECK(neri_rt_v1_worker_pool_open(&limits, entry, &config, 1, &handle, NULL) == NERI_WORKER_OK_V1);
  return handle;
}
static uint64_t submit(int64_t handle, uint8_t operation) {
  uint64_t id;
  uint8_t bytes[2] = {operation, 0};

  CHECK(neri_rt_v1_worker_pool_submit(handle, bytes, sizeof(bytes), &id) == NERI_WORKER_OK_V1);
  return id;
}
static neri_worker_completion_v1 completion_poll(int64_t handle) {
  neri_worker_completion_v1 completion;

  CHECK(neri_rt_v1_worker_pool_poll(handle, 5000, &completion) == NERI_WORKER_OK_V1);
  return completion;
}
static void *foreign_context(void *argument) {
  initialize();
  int64_t descriptor = 0;

  CHECK(neri_rt_v1_worker_pool_stop(*(int64_t *)argument) == NERI_WORKER_INVALID_V1);
  CHECK(neri_rt_v1_worker_pool_readiness(*(int64_t *)argument, &descriptor) == NERI_WORKER_INVALID_V1);
  CHECK(descriptor == -1);
  neri_rt_v1_interrupt_close(interrupt_token);
  neri_rt_v1_shutdown();
  return NULL;
}
static void task_context(void *argument) {
  CHECK(neri_rt_v1_worker_pool_stop(*(int64_t *)argument) == NERI_WORKER_INVALID_V1);
  reject_nested(NULL);
}
static void task_coordinator(neri_task_scope *scope, void *argument) {
  neri_task_execute(neri_task_register(scope), task_context, argument);
}
static void concurrency_and_ownership(void) {
  reset_gate();
  uint8_t config = 42;
  int64_t handle;
  neri_worker_options_v1 limits = options(2);
  limits.max_outstanding = 3; /* Saturation below is the byte reservation bound. */

  CHECK(neri_rt_v1_worker_pool_open(&limits, entry, &config, 1, &handle, NULL) == NERI_WORKER_OK_V1);
  config = 7;
  (void)submit(handle, 'G');
  (void)submit(handle, 'G');
  wait_entered(2); /* Both callbacks must overlap, not just share a queue. */
  uint64_t extra;
  uint8_t byte = 1;

  CHECK(neri_rt_v1_worker_pool_submit(handle, &byte, 1, &extra) == NERI_WORKER_FULL_V1);
  pthread_t thread;

  CHECK(pthread_create(&thread, NULL, foreign_context, &handle) == 0);
  CHECK(pthread_join(thread, NULL) == 0);
  neri_task_scope_run(task_coordinator, &handle);
  nested_probe();
  release_gate();
  for (unsigned i = 0; i < 2; ++i) {
    neri_worker_completion_v1 completion = completion_poll(handle);
    uint8_t output[2];
    if (i == 0) {
      CHECK(neri_rt_v1_worker_pool_submit(handle, &byte, 1, &extra) == NERI_WORKER_FULL_V1);
    }
    CHECK(completion.kind == NERI_WORKER_SUCCESS_V1 && completion.payload_length == 2);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, output, 1) == NERI_WORKER_LIMIT_V1);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, output, 2) == NERI_WORKER_OK_V1);
    CHECK(output[0] == 'G' && output[1] == 42);
  }
  neri_worker_close_report_v1 report;

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0);
  CHECK(neri_rt_v1_worker_pool_stop(handle) == NERI_WORKER_INVALID_V1);
}
static void cancellation(void) {
  reset_gate();
  const int64_t handle = open_pool(1, 42);
  const uint64_t active = submit(handle, 'U');
  wait_entered(1);
  const uint64_t queued = submit(handle, 'G');
  uint64_t extra;

  CHECK(neri_rt_v1_worker_pool_submit(handle, NULL, 0, &extra) == NERI_WORKER_FULL_V1);

  CHECK(neri_rt_v1_worker_pool_cancel(handle, queued) == NERI_WORKER_OK_V1);
  CHECK(neri_rt_v1_worker_pool_cancel(handle, active) == NERI_WORKER_OK_V1);
  for (unsigned i = 0; i < 2; ++i) {
    const neri_worker_completion_v1 completion = completion_poll(handle);

    CHECK(completion.kind == NERI_WORKER_CANCELLED_V1 && completion.payload_length == 0);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, NULL, 0) == NERI_WORKER_OK_V1);
  }
  neri_worker_close_report_v1 report;

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0);
}
static void count_bound(void) {
  neri_worker_options_v1 limits = options(1);
  limits.max_outstanding = 1;
  limits.max_reserved_bytes = 64;
  const uint8_t config = 42;
  int64_t handle;

  CHECK(neri_rt_v1_worker_pool_open(&limits, entry, &config, 1, &handle, NULL) == NERI_WORKER_OK_V1);
  (void)submit(handle, 'R');
  const neri_worker_completion_v1 completion = completion_poll(handle);
  uint64_t extra;

  CHECK(neri_rt_v1_worker_pool_submit(handle, NULL, 0, &extra) == NERI_WORKER_FULL_V1);
  uint8_t output[2];

  CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, output, 2) == NERI_WORKER_OK_V1);
  (void)submit(handle, 'R');
  neri_worker_close_report_v1 report;

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0);
}
static void readiness(void) {
  const int64_t handle = open_pool(1, 42);
  int64_t descriptor = -1;

  CHECK(neri_rt_v1_worker_pool_readiness(handle, &descriptor) == NERI_WORKER_OK_V1);
  CHECK(descriptor > 2);
  CHECK((fcntl((int)descriptor, F_GETFD) & FD_CLOEXEC) != 0);
  CHECK((fcntl((int)descriptor, F_GETFL) & O_NONBLOCK) != 0);
  struct pollfd ready = {(int)descriptor, POLLIN, 0};

  CHECK(poll(&ready, 1, 0) == 0);
  for (unsigned round = 0; round < 2; ++round) {
    (void)submit(handle, 'R');

    CHECK(poll(&ready, 1, 5000) == 1 && (ready.revents & POLLIN) != 0);
    const neri_worker_completion_v1 completion = completion_poll(handle);
    uint8_t output[2];

    CHECK(poll(&ready, 1, 0) == 1); /* Peeking leaves the level asserted. */
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, output, 1) == NERI_WORKER_LIMIT_V1);
    CHECK(poll(&ready, 1, 0) == 1);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, output, 2) == NERI_WORKER_OK_V1);
    neri_worker_completion_v1 empty;

    CHECK(neri_rt_v1_worker_pool_poll(handle, 0, &empty) == NERI_WORKER_EMPTY_V1);
    CHECK(poll(&ready, 1, 0) == 0);
  }
  CHECK(neri_rt_v1_worker_pool_stop(handle) == NERI_WORKER_OK_V1);
  CHECK(poll(&ready, 1, 0) == 1 && (ready.revents & POLLIN) != 0);
  neri_worker_completion_v1 completion;

  CHECK(neri_rt_v1_worker_pool_poll(handle, 0, &completion) == NERI_WORKER_STOPPED_V1);
  CHECK(poll(&ready, 1, 0) == 1);
  neri_worker_close_report_v1 report;

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0);
  CHECK(poll(&ready, 1, 0) == 1 && (ready.revents & POLLNVAL) != 0);
  CHECK(neri_rt_v1_worker_pool_readiness(handle, &descriptor) == NERI_WORKER_INVALID_V1);
  CHECK(descriptor == -1);
  CHECK(neri_rt_v1_worker_pool_readiness(handle, NULL) == NERI_WORKER_INVALID_V1);
}
static void readiness_during_stop(void) {
  reset_gate();
  const int64_t handle = open_pool(1, 42);
  int64_t descriptor;

  CHECK(neri_rt_v1_worker_pool_readiness(handle, &descriptor) == NERI_WORKER_OK_V1);
  (void)submit(handle, 'G');
  wait_entered(1);
  struct pollfd ready = {(int)descriptor, POLLIN, 0};
  neri_worker_completion_v1 completion;

  CHECK(neri_rt_v1_worker_pool_stop(handle) == NERI_WORKER_OK_V1);
  CHECK(neri_rt_v1_worker_pool_poll(handle, 0, &completion) == NERI_WORKER_EMPTY_V1);
  CHECK(poll(&ready, 1, 0) == 0); /* Active work must finish before another wake. */
  release_gate();

  CHECK(poll(&ready, 1, 5000) == 1 && (ready.revents & POLLIN) != 0);
  CHECK(neri_rt_v1_worker_pool_poll(handle, 0, &completion) == NERI_WORKER_OK_V1);
  CHECK(completion.kind == NERI_WORKER_CANCELLED_V1);
  CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, NULL, 0) == NERI_WORKER_OK_V1);
  CHECK(neri_rt_v1_worker_pool_poll(handle, 0, &completion) == NERI_WORKER_STOPPED_V1);
  CHECK(poll(&ready, 1, 0) == 1 && (ready.revents & POLLIN) != 0);
  neri_worker_close_report_v1 report;

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0);
}
static void failures_and_shutdown(void) {
  neri_worker_options_v1 limits = options(2);
  neri_worker_close_report_v1 report;
  uint8_t config = 0;
  int64_t handle;
  reset_gate();

  CHECK(neri_rt_v1_worker_pool_open(&limits, entry, &config, 1, &handle, &report) == NERI_WORKER_STARTUP_FAILED_V1);
  CHECK(handle == 0 && report.failures == 1 && report.error_length == 7);
  CHECK(memcmp(report.first_error, "startup", 7) == 0);
  reset_gate();
  handle = open_pool(1, 42);
  (void)submit(handle, 'E');
  wait_entered(1);
  (void)submit(handle, 'G');
  release_gate();
  for (unsigned i = 0; i < 2; ++i) {
    const neri_worker_completion_v1 completion = completion_poll(handle);

    CHECK(completion.kind == NERI_WORKER_FAILED_V1 && completion.payload_length == 0);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, NULL, 0) == NERI_WORKER_OK_V1);
  }
  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 1 && report.error_length != 0);
  reset_gate();
  handle = open_pool(1, 42);
  (void)submit(handle, 'F');
  wait_entered(1);
  const uint64_t queued = submit(handle, 'G');
  release_gate();
  wait_entered(2); /* Terminal failure reported, worker cleanup still blocked. */
  uint64_t extra;

  CHECK(neri_rt_v1_worker_pool_submit(handle, NULL, 0, &extra) == NERI_WORKER_STOPPED_V1);
  for (unsigned i = 0; i < 2; ++i) {
    const neri_worker_completion_v1 completion = completion_poll(handle);

    CHECK(completion.kind == NERI_WORKER_FAILED_V1 && completion.payload_length == 0);
    if (i == 1) CHECK(completion.job_id == queued);
    CHECK(neri_rt_v1_worker_pool_take(handle, completion.job_id, NULL, 0) == NERI_WORKER_OK_V1);
  }
  release_gate();

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 1 && report.error_length == 8);
  CHECK(memcmp(report.first_error, "terminal", 8) == 0);
  handle = open_pool(1, 99);

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 1 && report.error_length == 7);
  CHECK(memcmp(report.first_error, "cleanup", 7) == 0);
  reset_gate();
  handle = open_pool(1, 42);
  (void)submit(handle, 'C');
  wait_entered(1);
  (void)submit(handle, 'G');

  CHECK(neri_rt_v1_worker_pool_close(handle, &report) == NERI_WORKER_OK_V1);
  CHECK(report.failures == 0); /* Stop wakes active cancellation and cancels queue. */
  handle = open_pool(1, 42);
  neri_rt_v1_shutdown(); /* Owned idle worker must be stopped and joined. */
  initialize();

  CHECK(neri_rt_v1_worker_pool_stop(handle) == NERI_WORKER_INVALID_V1);
}
int main(void) {
  initialize();
  interrupt_token = neri_rt_v1_interrupt_open();

  CHECK(interrupt_token > 0);
  CHECK(!neri_worker_thread_active());
  concurrency_and_ownership();

  CHECK(neri_rt_v1_interrupt_open() == 0); /* Foreign/worker close did not release it. */
  cancellation();
  count_bound();
  readiness();
  readiness_during_stop();
  failures_and_shutdown();
  neri_rt_v1_interrupt_close(interrupt_token);
  neri_rt_v1_shutdown();
  puts("worker pool contracts passed");
  return 0;
}
