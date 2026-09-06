#include "neri/runtime_abi.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NERI_ABI_STATIC_ASSERT(condition)                                    \
  _Static_assert((condition), #condition)
#define NERI_ABI_CHECK(condition)                                            \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "ABI check failed: %s (%s:%d)\n", #condition, __FILE__,  \
              __LINE__);                                                       \
      abort();                                                                 \
    }                                                                          \
  } while (0)

NERI_ABI_STATIC_ASSERT(sizeof(neri_bool_v1) == 1);
NERI_ABI_STATIC_ASSERT(alignof(neri_bool_v1) == 1);
NERI_ABI_STATIC_ASSERT(sizeof(neri_byte_v1) == 1);
NERI_ABI_STATIC_ASSERT(alignof(neri_byte_v1) == 1);
NERI_ABI_STATIC_ASSERT(sizeof(neri_int_v1) == 8);
NERI_ABI_STATIC_ASSERT(alignof(neri_int_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_float_v1) == 8);
NERI_ABI_STATIC_ASSERT(alignof(neri_float_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_ref_v1) == 8);
NERI_ABI_STATIC_ASSERT(alignof(neri_ref_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_object_header_v1) == 16);
NERI_ABI_STATIC_ASSERT(alignof(neri_object_header_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_optional_bool_v1) == 2);
NERI_ABI_STATIC_ASSERT(alignof(neri_optional_bool_v1) == 1);
NERI_ABI_STATIC_ASSERT(sizeof(neri_optional_byte_v1) == 2);
NERI_ABI_STATIC_ASSERT(alignof(neri_optional_byte_v1) == 1);
NERI_ABI_STATIC_ASSERT(sizeof(neri_optional_int_v1) == 16);
NERI_ABI_STATIC_ASSERT(alignof(neri_optional_int_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_optional_float_v1) == 16);
NERI_ABI_STATIC_ASSERT(alignof(neri_optional_float_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_string_prefix_v1) == 24);
NERI_ABI_STATIC_ASSERT(alignof(neri_string_prefix_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_array_prefix_v1) == 24);
NERI_ABI_STATIC_ASSERT(alignof(neri_array_prefix_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_type_descriptor_v1) == 104);
NERI_ABI_STATIC_ASSERT(alignof(neri_type_descriptor_v1) == 8);
NERI_ABI_STATIC_ASSERT(NERI_TYPE_DESCRIPTOR_V1_BASE_SIZE == 80);
NERI_ABI_STATIC_ASSERT(NERI_TYPE_DESCRIPTOR_V1_4_SIZE == 104);
NERI_ABI_STATIC_ASSERT(NERI_TYPE_KIND_INLINE_AGGREGATE_V1 == 4);
NERI_ABI_STATIC_ASSERT(NERI_TYPE_KIND_SCALAR_V1 == 5);
NERI_ABI_STATIC_ASSERT(NERI_SCALAR_KIND_NONE_V1 == 0);
NERI_ABI_STATIC_ASSERT(NERI_SCALAR_KIND_BOOL_V1 == 1);
NERI_ABI_STATIC_ASSERT(NERI_SCALAR_KIND_BYTE_V1 == 2);
NERI_ABI_STATIC_ASSERT(NERI_SCALAR_KIND_INT_V1 == 3);
NERI_ABI_STATIC_ASSERT(NERI_SCALAR_KIND_FLOAT_V1 == 4);
NERI_ABI_STATIC_ASSERT(NERI_RT_FEATURE_INLINE_AGGREGATES ==
                         (UINT64_C(1) << 8));
NERI_ABI_STATIC_ASSERT(NERI_RT_FEATURE_EXTENDED_SCALARS ==
                         (UINT64_C(1) << 9));
NERI_ABI_STATIC_ASSERT(NERI_RT_FEATURE_MULTIPLE_MUTATORS ==
                         (UINT64_C(1) << 10));
NERI_ABI_STATIC_ASSERT(offsetof(neri_type_descriptor_v1, reserved) == 72);
NERI_ABI_STATIC_ASSERT(offsetof(neri_type_descriptor_v1, element_type) ==
                         80);
NERI_ABI_STATIC_ASSERT(offsetof(neri_type_descriptor_v1, trace_inline) ==
                         88);
NERI_ABI_STATIC_ASSERT(offsetof(neri_type_descriptor_v1, scalar_kind) ==
                         96);
NERI_ABI_STATIC_ASSERT(offsetof(neri_type_descriptor_v1, reserved_v1_4) ==
                         100);
