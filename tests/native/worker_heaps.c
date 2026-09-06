#include "neri/runtime_abi.h"
#include "task_heap.h"

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
  neri_ref_v1 shared;
  int64_t value;
} node_payload;

static node_payload *payload(neri_ref_v1 object) {
  return (node_payload *)((unsigned char *)object + sizeof(*object));
}

static void trace(neri_ref_v1 object, neri_gc_visit_slot_fn_v1 visit, void *context) {
  visit(&payload(object)->next, context);
  visit(&payload(object)->shared, context);
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

static const neri_type_descriptor_v1 int_type = {
  sizeof(neri_type_descriptor_v1), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
  NERI_TYPE_KIND_SCALAR_V1, 0, sizeof(int64_t), _Alignof(int64_t),
  0, 0, NULL, NULL, "task-int", 0, NULL, NULL, NERI_SCALAR_KIND_INT_V1, 0
};

static const neri_type_descriptor_v1 array_type = {
  sizeof(neri_type_descriptor_v1), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
  NERI_TYPE_KIND_ARRAY_V1, 0, sizeof(uint64_t), _Alignof(uint64_t),
  sizeof(int64_t), _Alignof(int64_t), NULL, NULL, "task-array",
  0, &int_type, NULL, NERI_SCALAR_KIND_NONE_V1, 0
};

typedef struct task_case {
  neri_task_ticket *ticket;
  neri_ref_v1 ancestor;
  neri_ref_v1 text;
  neri_ref_v1 values;
  int complete;
  pthread_t thread;
} task_case;

static void nested_body(void *argument) {
  const task_case *test = argument;
  neri_ref_v1 roots[2] = {test->ancestor, allocate()};
  neri_gc_root_frame_v1 frame = {NULL, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  neri_rt_v1_gc_store_ref(roots[1], &payload(roots[1])->next, roots[0]);
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 1);
  CHECK(payload(roots[0])->value == 42);
  CHECK(neri_rt_v1_host_string_byte_length(test->text) == 2);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void nested_coordinator(neri_task_scope *scope, void *context) {
  /* Helping on the coordinator's thread preserves both suspended contexts. */
  neri_task_execute(neri_task_register(scope), nested_body, context);
}

static void task_body(void *argument) {
  task_case *test = argument;
  CHECK(stats().managed_object_count == 0);
  neri_ref_v1 roots[3] = {NULL, NULL, NULL};
  neri_gc_root_frame_v1 frame = {NULL, roots, 3, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  roots[0] = allocate();
  neri_rt_v1_gc_store_ref(roots[0], &payload(roots[0])->next, test->ancestor);
  roots[1] = neri_rt_v1_string_concat(test->text, test->text);
  roots[2] = neri_rt_v1_host_append_int(test->values, 9);
  for (unsigned round = 0; round < 32; ++round) {
    for (unsigned garbage = 0; garbage < 64; ++garbage) (void)allocate();
    neri_rt_v1_gc_collect();
    CHECK(stats().managed_object_count == 3);
    CHECK(payload(payload(roots[0])->next)->value == 42);
  }
  CHECK(neri_rt_v1_host_string_byte_length(roots[1]) == 4);
  const neri_array_prefix_v1 *array = (const neri_array_prefix_v1 *)roots[2];
  const int64_t *elements = (const int64_t *)(array + 1);
  CHECK(array->length == 3 && elements[0] == 4 && elements[1] == 5 && elements[2] == 9);
  neri_task_scope_run(nested_coordinator, test);
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 3);
  neri_rt_v1_gc_root_frame_leave(&frame);
  test->complete = 1;
}

static void *task_worker(void *argument) {
  task_case *test = argument;
  neri_task_execute(test->ticket, task_body, test);
  return NULL;
}

static void task_coordinator(neri_task_scope *scope, void *context) {
  task_case *tests = context;
  for (unsigned index = 0; index < 4; ++index) {
    tests[index].ticket = neri_task_register(scope);
    CHECK(pthread_create(&tests[index].thread, NULL, task_worker, &tests[index]) == 0);
  }
  /* Return without joining: the runtime scope must wait for all tickets. */
}

static void check_scoped_reads(neri_ref_v1 ancestor) {
  neri_ref_v1 roots[2] = {NULL, NULL};
  neri_gc_root_frame_v1 frame = {NULL, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  roots[0] = neri_rt_v1_string_from_int(42);
  roots[1] = neri_rt_v1_gc_alloc(&array_type, sizeof(uint64_t) + 2 * sizeof(int64_t), _Alignof(uint64_t));
  neri_array_prefix_v1 *array = (neri_array_prefix_v1 *)roots[1];
  array->length = 2;
  int64_t *elements = (int64_t *)(array + 1);
  elements[0] = 4;
  elements[1] = 5;
  const uint64_t parent_count = stats().managed_object_count;
  task_case tests[4] = {0};
  for (unsigned index = 0; index < 4; ++index) {
    tests[index].ancestor = ancestor;
    tests[index].text = roots[0];
    tests[index].values = roots[1];
  }
  neri_task_scope_run(task_coordinator, tests);
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(tests[index].complete == 1);
    CHECK(pthread_join(tests[index].thread, NULL) == 0);
  }
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == parent_count);
  CHECK(array->length == 2 && elements[0] == 4 && elements[1] == 5);
  payload(ancestor)->value = 43;
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void write_ancestor(void *context) {
  neri_ref_v1 ancestor = context;
  neri_rt_v1_gc_store_ref(ancestor, &payload(ancestor)->next, NULL);
}

static void borrow_ancestor(void *context) {
  neri_gc_borrow_v1 borrow = {{0}};
  (void)neri_rt_v1_gc_borrow_begin(context, 0, 1, &borrow);
}

typedef struct rejected_task {
  neri_task_body body;
  neri_ref_v1 ancestor;
} rejected_task;

static void rejected_coordinator(neri_task_scope *scope, void *context) {
  const rejected_task *test = context;
  if (test->body == NULL) {
    neri_rt_v1_gc_collect();
  } else {
    neri_task_execute(neri_task_register(scope), test->body, test->ancestor);
  }
}

static void reject_scoped_write(neri_ref_v1 ancestor, neri_task_body body) {
  const pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    rejected_task test = {body, ancestor};
    neri_task_scope_run(rejected_coordinator, &test);
    _Exit(1);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == NERI_RUNTIME_PANIC_EXIT_CODE_V1);
}

typedef struct result_case {
  neri_task_ticket *ticket;
  neri_ref_v1 ancestor;
  neri_ref_v1 *slots;
  neri_ref_v1 produced;
  pthread_t thread;
  neri_task_body body;
} result_case;

static void result_body(void *context) {
  result_case *test = context;
  /* The runtime roots the output span throughout allocation and final GC. */
  test->slots[0] = allocate();
  neri_ref_v1 other = allocate();
  neri_rt_v1_gc_store_ref(test->slots[0], &payload(test->slots[0])->next, other);
  neri_rt_v1_gc_store_ref(other, &payload(other)->next, test->slots[0]);
  neri_rt_v1_gc_store_ref(test->slots[0], &payload(test->slots[0])->shared, test->ancestor);
  test->slots[1] = test->slots[0];
  for (unsigned index = 0; index < 256; ++index) (void)allocate();
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 2);
  (void)allocate(); /* Final collection must discard non-result objects. */
  (void)neri_rt_v1_native_alloc(64, 8);
  test->produced = test->slots[0];
}

