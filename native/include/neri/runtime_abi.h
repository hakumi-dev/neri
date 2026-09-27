#ifndef NERI_RUNTIME_ABI_H
#define NERI_RUNTIME_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32)
#if defined(NERI_RUNTIME_STATIC)
#define NERI_RT_API
#elif defined(NERI_RUNTIME_BUILD)
#define NERI_RT_API __declspec(dllexport)
#else
#define NERI_RT_API __declspec(dllimport)
#endif
#define NERI_RT_NORETURN __declspec(noreturn)
#else
#define NERI_RT_API __attribute__((visibility("default")))
#define NERI_RT_NORETURN __attribute__((noreturn))
#endif

#include "neri/abi_catalog.h"

#define NERI_TYPE_KIND_CLASS_V1 UINT32_C(1)
#define NERI_TYPE_KIND_STRING_V1 UINT32_C(2)
#define NERI_TYPE_KIND_ARRAY_V1 UINT32_C(3)
#define NERI_TYPE_KIND_INLINE_AGGREGATE_V1 UINT32_C(4)
#define NERI_TYPE_KIND_SCALAR_V1 UINT32_C(5)

#define NERI_SCALAR_KIND_NONE_V1 UINT32_C(0)
#define NERI_SCALAR_KIND_BOOL_V1 UINT32_C(1)
#define NERI_SCALAR_KIND_BYTE_V1 UINT32_C(2)
#define NERI_SCALAR_KIND_INT_V1 UINT32_C(3)
#define NERI_SCALAR_KIND_FLOAT_V1 UINT32_C(4)
#define NERI_SCALAR_KIND_INT32_V1 UINT32_C(5)
#define NERI_SCALAR_KIND_UINT32_V1 UINT32_C(6)
#define NERI_SCALAR_KIND_UINT64_V1 UINT32_C(7)
#define NERI_SCALAR_KIND_FLOAT32_V1 UINT32_C(8)

#define NERI_TYPE_FLAG_CONTAINS_REFS_V1 UINT32_C(1)
#define NERI_TYPE_FLAG_IMMUTABLE_V1 (UINT32_C(1) << 1)

#define NERI_ABI_STATUS_OK_V1 INT32_C(0)
#define NERI_ABI_STATUS_INVALID_ARGUMENT_V1 INT32_C(1)
#define NERI_ABI_STATUS_INCOMPATIBLE_MAJOR_V1 INT32_C(2)
#define NERI_ABI_STATUS_RUNTIME_TOO_OLD_V1 INT32_C(3)
#define NERI_ABI_STATUS_MISSING_FEATURE_V1 INT32_C(4)

#define NERI_PANIC_BOUNDS_V1 UINT32_C(1)
#define NERI_PANIC_ARITHMETIC_V1 UINT32_C(2)
#define NERI_PANIC_OUT_OF_MEMORY_V1 UINT32_C(3)
#define NERI_PANIC_ABI_MISMATCH_V1 UINT32_C(4)
#define NERI_PANIC_RUNTIME_CONTRACT_V1 UINT32_C(5)
#define NERI_RUNTIME_PANIC_EXIT_CODE_V1 70

typedef uint8_t neri_bool_v1;
typedef uint8_t neri_byte_v1;
typedef int64_t neri_int_v1;
typedef double neri_float_v1;
typedef int32_t neri_abi_status_v1;

typedef struct neri_type_descriptor_v1 neri_type_descriptor_v1;

typedef struct neri_object_header_v1 {
  const neri_type_descriptor_v1 *type;
  uintptr_t runtime_word;
} neri_object_header_v1;

typedef neri_object_header_v1 *neri_ref_v1;

typedef neri_ref_v1 (*neri_session_entry_fn_v1)(neri_ref_v1 previous);

typedef struct neri_session_layout_v1 {
  const char *type_id;
  const char *canonical_layout;
  uint32_t kind;
  uint32_t flags;
  uint64_t payload_size;
  uint64_t payload_alignment;
  const uint64_t *trace_offsets;
  uint64_t trace_offset_count;
} neri_session_layout_v1;

typedef struct neri_session_module_metadata_v1 {
  uint32_t struct_size;
  uint16_t version_major;
  uint16_t version_minor;
  const char *entry_name;
  neri_session_entry_fn_v1 entry;
  const char *source_type_id;
  const char *target_type_id;
  const neri_session_layout_v1 *layouts;
  uint64_t layout_count;
  uint64_t display_offset;
  uint64_t flags;
} neri_session_module_metadata_v1;

#define NERI_SESSION_MODULE_NULLABLE_ENTRY_V1 UINT64_C(1)

