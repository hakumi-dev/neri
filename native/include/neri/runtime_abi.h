#ifndef NERI_RUNTIME_ABI_H
#define NERI_RUNTIME_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32)
#if defined(NERI_RUNTIME_BUILD)
#define NERI_RT_API __declspec(dllexport)
#else
#define NERI_RT_API __declspec(dllimport)
#endif
#define NERI_RT_NORETURN __declspec(noreturn)
#else
#define NERI_RT_API __attribute__((visibility("default")))
#define NERI_RT_NORETURN __attribute__((noreturn))
#endif

#define NERI_RUNTIME_ABI_MAJOR UINT16_C(1)
#define NERI_RUNTIME_ABI_MINOR UINT16_C(9)

#define NERI_RT_FEATURE_PRECISE_GC UINT64_C(1)
#define NERI_RT_FEATURE_NONMOVING_GC (UINT64_C(1) << 1)
#define NERI_RT_FEATURE_SCOPED_BORROWS (UINT64_C(1) << 2)
#define NERI_RT_FEATURE_NATIVE_MEMORY (UINT64_C(1) << 3)
#define NERI_RT_FEATURE_ROOT_FRAMES (UINT64_C(1) << 4)
#define NERI_RT_FEATURE_SOURCE_LOCATIONS (UINT64_C(1) << 5)
#define NERI_RT_FEATURE_NATIVE_STRINGS (UINT64_C(1) << 6)
#define NERI_RT_FEATURE_CONSOLE_IO (UINT64_C(1) << 7)
/* ABI v1.4 capability bits. Extended scalars are advertised since ABI v1.9;
 * inline aggregates and multiple mutators remain reserved. */
#define NERI_RT_FEATURE_INLINE_AGGREGATES (UINT64_C(1) << 8)
#define NERI_RT_FEATURE_EXTENDED_SCALARS (UINT64_C(1) << 9)
#define NERI_RT_FEATURE_MULTIPLE_MUTATORS (UINT64_C(1) << 10)
#define NERI_RT_FEATURE_BOOTSTRAP_HOST (UINT64_C(1) << 11)
#define NERI_RT_FEATURE_SOCKETS (UINT64_C(1) << 12)
#define NERI_RT_FEATURE_INTERACTIVE_IO (UINT64_C(1) << 13)

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
NERI_RT_API neri_ref_v1 neri_rt_v1_stdin_read_line(void);

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
NERI_RT_API neri_ref_v1
neri_rt_v1_host_path_file_name(neri_ref_v1 path);
NERI_RT_API neri_int_v1 neri_rt_v1_host_argument_count(void);
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
NERI_RT_API neri_int_v1 neri_rt_v1_net_configure(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_bind(neri_int_v1 fd, neri_int_v1 port);
NERI_RT_API neri_int_v1 neri_rt_v1_net_listen(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_connect(neri_int_v1 fd, neri_int_v1 port);
NERI_RT_API neri_int_v1 neri_rt_v1_net_accept(neri_int_v1 fd);
NERI_RT_API neri_int_v1 neri_rt_v1_net_poll(neri_int_v1 fd, neri_int_v1 writing, neri_int_v1 milliseconds);
NERI_RT_API neri_int_v1 neri_rt_v1_net_read(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length);
NERI_RT_API neri_int_v1 neri_rt_v1_net_write(neri_int_v1 fd, uint8_t *bytes, neri_int_v1 length);
NERI_RT_API void neri_rt_v1_net_close(neri_int_v1 fd);
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
