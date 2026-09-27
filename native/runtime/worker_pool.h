#ifndef NERI_WORKER_POOL_INTERNAL_H
#define NERI_WORKER_POOL_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif
int neri_worker_thread_active(void);
/* Active heap identity; NULL outside an active runtime context. */
const void *neri_worker_heap_identity(void);
/* Called before an owning heap is reclaimed. Stops and joins every owned pool. */
void neri_worker_close_owned(const void *heap);
#ifdef __cplusplus
}
#endif
#endif