static void nested_result_coordinator(neri_task_scope *scope, void *context) {
  result_case *test = context;
  neri_task_execute(neri_task_register_results(scope, test->slots, 2), result_body, test);
}

static void nested_result_body(void *context) {
  neri_task_scope_run(nested_result_coordinator, context);
  CHECK(stats().managed_object_count == 2 && stats().native_byte_count == 0);
}

static void *result_worker(void *context) {
  result_case *test = context;
  neri_task_execute(test->ticket, nested_result_body, test);
  return NULL;
}

static void result_coordinator(neri_task_scope *scope, void *context) {
  result_case *tests = context;
  for (unsigned index = 0; index < 4; ++index) {
    tests[index].ticket = neri_task_register_results(scope, tests[index].slots, 2);
    CHECK(pthread_create(&tests[index].thread, NULL, result_worker, &tests[index]) == 0);
  }
}

static void check_result_adoption(neri_ref_v1 ancestor) {
  neri_ref_v1 slots[8] = {NULL};
  neri_gc_root_frame_v1 frame = {NULL, slots, 8, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  result_case tests[4] = {0};
  for (unsigned index = 0; index < 4; ++index) {
    tests[index].ancestor = ancestor;
    tests[index].slots = &slots[index * 2];
  }
  neri_task_scope_run(result_coordinator, tests);
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(pthread_join(tests[index].thread, NULL) == 0);
    neri_ref_v1 root = slots[index * 2];
    CHECK(root == tests[index].produced && root == slots[index * 2 + 1]);
    CHECK(payload(payload(root)->next)->next == root && payload(root)->shared == ancestor);
  }
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 9 && stats().native_byte_count == 0);
  /* A parent-owned store breaks one returned cycle; the detached node dies. */
  neri_rt_v1_gc_store_ref(slots[0], &payload(slots[0])->next, ancestor);
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 8);
  for (unsigned index = 0; index < 8; ++index) slots[index] = NULL;
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 1);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void sibling_result_body(void *context) {
  neri_ref_v1 *slots = context;
  slots[2] = slots[0];
}

