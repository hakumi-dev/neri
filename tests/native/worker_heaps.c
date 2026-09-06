#include "neri/runtime_abi.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/* Native entry callbacks are not expressible in Neri's current C ABI. This
 * probe exercises the worker heap boundary, not a public task API. */
#define CHECK(value) do { if (!(value)) abort(); } while (0)

typedef struct node_payload {
  neri_ref_v1 next;
  int64_t value;
} node_payload;

static node_payload *payload(neri_ref_v1 object) {
  return (node_payload *)((unsigned char *)object + sizeof(*object));
}

static void trace(neri_ref_v1 object, neri_gc_visit_slot_fn_v1 visit, void *context) {
  visit(&payload(object)->next, context);
}

static const neri_type_descriptor_v1 node_type = {
  sizeof(neri_type_descriptor_v1), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
  NERI_TYPE_KIND_CLASS_V1, NERI_TYPE_FLAG_CONTAINS_REFS_V1,
  sizeof(node_payload), _Alignof(node_payload), 0, 0, trace, NULL,
  "worker-node", 0, NULL, NULL, NERI_SCALAR_KIND_NONE_V1, 0
};

static void initialize(void) {
  const neri_runtime_abi_requirements_v1 requirements = {
    sizeof(requirements), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
    NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_ROOT_FRAMES
  };
  CHECK(neri_rt_v1_initialize(&requirements) == NERI_ABI_STATUS_OK_V1);
}

static neri_ref_v1 allocate(void) {
  return neri_rt_v1_gc_alloc(&node_type, sizeof(node_payload), _Alignof(node_payload));
}

static neri_gc_stats_v1 stats(void) {
  neri_gc_stats_v1 result = {sizeof(result), 0, 0, 0, 0, 0};
  neri_rt_v1_gc_get_stats(&result);
  return result;
}

static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static unsigned ready;

static void *worker(void *argument) {
  const int64_t identity = (int64_t)(intptr_t)argument;
  initialize();
  CHECK(stats().managed_object_count == 0);
  neri_ref_v1 roots[2] = {NULL, NULL};
  neri_gc_root_frame_v1 frame = {NULL, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  CHECK(pthread_mutex_lock(&gate_mutex) == 0);
  ++ready;
  CHECK(pthread_cond_broadcast(&gate_condition) == 0);
  while (ready != 4) CHECK(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
  CHECK(pthread_mutex_unlock(&gate_mutex) == 0);

  for (unsigned round = 0; round < 64; ++round) {
    roots[0] = allocate();
    roots[1] = allocate();
    payload(roots[0])->value = identity;
    neri_rt_v1_gc_store_ref(roots[0], &payload(roots[0])->next, roots[1]);
    neri_rt_v1_gc_store_ref(roots[1], &payload(roots[1])->next, roots[0]);
    for (unsigned garbage = 0; garbage < 128; ++garbage) (void)allocate();
    neri_rt_v1_gc_collect();
    CHECK(stats().managed_object_count == 2);
    CHECK(payload(payload(roots[1])->next)->value == identity);
    roots[0] = roots[1] = NULL;
    neri_rt_v1_gc_collect();
    CHECK(stats().managed_object_count == 0);
  }
  neri_rt_v1_gc_root_frame_leave(&frame);
  neri_rt_v1_shutdown();
  return NULL;
}

typedef struct foreign_case {
  neri_ref_v1 reference;
  int store;
} foreign_case;

static void *foreign_worker(void *argument) {
  const foreign_case *test = argument;
  initialize();
  if (test->store) {
    neri_ref_v1 local = allocate();
    neri_rt_v1_gc_store_ref(local, &payload(local)->next, test->reference);
  } else {
    neri_ref_v1 root = test->reference;
    neri_gc_root_frame_v1 frame = {NULL, &root, 1, 0};
    neri_rt_v1_gc_root_frame_enter(&frame);
    neri_rt_v1_gc_collect();
  }
  return NULL;
}

static void reject_foreign_reference(neri_ref_v1 reference, int store) {
  const pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    foreign_case test = {reference, store};
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, foreign_worker, &test) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    _Exit(1);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == NERI_RUNTIME_PANIC_EXIT_CODE_V1);
}

int main(void) {
  initialize();
  neri_ref_v1 root = allocate();
  neri_gc_root_frame_v1 frame = {NULL, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  payload(root)->value = 42;
  reject_foreign_reference(root, 1);
  reject_foreign_reference(root, 0);

  pthread_t threads[4];
  for (intptr_t index = 0; index < 4; ++index)
    CHECK(pthread_create(&threads[index], NULL, worker, (void *)(index + 1)) == 0);
  for (unsigned index = 0; index < 4; ++index)
    CHECK(pthread_join(threads[index], NULL) == 0);

  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 1 && payload(root)->value == 42);
  neri_rt_v1_gc_root_frame_leave(&frame);
  neri_rt_v1_shutdown();
  return 0;
}
