#ifndef NERI_TASK_HEAP_INTERNAL_H
#define NERI_TASK_HEAP_INTERNAL_H

#include "neri/runtime_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal scheduler boundary, not a language task API or a versioned ABI.
 * The coordinator runs with its managed heap suspended. Register each task
 * before publishing it to an executor, and consume every ticket exactly once.
 * The scope waits for all registered tasks before resuming the parent heap.
 * Task bodies receive a fresh heap and read access to suspended ancestors;
 * they must return with no live native borrows or body-owned root frames.
 * Callbacks must return normally or terminate the process; they must not throw
 * or longjmp across this boundary. Raw C access
 * still requires trusted callbacks. */
typedef struct neri_task_scope neri_task_scope;
typedef struct neri_task_ticket neri_task_ticket;
typedef void (*neri_task_coordinator)(neri_task_scope *, void *);
typedef void (*neri_task_body)(void *);

void neri_task_scope_run(neri_task_coordinator coordinate, void *context);
neri_task_ticket *neri_task_register(neri_task_scope *scope);
/* Each result span is initialized, writable reference storage valid through
 * join. Spans must be disjoint and inaccessible to other task bodies/captures.
 * The child roots its span; only after all tasks finish does the parent adopt
 * reachable child allocations, preserving addresses. The caller must keep the
 * results rooted before subsequent managed operations. Native allocations and
 * references from sibling heaps are not transferable through this boundary. */
neri_task_ticket *neri_task_register_results(neri_task_scope *scope,
                                            neri_ref_v1 *slots, uint64_t count);
void neri_task_execute(neri_task_ticket *ticket, neri_task_body body, void *context);

/* A joined range group, using the same trusted capture/result-span contract.
 * The body receives disjoint half-open ranges covering [0, count) exactly once.
 * Result storage has count * references_per_item slots, in input order.
 * The outer call bounds its pool by positive parallelism, hardware availability
 * and count, including the caller. Nested groups reuse that pool; parallelism
 * limits each group's active range invocations (including suspended invocations),
 * not all invocations in its descendant groups. Waiting coordinators help queued
 * work; callbacks must not wait on siblings or require a particular execution
 * order. */
typedef void (*neri_task_range_body)(uint64_t begin, uint64_t end, void *context);
void neri_task_parallel_for(uint64_t count, uint32_t parallelism,
                            neri_task_range_body body, void *context,
                            neri_ref_v1 *results, uint64_t references_per_item);

#ifdef __cplusplus
}
#endif
#endif