static void sibling_result_coordinator(neri_task_scope *scope, void *context) {
  result_case *test = context;
  neri_task_execute(neri_task_register_results(scope, test->slots, 2), result_body, test);
  /* Completed results still belong to the first child until the scope joins. */
  neri_task_execute(neri_task_register_results(scope, &test->slots[2], 1), sibling_result_body, test->slots);
}

static void leak_result_root(void *context) {
  result_case *test = context;
  test->slots[0] = allocate();
  neri_gc_root_frame_v1 frame = {NULL, test->slots, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
}

static void leak_result_borrow(void *context) {
  result_case *test = context;
  test->slots[0] = allocate();
  neri_gc_borrow_v1 borrow = {{0}};
  (void)neri_rt_v1_gc_borrow_begin(test->slots[0], 0, 1, &borrow);
}

static void result_boundary_coordinator(neri_task_scope *scope, void *context) {
  result_case *test = context;
  if (test->body == NULL) {
    sibling_result_coordinator(scope, context);
  } else {
    neri_task_execute(neri_task_register_results(scope, test->slots, 2), test->body, test);
  }
}

static void reject_result_boundary(neri_ref_v1 ancestor, neri_task_body body) {
  const pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    neri_ref_v1 slots[3] = {NULL};
    result_case test = {0};
    test.ancestor = ancestor;
    test.slots = slots;
    test.body = body;
    neri_task_scope_run(result_boundary_coordinator, &test);
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
  reject_scoped_write(root, write_ancestor);
  reject_scoped_write(root, borrow_ancestor);
  reject_scoped_write(root, NULL);
  check_scoped_reads(root);
  neri_rt_v1_gc_collect();
  CHECK(stats().managed_object_count == 1 && payload(root)->value == 43);
  reject_result_boundary(root, NULL);
  reject_result_boundary(root, leak_result_root);
  reject_result_boundary(root, leak_result_borrow);
  check_result_adoption(root);
  neri_rt_v1_gc_root_frame_leave(&frame);
  neri_rt_v1_shutdown();
  return 0;
}