typedef const neri_session_module_metadata_v1 *(*neri_session_module_accessor_v1)(void);

#define NERI_SESSION_OK_V1 INT64_C(0)
#define NERI_SESSION_INVALID_HANDLE_V1 INT64_C(1)
#define NERI_SESSION_LOAD_FAILED_V1 INT64_C(2)
#define NERI_SESSION_INVALID_METADATA_V1 INT64_C(3)
#define NERI_SESSION_INCOMPATIBLE_LAYOUT_V1 INT64_C(4)
#define NERI_SESSION_INCOMPATIBLE_FRAME_V1 INT64_C(5)
#define NERI_SESSION_INVOKE_STATE_V1 INT64_C(6)

NERI_RT_API neri_int_v1 neri_rt_v1_session_create(void);
NERI_RT_API neri_int_v1 neri_rt_v1_session_identity(void);
NERI_RT_API neri_ref_v1 neri_rt_v1_session_owner(neri_int_v1 handle);
NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute(
    neri_int_v1 handle, neri_ref_v1 module_path);
NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute_retained(
    neri_int_v1 handle, neri_ref_v1 module_path,
    neri_ref_v1 artifact_identity);
NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute_object(
    neri_int_v1 handle, neri_ref_v1 object_path,
    neri_ref_v1 artifact_identity, neri_ref_v1 linker_path);
NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute_object_libraries(
    neri_int_v1 handle, neri_ref_v1 object_path,
    neri_ref_v1 artifact_identity, neri_ref_v1 linker_path,
    neri_ref_v1 native_libraries);
NERI_RT_API neri_int_v1 neri_rt_v1_session_reset(neri_int_v1 handle);
NERI_RT_API neri_int_v1 neri_rt_v1_session_destroy(neri_int_v1 handle);
NERI_RT_API neri_ref_v1 neri_rt_v1_session_error(neri_int_v1 handle);
NERI_RT_API neri_ref_v1 neri_rt_v1_session_result(neri_int_v1 handle);
NERI_RT_API void neri_rt_v1_session_fail(neri_ref_v1 message);

typedef struct neri_optional_bool_v1 {
  neri_bool_v1 has_value;
  neri_bool_v1 value;
} neri_optional_bool_v1;

typedef struct neri_optional_byte_v1 {
  neri_bool_v1 has_value;
  neri_byte_v1 value;
} neri_optional_byte_v1;

typedef struct neri_optional_int_v1 {
  neri_bool_v1 has_value;
  uint8_t reserved[7];
  neri_int_v1 value;
} neri_optional_int_v1;

typedef struct neri_optional_float_v1 {
  neri_bool_v1 has_value;
  uint8_t reserved[7];
  neri_float_v1 value;
} neri_optional_float_v1;

typedef struct neri_string_prefix_v1 {
  neri_object_header_v1 header;
  uint64_t byte_length;
} neri_string_prefix_v1;

typedef struct neri_array_prefix_v1 {
  neri_object_header_v1 header;
  uint64_t length;
} neri_array_prefix_v1;

typedef void (*neri_gc_visit_slot_fn_v1)(neri_ref_v1 *slot, void *context);
typedef void (*neri_gc_trace_fn_v1)(neri_ref_v1 object,
                                      neri_gc_visit_slot_fn_v1 visit,
                                      void *context);
typedef void (*neri_gc_trace_inline_fn_v1)(void *storage,
                                             neri_gc_visit_slot_fn_v1 visit,
                                             void *context);

struct neri_type_descriptor_v1 {
  uint32_t struct_size;
  uint16_t abi_major;
  uint16_t abi_minor;
  uint32_t kind;
  uint32_t flags;
  uint64_t payload_size;
  uint64_t payload_alignment;
  uint64_t element_size;
  uint64_t element_alignment;
  neri_gc_trace_fn_v1 trace;
  const void *method_table;
  const char *mangled_name;
  uintptr_t reserved;
  const neri_type_descriptor_v1 *element_type;
  neri_gc_trace_inline_fn_v1 trace_inline;
  uint32_t scalar_kind;
  uint32_t reserved_v1_4;
};

#define NERI_TYPE_DESCRIPTOR_V1_BASE_SIZE UINT32_C(80)
#define NERI_TYPE_DESCRIPTOR_V1_4_SIZE UINT32_C(104)

typedef struct neri_runtime_abi_requirements_v1 {
  uint32_t struct_size;
  uint16_t major;
  uint16_t minimum_minor;
  uint64_t required_features;
} neri_runtime_abi_requirements_v1;