NERI_ABI_STATIC_ASSERT(sizeof(neri_runtime_abi_requirements_v1) == 16);
NERI_ABI_STATIC_ASSERT(sizeof(neri_runtime_abi_info_v1) == 16);
NERI_ABI_STATIC_ASSERT(sizeof(neri_gc_root_frame_v1) == 32);
NERI_ABI_STATIC_ASSERT(alignof(neri_gc_root_frame_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_gc_borrow_v1) == 32);
NERI_ABI_STATIC_ASSERT(alignof(neri_gc_borrow_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_gc_stats_v1) == 40);
NERI_ABI_STATIC_ASSERT(sizeof(neri_source_location_v1) == 24);
NERI_ABI_STATIC_ASSERT(alignof(neri_source_location_v1) == 8);
NERI_ABI_STATIC_ASSERT(sizeof(neri_panic_v1) == 24);
NERI_ABI_STATIC_ASSERT(alignof(neri_panic_v1) == 8);

typedef struct probe_node_payload {
  neri_ref_v1 left;
  neri_ref_v1 right;
} probe_node_payload;

static probe_node_payload *payload_of(neri_ref_v1 object) {
  return (probe_node_payload *)((uint8_t *)object +
                                sizeof(neri_object_header_v1));
}

static void trace_node(neri_ref_v1 object, neri_gc_visit_slot_fn_v1 visit,
                       void *context) {
  probe_node_payload *payload = payload_of(object);
  visit(&payload->left, context);
  visit(&payload->right, context);
}

static const neri_type_descriptor_v1 node_type = {
    sizeof(neri_type_descriptor_v1),
    NERI_RUNTIME_ABI_MAJOR,
    NERI_RUNTIME_ABI_MINOR,
    NERI_TYPE_KIND_CLASS_V1,
    NERI_TYPE_FLAG_CONTAINS_REFS_V1,
    sizeof(probe_node_payload),
    alignof(probe_node_payload),
    0,
    0,
    trace_node,
    NULL,
    "hk1_t_q1_n9_50726f62654e6f6465",
    0,
    NULL,
    NULL,
    NERI_SCALAR_KIND_NONE_V1,
    0,
};

static neri_gc_stats_v1 read_stats(void) {
  neri_gc_stats_v1 stats = {
      .struct_size = sizeof(neri_gc_stats_v1),
  };
  neri_rt_v1_gc_get_stats(&stats);
  return stats;
}

static neri_ref_v1 allocate_node(void) {
  return neri_rt_v1_gc_alloc(&node_type, sizeof(probe_node_payload),
                               alignof(probe_node_payload));
}

static void probe_legacy_descriptor_prefix(void) {
  neri_type_descriptor_v1 legacy_type = node_type;
  legacy_type.struct_size = NERI_TYPE_DESCRIPTOR_V1_BASE_SIZE;
  legacy_type.abi_minor = 3;

  neri_ref_v1 roots[1] = {NULL};
  neri_gc_root_frame_v1 frame = {
      .previous = NULL,
      .slots = roots,
      .slot_count = 1,
      .runtime_cookie = 0,
  };
  neri_rt_v1_gc_root_frame_enter(&frame);
  roots[0] = neri_rt_v1_gc_alloc(
      &legacy_type, sizeof(probe_node_payload), alignof(probe_node_payload));
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 1);
  roots[0] = NULL;
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 0);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void probe_abi_negotiation(void) {
  const neri_runtime_abi_info_v1 *info = neri_rt_v1_get_abi();
  NERI_ABI_CHECK(info != NULL);
  NERI_ABI_CHECK(info->struct_size >= sizeof(*info));
  NERI_ABI_CHECK(info->major == NERI_RUNTIME_ABI_MAJOR);
  NERI_ABI_CHECK(info->minor >= NERI_RUNTIME_ABI_MINOR);
  NERI_ABI_CHECK(
      (info->features & (NERI_RT_FEATURE_INLINE_AGGREGATES |
                         NERI_RT_FEATURE_MULTIPLE_MUTATORS)) == 0);
  NERI_ABI_CHECK((info->features & NERI_RT_FEATURE_EXTENDED_SCALARS) != 0);

  const uint64_t required_features =
      NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_NONMOVING_GC |
      NERI_RT_FEATURE_SCOPED_BORROWS | NERI_RT_FEATURE_NATIVE_MEMORY |
      NERI_RT_FEATURE_ROOT_FRAMES | NERI_RT_FEATURE_SOURCE_LOCATIONS |
      NERI_RT_FEATURE_NATIVE_STRINGS | NERI_RT_FEATURE_CONSOLE_IO |
      NERI_RT_FEATURE_BOOTSTRAP_HOST;
  const neri_runtime_abi_requirements_v1 requirements = {
      sizeof(neri_runtime_abi_requirements_v1),
      NERI_RUNTIME_ABI_MAJOR,
      NERI_RUNTIME_ABI_MINOR,
      required_features,
  };
  NERI_ABI_CHECK(neri_rt_v1_initialize(&requirements) ==
                   NERI_ABI_STATUS_OK_V1);

  neri_runtime_abi_requirements_v1 legacy = requirements;
  legacy.minimum_minor = 0;
  legacy.required_features &= ~NERI_RT_FEATURE_SOURCE_LOCATIONS;
  NERI_ABI_CHECK(neri_rt_v1_initialize(&legacy) ==
                   NERI_ABI_STATUS_OK_V1);

  neri_runtime_abi_requirements_v1 incompatible = requirements;
  incompatible.major += 1;
  NERI_ABI_CHECK(neri_rt_v1_initialize(&incompatible) ==
                   NERI_ABI_STATUS_INCOMPATIBLE_MAJOR_V1);
  incompatible = requirements;
  incompatible.minimum_minor += 1;
  NERI_ABI_CHECK(neri_rt_v1_initialize(&incompatible) ==
                   NERI_ABI_STATUS_RUNTIME_TOO_OLD_V1);
  incompatible = requirements;
  incompatible.required_features |= UINT64_C(1) << 63;
  NERI_ABI_CHECK(neri_rt_v1_initialize(&incompatible) ==
                   NERI_ABI_STATUS_MISSING_FEATURE_V1);

  NERI_ABI_CHECK(neri_rt_v1_abi_anchor == 0);
}

