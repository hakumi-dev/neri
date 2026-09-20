#include "neri/runtime_abi.h"

#include <stdlib.h>
#if !defined(_WIN32)
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* C owns uninitialized entry storage, foreign threads and abnormal control
 * flow here; these runtime boundary contracts cannot be exercised in Neri. */
#define CHECK(value) do { if (!(value)) abort(); } while (0)

_Static_assert(sizeof(neri_foreign_entry_v1) == 6 * sizeof(uintptr_t), "entry size");
_Static_assert(_Alignof(neri_foreign_entry_v1) == _Alignof(uintptr_t), "entry alignment");

static const neri_runtime_abi_requirements_v1 requirements = {
  sizeof(requirements), NERI_RUNTIME_ABI_MAJOR, 27,
  NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_ROOT_FRAMES
};

static neri_gc_stats_v1 stats(void) {
  neri_gc_stats_v1 result = {sizeof(result), 0, 0, 0, 0, 0};
  neri_rt_v1_gc_get_stats(&result);
  return result;
}

static void nested_entry(void) {
  neri_ref_v1 root = neri_rt_v1_string_from_int(42);
  neri_gc_root_frame_v1 frame = {NULL, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  neri_gc_borrow_v1 borrow = {{0}};
  const uint64_t *length = neri_rt_v1_gc_borrow_begin(root, 0, sizeof(uint64_t), &borrow);
  neri_foreign_entry_v1 inner;
  neri_rt_v1_foreign_enter(&inner, &requirements);
  (void)neri_rt_v1_string_from_int(99);
  neri_rt_v1_gc_collect();

  CHECK(stats().managed_object_count == 1);
  CHECK(*length == 2);

  neri_rt_v1_foreign_leave(&inner);
  neri_rt_v1_gc_borrow_end(&borrow);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void temporary_entry(void) {
  neri_foreign_entry_v1 outer;
  neri_rt_v1_foreign_enter(&outer, &requirements);

  CHECK(stats().managed_object_count == 0);
  CHECK(stats().native_byte_count == 0);

  nested_entry();
  (void)neri_rt_v1_native_alloc(16, 8);
  neri_rt_v1_foreign_leave(&outer);
  /* Reuse of completed token storage starts with an empty heap. */
  neri_rt_v1_foreign_enter(&outer, &requirements);

  CHECK(stats().managed_object_count == 0);
  CHECK(stats().native_byte_count == 0);

  neri_rt_v1_foreign_leave(&outer);
}

static void host_owned_entry(void) {
  const char *arguments[] = {"host", "retained"};

  CHECK(neri_rt_v1_initialize(&requirements) == NERI_ABI_STATUS_OK_V1);

  neri_rt_v1_set_process_arguments(2, arguments);
  neri_foreign_entry_v1 entry;
  neri_rt_v1_foreign_enter(&entry, &requirements);
  unsigned char *memory = neri_rt_v1_native_alloc(16, 8);
  memory[0] = 42;
  nested_entry();
  neri_rt_v1_foreign_leave(&entry);

  CHECK(stats().native_byte_count == 16);
  CHECK(memory[0] == 42);
  CHECK(neri_rt_v1_host_argument_count() == 1);

  neri_rt_v1_native_free(memory);
  neri_rt_v1_shutdown();
}

#if !defined(_WIN32)
static void *worker(void *argument) {
  (void)argument;
  for (unsigned round = 0; round < 32; ++round) temporary_entry();
  return NULL;
}

static void foreign_threads(void) {
  pthread_t threads[4];
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(pthread_create(&threads[index], NULL, worker, NULL) == 0);
  }
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(pthread_join(threads[index], NULL) == 0);
  }
}

static void *wrong_thread(void *argument) {
  neri_foreign_entry_v1 local;
  neri_rt_v1_foreign_enter(&local, &requirements);
  neri_rt_v1_foreign_leave(argument);
  return NULL;
}

static void misuse(unsigned mode) {
  neri_foreign_entry_v1 outer, inner;
  neri_rt_v1_foreign_enter(&outer, &requirements);
  if (mode == 0) {
    neri_rt_v1_foreign_enter(&inner, &requirements);
    neri_rt_v1_foreign_enter(&outer, &requirements);
  } else if (mode == 1) {
    neri_rt_v1_foreign_enter(&inner, &requirements);
    neri_rt_v1_foreign_leave(&outer);
  } else if (mode == 2) {
    neri_rt_v1_shutdown();
  } else if (mode == 3) {
    neri_gc_root_frame_v1 frame = {NULL, NULL, 0, 0};
    neri_rt_v1_gc_root_frame_enter(&frame);
    neri_rt_v1_foreign_leave(&outer);
  } else if (mode == 4) {
    neri_ref_v1 value = neri_rt_v1_string_from_int(42);
    neri_gc_borrow_v1 borrow = {{0}};
    (void)neri_rt_v1_gc_borrow_begin(value, 0, sizeof(uint64_t), &borrow);
    neri_rt_v1_foreign_leave(&outer);
  } else if (mode == 5) {
    pthread_t thread;

    CHECK(pthread_create(&thread, NULL, wrong_thread, &outer) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
  } else if (mode == 6) {
    neri_runtime_abi_requirements_v1 incompatible = requirements;
    incompatible.minimum_minor = UINT16_MAX;
    neri_rt_v1_foreign_enter(&inner, &incompatible);
  } else {
    neri_rt_v1_foreign_leave(&outer);
    neri_rt_v1_foreign_leave(&outer);
  }
  _Exit(1);
}

static void rejected_entries(void) {
  for (unsigned mode = 0; mode < 8; ++mode) {
    const pid_t child = fork();

    CHECK(child >= 0);

    if (child == 0) misuse(mode);
    int status = 0;

    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == NERI_RUNTIME_PANIC_EXIT_CODE_V1);
  }
}
#endif

int main(void) {
  temporary_entry();
  host_owned_entry();
#if !defined(_WIN32)
  rejected_entries();
  foreign_threads();
#endif
  return 0;
}