typedef struct neri_runtime_abi_info_v1 {
  uint32_t struct_size;
  uint16_t major;
  uint16_t minor;
  uint64_t features;
} neri_runtime_abi_info_v1;

typedef struct neri_gc_root_frame_v1 {
  struct neri_gc_root_frame_v1 *previous;
  neri_ref_v1 *slots;
  uint64_t slot_count;
  uintptr_t runtime_cookie;
} neri_gc_root_frame_v1;

typedef struct neri_gc_borrow_v1 {
  uintptr_t runtime_words[4];
} neri_gc_borrow_v1;

typedef struct neri_foreign_entry_v1 {
  uintptr_t runtime_words[6];
} neri_foreign_entry_v1;

typedef struct neri_gc_stats_v1 {
  uint32_t struct_size;
  uint32_t reserved;
  uint64_t managed_object_count;
  uint64_t managed_byte_count;
  uint64_t collection_count;
  uint64_t native_byte_count;
} neri_gc_stats_v1;

typedef struct neri_source_location_v1 {
  const uint8_t *source_name;
  uint64_t source_name_length;
  uint32_t utf8_start;
  uint32_t utf8_length;
} neri_source_location_v1;

typedef struct neri_panic_v1 {
  uint32_t code;
  uint32_t reserved;
  const uint8_t *message;
  uint64_t message_length;
} neri_panic_v1;

NERI_RT_API extern const unsigned char neri_rt_v1_abi_anchor;

/* Emitted once by neri-codegen and consumed by the runtime entry point. */
extern const neri_runtime_abi_requirements_v1
    neri_program_v1_abi_requirements;

NERI_RT_API const neri_runtime_abi_info_v1 *neri_rt_v1_get_abi(void);
/* Initialization, roots, heaps, arguments and shutdown belong to the calling
 * native thread. Each thread must initialize and shut down explicitly. Live
 * managed references remain in their owning heap; no transfer is implied.
 * MULTIPLE_MUTATORS remains reserved: these disjoint heaps do not provide
 * shared-heap collection or a language task/capture contract. */
NERI_RT_API neri_abi_status_v1
neri_rt_v1_initialize(const neri_runtime_abi_requirements_v1 *requirements);
NERI_RT_API void neri_rt_v1_set_process_arguments(int argc,
                                                       const char *const *argv);
NERI_RT_API void neri_rt_v1_shutdown(void);

/* ABI 1.27: synchronous C entry on the calling native thread. The token needs
 * writable, pointer-aligned storage and requires no initial contents. Tokens
 * are thread-confined and leave in LIFO order with balanced roots and borrows.
 * An entry reuses an initialized heap, preserving its ownership and state;
 * otherwise it owns a temporary heap reclaimed on leave. Managed and native
 * allocations from that temporary heap must not escape. Hosts requiring longer
 * allocation lifetimes initialize explicitly before entering and shut down
 * after their final use. Panic terminates the process; unwinding and longjmp
 * across an entry are outside this contract. */
NERI_RT_API void neri_rt_v1_foreign_enter(
    neri_foreign_entry_v1 *entry,
    const neri_runtime_abi_requirements_v1 *requirements);
NERI_RT_API void neri_rt_v1_foreign_leave(neri_foreign_entry_v1 *entry);

NERI_RT_API neri_ref_v1
neri_rt_v1_gc_alloc(const neri_type_descriptor_v1 *type,
                      uint64_t payload_size, uint64_t payload_alignment);
NERI_RT_API void
neri_rt_v1_gc_root_frame_enter(neri_gc_root_frame_v1 *frame);
NERI_RT_API void
neri_rt_v1_gc_root_frame_leave(neri_gc_root_frame_v1 *frame);
NERI_RT_API void neri_rt_v1_gc_store_ref(neri_ref_v1 owner,
                                             neri_ref_v1 *slot,
                                             neri_ref_v1 value);
NERI_RT_API void neri_rt_v1_gc_keep_alive(neri_ref_v1 object);
NERI_RT_API void neri_rt_v1_gc_collect(void);
NERI_RT_API void neri_rt_v1_gc_get_stats(neri_gc_stats_v1 *stats);

/* Joined generation with a compiler-verified callback. The adapter writes one
 * element in its physical array layout without allocating after obtaining the
 * returned value. Captures must be read-only; output slots are fresh, disjoint
 * and inaccessible through captures. Zero parallelism selects the host limit.
 * The parent adopts managed results before returning; callers root the result
 * across subsequent allocating operations. Raw native adapters are trusted. */
typedef void (*neri_task_generate_fn_v1)(neri_ref_v1 callback, neri_int_v1 index,
                                        void *output);