static void probe_reference_transfer_and_cycles(void) {
  neri_ref_v1 roots[2] = {NULL, NULL};
  neri_gc_root_frame_v1 frame = {
      .previous = NULL,
      .slots = roots,
      .slot_count = 2,
      .runtime_cookie = 0,
  };
  neri_rt_v1_gc_root_frame_enter(&frame);

  roots[0] = allocate_node();
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 1);

  roots[1] = allocate_node();
  roots[0] = roots[1];
  roots[1] = NULL;
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 1);

  neri_ref_v1 first = allocate_node();
  neri_ref_v1 second = allocate_node();
  roots[0] = first;
  roots[1] = second;
  neri_rt_v1_gc_store_ref(first, &payload_of(first)->right, second);
  neri_rt_v1_gc_store_ref(second, &payload_of(second)->left, first);
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 2);

  roots[0] = NULL;
  roots[1] = NULL;
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 0);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void probe_managed_borrow(void) {
  neri_ref_v1 roots[1] = {allocate_node()};
  neri_gc_root_frame_v1 frame = {
      .previous = NULL,
      .slots = roots,
      .slot_count = 1,
      .runtime_cookie = 0,
  };
  neri_rt_v1_gc_root_frame_enter(&frame);

  neri_gc_borrow_v1 borrow = {{0, 0, 0, 0}};
  void *borrowed =
      neri_rt_v1_gc_borrow_begin(roots[0], offsetof(probe_node_payload, left),
                                   sizeof(neri_ref_v1), &borrow);
  NERI_ABI_CHECK(borrowed == &payload_of(roots[0])->left);

  roots[0] = NULL;
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 1);

  neri_rt_v1_gc_borrow_end(&borrow);
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 0);
  neri_rt_v1_gc_root_frame_leave(&frame);
}

static void probe_raw_pointer_does_not_root(void) {
  neri_ref_v1 roots[1] = {allocate_node()};
  neri_gc_root_frame_v1 frame = {
      .previous = NULL,
      .slots = roots,
      .slot_count = 1,
      .runtime_cookie = 0,
  };
  neri_rt_v1_gc_root_frame_enter(&frame);

  void *volatile raw_pointer = payload_of(roots[0]);
  NERI_ABI_CHECK(raw_pointer != NULL);
  roots[0] = NULL;
  neri_rt_v1_gc_collect();
  NERI_ABI_CHECK(read_stats().managed_object_count == 0);
  neri_rt_v1_gc_root_frame_leave(&frame);

}

static void probe_native_memory(void) {
  uint8_t *memory = neri_rt_v1_native_alloc_zeroed(64, 16);
  NERI_ABI_CHECK(memory != NULL);
  NERI_ABI_CHECK((uintptr_t)memory % 16 == 0);
  for (size_t index = 0; index < 64; ++index) {
    NERI_ABI_CHECK(memory[index] == 0);
    memory[index] = (uint8_t)index;
  }
  NERI_ABI_CHECK(read_stats().native_byte_count == 64);

  memory = neri_rt_v1_native_realloc(memory, 128, 32);
  NERI_ABI_CHECK(memory != NULL);
  NERI_ABI_CHECK((uintptr_t)memory % 32 == 0);
  for (size_t index = 0; index < 64; ++index) {
    NERI_ABI_CHECK(memory[index] == (uint8_t)index);
  }
  NERI_ABI_CHECK(read_stats().native_byte_count == 128);

  neri_rt_v1_native_free(memory);
  neri_rt_v1_native_free(NULL);
  NERI_ABI_CHECK(read_stats().native_byte_count == 0);
}

int main(void) {
  probe_abi_negotiation();
  probe_legacy_descriptor_prefix();
  probe_reference_transfer_and_cycles();
  probe_managed_borrow();
  probe_raw_pointer_does_not_root();
  probe_native_memory();
  neri_rt_v1_shutdown();
  return 0;
}