NERI_RT_API neri_ref_v1 neri_rt_v1_task_generate(neri_int_v1 count,
    neri_int_v1 parallelism, const neri_type_descriptor_v1 *array_type,
    neri_ref_v1 callback, neri_task_generate_fn_v1 adapter);

/* Isolated persistent workers. The native entry is trusted, noncapturing code:
 * config is copied before open returns and is valid until entry returns. Each
 * entry runs in a fresh runtime heap, calls ready after startup, then receives,
 * reads and replies to one job at a time. No managed reference crosses threads.
 * Entries must return normally, unwinding all roots/resources; native exceptions
 * and longjmp across the entry boundary are outside this contract.
 * Bounds: workers 1..64, outstanding 1..65536, input/output <=128 MiB,
 * reserved <=1 GiB, config <=1 MiB. Admission reserves input + maximum output
 * until take, including unread completions. Zero byte limits are supported.
 * Pool handles belong to the creating thread AND active runtime heap.
 * Joined task heaps cannot open or operate pools.
 * Open waits for every ready; startup failure rolls back and joins all workers.
 * Its optional report is zeroed on entry and preserves startup failure details.
 * Stop cancels queued jobs and requests cooperative active cancellation. An
 * entry may unwind and return after cancellation without replying; its active
 * job completes cancelled. An uncancelled active return fails the job. Close
 * stops and joins, discarding unread results; noncooperative entries can delay
 * either open rollback or close indefinitely. Heap shutdown closes owned pools.
 * Failure diagnostics are limited to 4096 bytes per worker, separately bounded.
 * Close reports worker failures, including cleanup failures after a reply.
 * Raw close invalidates the handle; resource wrappers provide idempotence. */
#define NERI_WORKER_OK_V1 0
#define NERI_WORKER_EMPTY_V1 1
#define NERI_WORKER_STOPPED_V1 2
#define NERI_WORKER_FULL_V1 3
#define NERI_WORKER_INVALID_V1 4
#define NERI_WORKER_LIMIT_V1 5
#define NERI_WORKER_STARTUP_FAILED_V1 6
#define NERI_WORKER_NO_MEMORY_V1 7
#define NERI_WORKER_UNAVAILABLE_V1 8
#define NERI_WORKER_SUCCESS_V1 0
#define NERI_WORKER_FAILED_V1 1
#define NERI_WORKER_CANCELLED_V1 2

typedef void (*neri_worker_entry_v1)(const uint8_t *config, uint64_t length);
typedef struct neri_worker_options_v1 {
  uint32_t struct_size;
  uint32_t worker_count;
  uint64_t max_outstanding;
  uint64_t max_input_bytes;
  uint64_t max_output_bytes;
  uint64_t max_reserved_bytes;
} neri_worker_options_v1;
typedef struct neri_worker_completion_v1 {
  uint64_t job_id;
  uint64_t payload_length;
  uint32_t kind;
  uint32_t reserved;
} neri_worker_completion_v1;
typedef struct neri_worker_close_report_v1 {
  uint32_t failures;
  uint32_t error_length;
  uint8_t first_error[4096]; /* counted bytes, not NUL terminated */
} neri_worker_close_report_v1;
NERI_RT_API int32_t neri_rt_v1_worker_pool_open(const neri_worker_options_v1 *,
    neri_worker_entry_v1, const uint8_t *, uint64_t, int64_t *,
    neri_worker_close_report_v1 *);
NERI_RT_API int32_t neri_rt_v1_worker_pool_submit(int64_t, const uint8_t *, uint64_t, uint64_t *);
/* Poll peeks the oldest completion; zero timeout never blocks. Take copies and
 * consumes a completed ticket only if capacity suffices; failed/cancelled jobs
 * have empty payloads. Cancel is cooperative once a job has been received. */
NERI_RT_API int32_t neri_rt_v1_worker_pool_poll(int64_t, uint32_t, neri_worker_completion_v1 *);
NERI_RT_API int32_t neri_rt_v1_worker_pool_take(int64_t, uint64_t, uint8_t *, uint64_t);
/* ABI 1.33: borrowed poll-only descriptor, valid until pool close. Signals
 * completed results or a stopped pool with no outstanding jobs; readiness is
 * advisory and may be stale. Stopping alone does not signal unfinished work.
 * Call worker_pool_poll after a wakeup, allowing EMPTY. Poll and take reconcile
 * an empty running pool without waiting for the notification channel.
 * Never read/write/close the descriptor or alter its flags.
 * The creator thread and heap restrictions apply; errors write -1 when the
 * output pointer is non-null. POSIX uses a pipe, Windows a loopback socket. */
NERI_RT_API int32_t neri_rt_v1_worker_pool_readiness(int64_t, int64_t *);
NERI_RT_API int32_t neri_rt_v1_worker_pool_cancel(int64_t, uint64_t);
NERI_RT_API int32_t neri_rt_v1_worker_pool_stop(int64_t);
NERI_RT_API int32_t neri_rt_v1_worker_pool_close(int64_t, neri_worker_close_report_v1 *);
NERI_RT_API int32_t neri_rt_v1_worker_ready(void);
/* Receive blocks, claims a job and exposes its length. Read copies its bytes
 * without consuming it. Reply copies output and completes the active job;
 * an oversized reply leaves it active. Reply after cancellation is cancelled.
 * Receive before ready or before the previous reply is invalid. */
NERI_RT_API int32_t neri_rt_v1_worker_receive(uint64_t *);
NERI_RT_API int32_t neri_rt_v1_worker_read(uint8_t *, uint64_t);
NERI_RT_API int32_t neri_rt_v1_worker_reply(const uint8_t *, uint64_t);
NERI_RT_API int32_t neri_rt_v1_worker_cancelled(void);
/* Terminal failure, truncating diagnostic to 4096 bytes. Entry must unwind and
 * return; further receive/reply calls are invalid. If no available workers
 * remain, admission stops and queued jobs fail immediately, before cleanup. */
NERI_RT_API int32_t neri_rt_v1_worker_fail(const uint8_t *, uint64_t);

/* Sequential generation invokes an ordinary callback on the caller thread.
 * The callback and partially populated result remain rooted throughout. */
NERI_RT_API neri_ref_v1 neri_rt_v1_array_generate(neri_int_v1 count,
    const neri_type_descriptor_v1 *array_type, neri_ref_v1 callback,
    neri_task_generate_fn_v1 adapter);

/* Immutable UTF-8 strings. Literal objects use the exported immortal type. */
NERI_RT_API extern const neri_type_descriptor_v1
    neri_rt_v1_string_literal_type;
NERI_RT_API neri_ref_v1
neri_rt_v1_string_concat(neri_ref_v1 left, neri_ref_v1 right);
NERI_RT_API neri_bool_v1
neri_rt_v1_string_equal(neri_ref_v1 left, neri_ref_v1 right);
NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_int(neri_int_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_byte(neri_byte_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_float(neri_float_v1 value);

/* Length-delimited UTF-8 console output. Every operation flushes its stream. */
NERI_RT_API void neri_rt_v1_stdout_write(neri_ref_v1 value);
NERI_RT_API void neri_rt_v1_stdout_write_line(neri_ref_v1 value);
NERI_RT_API void neri_rt_v1_stderr_write(neri_ref_v1 value);
/* ABI 1.28: writes exactly length bytes, returns 0 on success, -1 on failure. */
NERI_RT_API neri_int_v1 neri_rt_v1_stderr_write_bytes(const uint8_t *bytes, neri_int_v1 length);
NERI_RT_API neri_ref_v1 neri_rt_v1_stdin_read_line(void);
NERI_RT_API neri_ref_v1 neri_rt_v1_stdin_read_line_optional(void);

/* UTF-8, collections, filesystem, path, environment, and process services. */
NERI_RT_API neri_int_v1
neri_rt_v1_host_string_byte_length(neri_ref_v1 value);
NERI_RT_API neri_byte_v1
neri_rt_v1_host_string_byte_at(neri_ref_v1 value, neri_int_v1 index);
NERI_RT_API neri_ref_v1 neri_rt_v1_host_string_slice(
    neri_ref_v1 value, neri_int_v1 start, neri_int_v1 length);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_string_from_bytes(neri_ref_v1 bytes);
NERI_RT_API void neri_rt_v1_host_parse_int(
    neri_optional_int_v1 *result, neri_ref_v1 value);
NERI_RT_API void neri_rt_v1_host_parse_float(
    neri_optional_float_v1 *result, neri_ref_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_byte(neri_ref_v1 values, neri_byte_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_int(neri_ref_v1 values, neri_int_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_string(neri_ref_v1 values, neri_ref_v1 value);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_read_text(neri_ref_v1 path);
NERI_RT_API neri_bool_v1 neri_rt_v1_host_write_text(
    neri_ref_v1 path, neri_ref_v1 contents);
NERI_RT_API neri_bool_v1 neri_rt_v1_host_write_bytes(
    neri_ref_v1 path, neri_ref_v1 contents);
NERI_RT_API neri_bool_v1
neri_rt_v1_host_remove_file(neri_ref_v1 path);
NERI_RT_API neri_ref_v1 neri_rt_v1_host_path_join(
    neri_ref_v1 left, neri_ref_v1 right);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_path_absolute(neri_ref_v1 path);
NERI_RT_API neri_int_v1 neri_rt_v1_host_canonical_path(
    const neri_byte_v1 *path, neri_byte_v1 *output, neri_int_v1 capacity);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_path_file_name(neri_ref_v1 path);
NERI_RT_API neri_int_v1 neri_rt_v1_host_argument_count(void);
/* ABI 1.26: canonical current executable path in UTF-8. Returns byte length,
   excluding NUL, or -1 on failure. Writes only when capacity exceeds length;
   output may be NULL when capacity is zero. */
NERI_RT_API neri_int_v1 neri_rt_v1_host_executable_path(
    neri_byte_v1 *output, neri_int_v1 capacity);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_argument_at(neri_int_v1 index);
NERI_RT_API neri_ref_v1
neri_rt_v1_host_environment(neri_ref_v1 name);
NERI_RT_API void neri_rt_v1_host_run(neri_optional_int_v1 *result,
                                         neri_ref_v1 executable,
                                         neri_ref_v1 arguments);
NERI_RT_API NERI_RT_NORETURN void
neri_rt_v1_host_exit(neri_int_v1 status);
NERI_RT_API neri_ref_v1 neri_rt_v1_host_error_message(void);

/* Unsafe socket primitives: caller owns descriptors and pointer ranges.
 * I/O returns -2 for retryable interruption/would-block, -1 for failure.
 * Read zero is EOF, poll zero is timeout. Close is called once, never retried.
 * net_error must be called immediately after failure, before another OS call.
 * configure enables nonblocking I/O, close-on-exec and SIGPIPE suppression.
 */
NERI_RT_API neri_int_v1 neri_rt_v1_net_open(void);
/* ABI 1.11: caller-owned regular-file descriptors; size -2 means nonregular,
 * read -2 means interrupted. Other failures return -1. Capture file_error
 * immediately after failure. Paths are UTF-8 bytes without embedded NUL. */
NERI_RT_API neri_int_v1 neri_rt_v1_file_open(const uint8_t *path, neri_int_v1 length);
NERI_RT_API neri_int_v1 neri_rt_v1_file_size(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_file_read(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length);
/* ABI 1.25: wait without consuming input. 1 means readable/EOF, 0 timeout,
 * -2 interrupted, -1 error. The timeout is a nonnegative millisecond count. */
NERI_RT_API neri_int_v1 neri_rt_v1_file_wait_readable(neri_int_v1 fd, neri_int_v1 milliseconds);
NERI_RT_API neri_int_v1 neri_rt_v1_file_close(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_file_error(void);
/* ABI 1.15: descriptor-relative, no-follow rooted opens. Return descriptor or
 * -1 with OS code and category: missing=1, denied=2, symlink=3, type=4,
 * other=5, unavailable=6. kind=1 requires a directory, kind=2 opens a file. */
NERI_RT_API neri_int_v1 neri_rt_v1_file_root_open(const uint8_t *path, neri_int_v1 length, neri_int_v1 *os_code, neri_int_v1 *category);
NERI_RT_API neri_int_v1 neri_rt_v1_file_root_open_at(neri_int_v1 parent, const uint8_t *name, neri_int_v1 length, neri_int_v1 kind, neri_int_v1 *os_code, neri_int_v1 *category);
/* ABI 1.17: descriptor-owned, no-follow directory iteration. */
NERI_RT_API neri_int_v1 neri_rt_v1_file_directory_open(neri_int_v1 parent, neri_int_v1 *token, neri_int_v1 *os_code, neri_int_v1 *close_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_directory_next(neri_int_v1 token, uint8_t *name, neri_int_v1 capacity, neri_int_v1 *length, neri_int_v1 *kind, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_directory_close(neri_int_v1 token, neri_int_v1 *os_code);
/* ABI 1.16: supervised child processes with owned output snapshots. */
NERI_RT_API neri_int_v1 neri_rt_v1_process_spawn(const uint8_t *config, neri_int_v1 length, neri_int_v1 *token, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_process_poll(neri_int_v1 token, neri_int_v1 wait_ms, neri_int_v1 *state, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_process_read(neri_int_v1 token, neri_int_v1 channel, neri_int_v1 offset, uint8_t *output, neri_int_v1 capacity, neri_int_v1 *count);
NERI_RT_API neri_int_v1 neri_rt_v1_process_cancel(neri_int_v1 token, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_process_dispose(neri_int_v1 token, neri_int_v1 *os_code);
/* ABI 1.22: bounded initial stdin and graceful child-domain interruption. */
NERI_RT_API neri_int_v1 neri_rt_v1_process_spawn_input(const uint8_t *config, neri_int_v1 length, neri_int_v1 *token, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_process_interrupt(neri_int_v1 token, neri_int_v1 *os_code);
/* ABI 1.22: owned temporary directories and filesystem operations. */
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_temp_directory(const uint8_t *prefix, neri_int_v1 prefix_length, uint8_t *output, neri_int_v1 capacity, neri_int_v1 *output_length, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_mkdir(const uint8_t *path, neri_int_v1 length, neri_int_v1 parents, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_rename(const uint8_t *source, neri_int_v1 source_length, const uint8_t *destination, neri_int_v1 destination_length, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_remove(const uint8_t *path, neri_int_v1 length, neri_int_v1 kind, neri_int_v1 *os_code);
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_status(const uint8_t *path, neri_int_v1 length, neri_int_v1 *exists, neri_int_v1 *kind, neri_int_v1 *executable, neri_int_v1 *symlink, neri_int_v1 *os_code);
/* ABI 1.28: compiler cache metadata on macOS arm64 and Linux.
 * supported returns 1 when the executable cache policy is implemented
 * (macOS arm64 only); metadata availability is independent of that policy.
 * metadata returns 0 on success, -1 on invalid input, failure or unsupported host.
 * Paths are nonempty byte sequences without NUL, at most 1 MiB; follow is 0/1.
 * Outputs are kind (regular=1, directory=2, symlink=3, other=4), permission bits
 * (07777), ownership by the real user (0/1), and a 32-byte SHA-256 fingerprint.
 * The fingerprint hashes ten little-endian uint64 values: device, inode, mode,
 * uid, gid, mtime seconds/nanoseconds, ctime seconds/nanoseconds, and byte size.
 * atime and platform padding are excluded. All output pointers are required.
 * This is a metadata snapshot; it does not acquire or retain a file handle. */
NERI_RT_API neri_int_v1 neri_rt_v1_cache_supported(void);
NERI_RT_API neri_int_v1 neri_rt_v1_cache_metadata(const uint8_t *path, neri_int_v1 length, neri_int_v1 follow, neri_int_v1 *kind, neri_int_v1 *permissions, neri_int_v1 *owned, uint8_t *fingerprint32);
NERI_RT_API neri_int_v1 neri_rt_v1_file_mutation_copy(const uint8_t *source, neri_int_v1 source_length, const uint8_t *destination, neri_int_v1 destination_length, neri_int_v1 *os_code);
/* ABI 1.12: one serving-thread-owned interrupt lease; 0 means unavailable.
 * Positive generation tokens prevent stale closes affecting a later lease. */
NERI_RT_API neri_int_v1 neri_rt_v1_interrupt_open(void);
NERI_RT_API neri_int_v1 neri_rt_v1_interrupt_pending(neri_int_v1 token);
NERI_RT_API void neri_rt_v1_interrupt_close(neri_int_v1 token);
/* ABI 1.20: native fatal drain deadline; token ownership remains native. */
NERI_RT_API neri_int_v1 neri_rt_v1_drain_open(neri_int_v1 timeout_milliseconds, neri_int_v1 watch_interrupt);
NERI_RT_API neri_int_v1 neri_rt_v1_drain_request(neri_int_v1 token);
NERI_RT_API neri_int_v1 neri_rt_v1_drain_close(neri_int_v1 token);
/* ABI 1.8: one foreground terminal lease, positive generation token.
 * Read: byte 0..255, -1 timeout, -2 closed/interrupted/error. Timeout 0..60000ms.
 * Close is idempotent; stale tokens cannot affect a subsequent lease.
 * Size: columns (rows=0), rows (otherwise), 0 unavailable.
 * Clock: monotonic milliseconds, -1 unavailable. */
NERI_RT_API neri_int_v1 neri_rt_v1_terminal_open(void);
NERI_RT_API void neri_rt_v1_terminal_close(neri_int_v1 token);
NERI_RT_API neri_int_v1 neri_rt_v1_terminal_read(neri_int_v1 token, neri_int_v1 timeout);
NERI_RT_API neri_int_v1 neri_rt_v1_terminal_size(neri_int_v1 token, neri_int_v1 rows);
NERI_RT_API neri_int_v1 neri_rt_v1_clock_milliseconds(void);
/* Returns zero and writes signed Unix milliseconds, or -1 on failure. */
NERI_RT_API neri_int_v1 neri_rt_v1_clock_wall_milliseconds(neri_int_v1 *value);
/* ABI 1.14: exact-byte SHA-256 and bounded OS entropy; zero succeeds. */
NERI_RT_API neri_int_v1 neri_rt_v1_crypto_sha256(const uint8_t *input, neri_int_v1 length, uint8_t *digest32);
NERI_RT_API neri_int_v1 neri_rt_v1_crypto_random(uint8_t *output, neri_int_v1 length);
NERI_RT_API neri_int_v1 neri_rt_v1_net_configure(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_bind(neri_int_v1 fd, neri_int_v1 port);
NERI_RT_API neri_int_v1 neri_rt_v1_net_listen(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_connect(neri_int_v1 fd, neri_int_v1 port);
NERI_RT_API neri_int_v1 neri_rt_v1_net_connect_timeout(neri_int_v1 fd, neri_int_v1 port, neri_int_v1 timeout_ms);
NERI_RT_API neri_int_v1 neri_rt_v1_net_local_port(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_accept(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_poll(neri_int_v1 fd, neri_int_v1 writing, neri_int_v1 milliseconds);
/* Poll one descriptor set in a single OS wait. Arrays have count elements and
 * events must not overlap either input array. Count is 0..4096; timeout is
 * -1 (infinite) or 0..60000 ms. A zero count accepts NULL arrays and waits only
 * for the timeout or interruption. Interests: 1 read, 2 write (0 observes only
 * exceptional conditions). Events: 1 read, 2 write, 4 error, 8 hangup, 16 invalid.
 * Returns the number of nonzero event entries, 0 on timeout/interruption, or -1
 * on validation/OS/allocation failure. For a valid count and events pointer,
 * outputs are cleared even on failure. POSIX accepts pipes as well as sockets;
 * Windows accepts sockets and may fail if every socket is invalid. Events do
 * not transfer descriptor ownership. Failure preserves the OS error through
 * cleanup; success/interruption preserves the caller's previous error value. */
NERI_RT_API neri_int_v1 neri_rt_v1_net_poll_many(const neri_int_v1 *descriptors,
    const neri_int_v1 *interests, neri_int_v1 *events, neri_int_v1 count,
    neri_int_v1 timeout_ms);
NERI_RT_API neri_int_v1 neri_rt_v1_net_read(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length);
NERI_RT_API neri_int_v1 neri_rt_v1_net_write(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length);
NERI_RT_API void neri_rt_v1_net_close(neri_int_v1 fd);
/* ABI 1.18: closes once and reports the platform close result. */
NERI_RT_API neri_int_v1 neri_rt_v1_net_close_result(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_milliseconds(void);
NERI_RT_API neri_int_v1 neri_rt_v1_net_error(uint8_t *bytes, neri_int_v1 capacity);

NERI_RT_API neri_int_v1 neri_rt_v1_math_abs(neri_int_v1 value);
NERI_RT_API neri_int_v1 neri_rt_v1_math_max(neri_int_v1 left,
                                                  neri_int_v1 right);
NERI_RT_API neri_int_v1 neri_rt_v1_math_min(neri_int_v1 left,
                                                  neri_int_v1 right);
NERI_RT_API neri_int_v1 neri_rt_v1_math_pow(neri_int_v1 base,
                                                  neri_int_v1 exponent);

NERI_RT_API void neri_rt_v1_test_assert(neri_bool_v1 condition);
NERI_RT_API void neri_rt_v1_test_assert_true(neri_bool_v1 value);
NERI_RT_API void neri_rt_v1_test_assert_false(neri_bool_v1 value);

NERI_RT_API void *neri_rt_v1_gc_borrow_begin(neri_ref_v1 owner,
                                                 uint64_t payload_offset,
                                                 uint64_t byte_length,
                                                 neri_gc_borrow_v1 *borrow);
NERI_RT_API void neri_rt_v1_gc_borrow_end(neri_gc_borrow_v1 *borrow);

NERI_RT_API void *neri_rt_v1_native_alloc(uint64_t byte_count,
                                              uint64_t alignment);
NERI_RT_API void *neri_rt_v1_native_alloc_zeroed(uint64_t byte_count,
                                                     uint64_t alignment);
NERI_RT_API void *neri_rt_v1_native_realloc(void *pointer,
                                                uint64_t byte_count,
                                                uint64_t alignment);
NERI_RT_API void neri_rt_v1_native_free(void *pointer);

NERI_RT_API NERI_RT_NORETURN void
neri_rt_v1_panic(const neri_panic_v1 *panic);
NERI_RT_API NERI_RT_NORETURN void
neri_rt_v1_panic_at(const neri_panic_v1 *panic,
                      const neri_source_location_v1 *location);

#if defined(__cplusplus)
}
#endif

#endif
