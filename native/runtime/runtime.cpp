#include "neri/runtime_abi.h"
#include "neri/session_object_linker.h"
#include "neri/host_path.h"
#include "../platform/host.h"
#include "../platform/session_loader.h"
#include "terminal.h"
#include "task_heap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>


namespace {
constexpr uintptr_t root_frame_cookie = UINT64_C(0x484b524f4f545631);
constexpr uintptr_t foreign_entry_cookie = UINT64_C(0x4e52464f52454947);
constexpr uintptr_t borrow_cookie = UINT64_C(0x484b424f52525631);
constexpr uint64_t collection_floor_bytes = 4 * 1024 * 1024;
constexpr uint64_t runtime_features = NERI_RT_ADVERTISED_FEATURES;
constexpr uint32_t known_type_flags = NERI_TYPE_FLAG_CONTAINS_REFS_V1 |
                                      NERI_TYPE_FLAG_IMMUTABLE_V1;

struct runtime_state;

struct managed_allocation final {
  managed_allocation *previous;
  managed_allocation *next;
  neri_ref_v1 object;
  uint64_t payload_size;
  const runtime_state *owner;
  bool marked;
};

// One reservation per managed object. The private prefix preserves the public
// object/payload alignment and is the address passed back to the allocator.
constexpr size_t managed_prefix_size =
    (sizeof(managed_allocation) + alignof(std::max_align_t) - 1) &
    ~(alignof(std::max_align_t) - 1);
static_assert(managed_prefix_size % alignof(neri_object_header_v1) == 0);
static_assert(sizeof(neri_object_header_v1) % alignof(std::max_align_t) == 0);

struct native_allocation final {
  native_allocation *previous;
  native_allocation *next;
  void *pointer;
  uint64_t byte_count;
  uint64_t alignment;
};

struct runtime_state final {
  managed_allocation *managed_head;
  native_allocation *native_head;
  neri_gc_root_frame_v1 *root_frame;
  neri_gc_borrow_v1 *borrow;
  neri_foreign_entry_v1 *foreign_entry;
  uint64_t managed_object_count;
  uint64_t managed_byte_count;
  uint64_t native_byte_count;
  uint64_t collection_count;
  uint64_t next_collection_bytes = collection_floor_bytes;
  int process_argument_count;
  const char *const *process_arguments;
  bool initialized;
  bool collecting;
  bool suspended;
  const runtime_state *read_parent;
  std::string host_error;
  std::vector<neri_ref_v1 *> persistent_roots;
};

struct task_result_heap final {
  runtime_state heap{};
  task_result_heap *next = nullptr;
};
} // namespace

struct neri_task_scope final {
  explicit neri_task_scope(runtime_state *owner) : parent(owner) {}
  runtime_state *parent;
  std::mutex mutex;
  std::condition_variable completed;
  uint64_t outstanding = 0;
  bool accepting = true;
  task_result_heap *results = nullptr;
};

struct neri_task_ticket final {
  neri_task_scope *scope;
  neri_ref_v1 *results;
  uint64_t result_count;
};

namespace {
struct mark_stack final {
  managed_allocation **items;
  size_t count;
  size_t capacity;
};

// The active context may be a scoped task on a worker or on its waiting parent.
// Heap identities remain stable while contexts are suspended or helped.
constinit thread_local runtime_state *active_state = nullptr;
runtime_state &current_state() {
  if (active_state == nullptr) [[unlikely]] {
    // Construct the base context once, outside the hot TLS access path. Its
    // string destructor must not add a dynamic TLS guard to every heap lookup.
    thread_local runtime_state base_state{};
    active_state = &base_state;
  }
  return *active_state;
}
thread_local mark_stack *active_mark_stack = nullptr;

const neri_type_descriptor_v1 dynamic_string_type = {
    sizeof(neri_type_descriptor_v1),
    NERI_RUNTIME_ABI_MAJOR,
    NERI_RUNTIME_ABI_MINOR,
    NERI_TYPE_KIND_STRING_V1,
    NERI_TYPE_FLAG_IMMUTABLE_V1,
    sizeof(uint64_t),
    alignof(uint64_t),
    0,
    0,
    nullptr,
    nullptr,
    "hk1_t_q1_n6_537472696e67",
    0,
    nullptr,
    nullptr,
    NERI_SCALAR_KIND_NONE_V1,
    0,
};

const neri_runtime_abi_info_v1 runtime_abi = {
    sizeof(neri_runtime_abi_info_v1),
    NERI_RUNTIME_ABI_MAJOR,
    NERI_RUNTIME_ABI_MINOR,
    runtime_features,
};

[[noreturn]] void report_panic(const neri_panic_v1 *panic,
                               const neri_source_location_v1 *location) {
  if (panic == nullptr || panic->reserved != 0 || panic->message == nullptr ||
      panic->message_length == 0 || panic->message_length > SIZE_MAX) {
    static const uint8_t fallback[] = "invalid panic descriptor";
    const neri_panic_v1 valid = {
        NERI_PANIC_RUNTIME_CONTRACT_V1,
        0,
        fallback,
        sizeof(fallback) - 1,
    };
    report_panic(&valid, nullptr);
  }

  static_cast<void>(std::fflush(stdout));
  neri_terminal_restore();
  neri_interrupt_restore();
  std::fprintf(stderr, "Neri panic NRP%03u", panic->code);
  if (location != nullptr && location->source_name != nullptr &&
      location->source_name_length != 0 &&
      location->source_name_length <= SIZE_MAX) {
    std::fputs(" at ", stderr);
    std::fwrite(location->source_name, 1,
                static_cast<size_t>(location->source_name_length), stderr);
    std::fprintf(stderr, ":%u+%u", location->utf8_start,
                 location->utf8_length);
  }
  std::fputs(": ", stderr);
  std::fwrite(panic->message, 1, static_cast<size_t>(panic->message_length),
              stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::_Exit(NERI_RUNTIME_PANIC_EXIT_CODE_V1);
}

[[noreturn]] void panic_raw(uint32_t code, const char *message,
                            const neri_source_location_v1 *location) {
  const neri_panic_v1 panic = {
      code,
      0,
      reinterpret_cast<const uint8_t *>(message),
      std::strlen(message),
  };
  report_panic(&panic, location);
}

[[noreturn]] void contract_panic(const char *message) {
  panic_raw(NERI_PANIC_RUNTIME_CONTRACT_V1, message, nullptr);
}

[[noreturn]] void out_of_memory(const char *message) {
  panic_raw(NERI_PANIC_OUT_OF_MEMORY_V1, message, nullptr);
}

void require_initialized() {
  if (!current_state().initialized) {
    contract_panic("runtime operation requires successful initialization");
  }
  if (current_state().suspended) {
    contract_panic("managed operations require an active heap, not a suspended task parent");
  }
}

[[nodiscard]] neri_abi_status_v1 validate_requirements(
    const neri_runtime_abi_requirements_v1 *requirements) {
  if (requirements == nullptr ||
      requirements->struct_size < sizeof(neri_runtime_abi_requirements_v1)) {
    return NERI_ABI_STATUS_INVALID_ARGUMENT_V1;
  }
  if (requirements->major != runtime_abi.major) {
    return NERI_ABI_STATUS_INCOMPATIBLE_MAJOR_V1;
  }
  if (requirements->minimum_minor > runtime_abi.minor) {
    return NERI_ABI_STATUS_RUNTIME_TOO_OLD_V1;
  }
  if ((requirements->required_features & runtime_abi.features) !=
      requirements->required_features) {
    return NERI_ABI_STATUS_MISSING_FEATURE_V1;
  }
  return NERI_ABI_STATUS_OK_V1;
}

[[nodiscard]] bool is_power_of_two(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] managed_allocation *find_managed(neri_ref_v1 object) {
  if (object == nullptr) {
    return nullptr;
  }
  auto *allocation = reinterpret_cast<managed_allocation *>(object->runtime_word);
  return allocation != nullptr && allocation->owner == &current_state() &&
                 allocation->object == object
             ? allocation : nullptr;
}

[[nodiscard]] bool is_string_literal(neri_ref_v1 object) {
  return object != nullptr &&
         object->type == &neri_rt_v1_string_literal_type;
}

[[nodiscard]] const managed_allocation *find_readable(neri_ref_v1 object) {
  if (object == nullptr) {
    return nullptr;
  }
  const auto *allocation =
      reinterpret_cast<const managed_allocation *>(object->runtime_word);
  if (allocation == nullptr || allocation->object != object) {
    return nullptr;
  }
  if (allocation->owner == &current_state()) {
    return allocation;
  }
  for (auto *parent = current_state().read_parent; parent != nullptr;
       parent = parent->read_parent) {
    if (allocation->owner == parent && parent->suspended) {
      return allocation;
    }
  }
  return nullptr;
}

[[nodiscard]] native_allocation *find_native(void *pointer) {
  for (auto *allocation = current_state().native_head; allocation != nullptr;
       allocation = allocation->next) {
    if (allocation->pointer == pointer) {
      return allocation;
    }
  }
  return nullptr;
}

void assert_consistent() {
#ifndef NDEBUG
  uint64_t managed_count = 0;
  uint64_t managed_bytes = 0;
  managed_allocation *managed_previous = nullptr;
  for (auto *allocation = current_state().managed_head; allocation != nullptr;
       allocation = allocation->next) {
    assert(allocation->previous == managed_previous);
    assert(allocation->object != nullptr);
    assert(allocation->owner == &current_state());
    assert(current_state().collecting || !allocation->marked);
    managed_previous = allocation;
    ++managed_count;
    managed_bytes += sizeof(neri_object_header_v1) + allocation->payload_size;
  }
  assert(managed_count == current_state().managed_object_count);
  assert(managed_bytes == current_state().managed_byte_count);

  uint64_t native_bytes = 0;
  native_allocation *native_previous = nullptr;
  for (auto *allocation = current_state().native_head; allocation != nullptr;
       allocation = allocation->next) {
    assert(allocation->previous == native_previous);
    assert(allocation->pointer != nullptr);
    native_previous = allocation;
    native_bytes += allocation->byte_count;
  }
  assert(native_bytes == current_state().native_byte_count);
#endif
}

void push_mark(mark_stack &stack, managed_allocation *allocation) {
  if (stack.count == stack.capacity) {
    const size_t next_capacity = stack.capacity == 0 ? 64 : stack.capacity * 2;
    if (next_capacity < stack.capacity ||
        next_capacity > SIZE_MAX / sizeof(managed_allocation *)) {
      out_of_memory("GC mark worklist is too large");
    }
    void *replacement = std::realloc(
        stack.items, next_capacity * sizeof(managed_allocation *));
    if (replacement == nullptr) {
      out_of_memory("GC mark worklist allocation failed");
    }
    stack.items = static_cast<managed_allocation **>(replacement);
    stack.capacity = next_capacity;
  }
  stack.items[stack.count++] = allocation;
}

void mark_object(neri_ref_v1 object) {
  if (object == nullptr) {
    return;
  }
  if (is_string_literal(object)) {
    return;
  }
  auto *allocation = find_managed(object);
  if (allocation == nullptr) {
    // Ancestor heaps remain intact. Their mark bits and tracing belong to
    // their suspended owners; child collections never touch them.
    if (find_readable(object) != nullptr) {
      return;
    }
    contract_panic("GC encountered a reference outside the managed heap");
  }
  if (allocation->marked) {
    return;
  }
  if (active_mark_stack == nullptr) {
    contract_panic("GC trace callback ran outside collection");
  }
  allocation->marked = true;
  push_mark(*active_mark_stack, allocation);
}

void mark_slot(neri_ref_v1 *slot, void *) {
  if (slot == nullptr) {
    contract_panic("GC trace callback supplied a null slot");
  }
  mark_object(*slot);
}

void unlink_managed(runtime_state &heap, managed_allocation *allocation) {
  if (allocation->previous != nullptr) {
    allocation->previous->next = allocation->next;
  } else {
    heap.managed_head = allocation->next;
  }
  if (allocation->next != nullptr) {
    allocation->next->previous = allocation->previous;
  }
}

void unlink_native(native_allocation *allocation) {
  if (allocation->previous != nullptr) {
    allocation->previous->next = allocation->next;
  } else {
    current_state().native_head = allocation->next;
  }
  if (allocation->next != nullptr) {
    allocation->next->previous = allocation->previous;
  }
}

void collect_impl() {
  auto &heap = current_state();
  if (heap.collecting) {
    contract_panic("nested GC collection is not supported by ABI v1.0");
  }
  heap.collecting = true;
  mark_stack stack{};
  active_mark_stack = &stack;
  for (auto *frame = heap.root_frame; frame != nullptr;
       frame = frame->previous) {
    if (frame->runtime_cookie != root_frame_cookie ||
        (frame->slot_count != 0 && frame->slots == nullptr)) {
      contract_panic("GC root-frame chain is corrupt");
    }
    for (uint64_t index = 0; index < frame->slot_count; ++index) {
      mark_object(frame->slots[index]);
    }
  }
  for (auto *slot : heap.persistent_roots) {
    if (slot == nullptr) contract_panic("persistent GC root is corrupt");
    mark_object(*slot);
  }
  for (auto *borrow = heap.borrow; borrow != nullptr;
       borrow = reinterpret_cast<neri_gc_borrow_v1 *>(
           borrow->runtime_words[0])) {
    if (borrow->runtime_words[3] != borrow_cookie) {
      contract_panic("managed-borrow chain is corrupt");
    }
    mark_object(reinterpret_cast<neri_ref_v1>(borrow->runtime_words[1]));
  }

  while (stack.count != 0) {
    auto *allocation = stack.items[--stack.count];
    const auto *type = allocation->object->type;
    if (type != nullptr && type->trace != nullptr) {
      type->trace(allocation->object, mark_slot, nullptr);
    }
  }
  active_mark_stack = nullptr;
  std::free(stack.items);

  for (auto *allocation = heap.managed_head; allocation != nullptr;) {
    auto *next = allocation->next;
    if (!allocation->marked) {
      unlink_managed(heap, allocation);
      heap.managed_object_count -= 1;
      heap.managed_byte_count -=
          sizeof(neri_object_header_v1) + allocation->payload_size;
      std::free(allocation);
    } else {
      // Outside collection every live object is unmarked. Reset survivors
      // while sweeping instead of traversing the entire heap before marking.
      allocation->marked = false;
    }
    allocation = next;
  }
  heap.collection_count += 1;
  heap.next_collection_bytes = std::max(collection_floor_bytes,
      heap.managed_byte_count > UINT64_MAX / 2 ? UINT64_MAX : heap.managed_byte_count * 2);
  heap.collecting = false;
  assert_consistent();
}

void validate_type(const neri_type_descriptor_v1 *type,
                   uint64_t payload_size, uint64_t payload_alignment) {
  const bool valid_kind = type != nullptr &&
                          type->kind >= NERI_TYPE_KIND_CLASS_V1 &&
                          type->kind <= NERI_TYPE_KIND_ARRAY_V1;
  const bool valid_array =
      type == nullptr || type->kind != NERI_TYPE_KIND_ARRAY_V1 ||
      (type->element_size != 0 && is_power_of_two(type->element_alignment) &&
       type->element_alignment <= alignof(std::max_align_t) &&
       type->element_size % type->element_alignment == 0 &&
       type->payload_size ==
           ((sizeof(uint64_t) + type->element_alignment - 1) &
            ~(type->element_alignment - 1)) &&
       type->payload_alignment ==
           std::max<uint64_t>(alignof(uint64_t), type->element_alignment) &&
       (type->struct_size < NERI_TYPE_DESCRIPTOR_V1_4_SIZE ||
        type->element_type != nullptr));
  const bool valid_v1_4_fields =
      type == nullptr ||
      type->struct_size < NERI_TYPE_DESCRIPTOR_V1_4_SIZE ||
      (type->reserved_v1_4 == 0 &&
       type->scalar_kind == NERI_SCALAR_KIND_NONE_V1 &&
       type->trace_inline == nullptr &&
       (type->kind == NERI_TYPE_KIND_ARRAY_V1 ||
        type->element_type == nullptr));
  if (!valid_kind ||
      type->struct_size < NERI_TYPE_DESCRIPTOR_V1_BASE_SIZE ||
      (type->abi_minor >= 4 &&
       type->struct_size < NERI_TYPE_DESCRIPTOR_V1_4_SIZE) ||
      !valid_v1_4_fields ||
      type->abi_major != NERI_RUNTIME_ABI_MAJOR ||
      type->abi_minor > NERI_RUNTIME_ABI_MINOR ||
      !valid_array ||
      (type->flags & ~known_type_flags) != 0 || type->reserved != 0 ||
      type->mangled_name == nullptr || type->mangled_name[0] == '\0' ||
      !is_power_of_two(payload_alignment) ||
      payload_alignment > alignof(std::max_align_t) ||
      payload_alignment != type->payload_alignment ||
      payload_size < type->payload_size ||
      (type->kind == NERI_TYPE_KIND_CLASS_V1 &&
       payload_size != type->payload_size) ||
      ((type->flags & NERI_TYPE_FLAG_CONTAINS_REFS_V1) != 0 &&
       type->trace == nullptr) ||
      payload_size > SIZE_MAX - sizeof(neri_object_header_v1)) {
    contract_panic("invalid managed allocation contract");
  }
}

[[nodiscard]] void *allocate_native_memory(uint64_t byte_count,
                                           uint64_t alignment, bool zeroed) {
  if (!is_power_of_two(alignment)) {
    contract_panic(
        "native allocation alignment must be a non-zero power of two");
  }
  const uint64_t requested_size = byte_count == 0 ? 1 : byte_count;
  if (requested_size > SIZE_MAX) {
    contract_panic("native allocation size is not representable on this target");
  }

  const size_t physical_size = static_cast<size_t>(requested_size);
  void *pointer = neri::platform::allocate(physical_size, static_cast<size_t>(alignment), zeroed);
  if (pointer == nullptr) {
    out_of_memory("native allocation failed");
  }

  auto *allocation =
      static_cast<native_allocation *>(std::malloc(sizeof(native_allocation)));
  if (allocation == nullptr ||
      current_state().native_byte_count > UINT64_MAX - byte_count) {
    neri::platform::deallocate(pointer);
    std::free(allocation);
    out_of_memory("native allocation accounting failed");
  }
  *allocation = {nullptr, current_state().native_head, pointer, byte_count, alignment};
  if (current_state().native_head != nullptr) {
    current_state().native_head->previous = allocation;
  }
  current_state().native_head = allocation;
  current_state().native_byte_count += byte_count;
  assert_consistent();
  return pointer;
}

struct string_view final {
  const uint8_t *bytes;
  uint64_t byte_length;
};

[[nodiscard]] string_view inspect_string(neri_ref_v1 value) {
  if (value == nullptr) {
    contract_panic("string operation received null");
  }

  const auto *prefix = reinterpret_cast<const neri_string_prefix_v1 *>(value);
  if (is_string_literal(value)) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(value) +
                        sizeof(neri_string_prefix_v1);
    if (prefix->byte_length > SIZE_MAX || bytes[prefix->byte_length] != 0) {
      contract_panic("invalid static string object");
    }
    return {bytes, prefix->byte_length};
  }

  const auto *allocation = find_readable(value);
  if (allocation == nullptr || value->type != &dynamic_string_type ||
      allocation->payload_size < sizeof(uint64_t) + 1U ||
      prefix->byte_length >
          allocation->payload_size - sizeof(uint64_t) - 1U) {
    contract_panic("invalid managed string object");
  }
  const auto *bytes = reinterpret_cast<const uint8_t *>(value) +
                      sizeof(neri_string_prefix_v1);
  if (bytes[prefix->byte_length] != 0) {
    contract_panic("managed string terminator is corrupt");
  }
  return {bytes, prefix->byte_length};
}

void write_console(FILE *stream, neri_ref_v1 value, bool append_newline,
                   const char *failure_message) {
  require_initialized();
  const auto text = inspect_string(value);
  const auto byte_length = static_cast<size_t>(text.byte_length);
  if ((byte_length != 0 &&
       std::fwrite(text.bytes, 1, byte_length, stream) != byte_length) ||
      (append_newline && std::fputc('\n', stream) == EOF) ||
      std::fflush(stream) != 0) {
    contract_panic(failure_message);
  }
}

[[nodiscard]] bool is_continuation(uint8_t value) {
  return (value & UINT8_C(0xc0)) == UINT8_C(0x80);
}

[[nodiscard]] bool is_strict_utf8(const uint8_t *bytes, size_t size) {
  for (size_t index = 0; index < size;) {
    const auto first = bytes[index++];
    if (first <= UINT8_C(0x7f)) {
      continue;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
      if (index == size || !is_continuation(bytes[index++])) {
        return false;
      }
      continue;
    }
    if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
      if (index + 1U >= size) {
        return false;
      }
      const auto second = bytes[index++];
      const auto third = bytes[index++];
      const auto valid_second =
          first == UINT8_C(0xe0)
              ? second >= UINT8_C(0xa0) && second <= UINT8_C(0xbf)
          : first == UINT8_C(0xed)
              ? second >= UINT8_C(0x80) && second <= UINT8_C(0x9f)
              : is_continuation(second);
      if (!valid_second || !is_continuation(third)) {
        return false;
      }
      continue;
    }
    if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
      if (index + 2U >= size) {
        return false;
      }
      const auto second = bytes[index++];
      const auto third = bytes[index++];
      const auto fourth = bytes[index++];
      const auto valid_second =
          first == UINT8_C(0xf0)
              ? second >= UINT8_C(0x90) && second <= UINT8_C(0xbf)
          : first == UINT8_C(0xf4)
              ? second >= UINT8_C(0x80) && second <= UINT8_C(0x8f)
              : is_continuation(second);
      if (!valid_second || !is_continuation(third) ||
          !is_continuation(fourth)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

[[nodiscard]] neri_ref_v1 allocate_string(uint64_t byte_length) {
  if (byte_length > UINT64_MAX - sizeof(uint64_t) - 1U) {
    out_of_memory("string payload size overflow");
  }
  const uint64_t payload_size = sizeof(uint64_t) + byte_length + 1U;
  auto *result = neri_rt_v1_gc_alloc(&dynamic_string_type, payload_size,
                                       alignof(uint64_t));
  auto *prefix = reinterpret_cast<neri_string_prefix_v1 *>(result);
  prefix->byte_length = byte_length;
  return result;
}

[[nodiscard]] neri_ref_v1 create_ascii_string(std::string_view bytes) {
  auto *result = allocate_string(bytes.size());
  auto *destination = reinterpret_cast<uint8_t *>(result) +
                      sizeof(neri_string_prefix_v1);
  std::memcpy(destination, bytes.data(), bytes.size());
  return result;
}

[[nodiscard]] neri_ref_v1 create_utf8_string(const uint8_t *bytes,
                                               size_t size) {
  if (!is_strict_utf8(bytes, size)) {
    contract_panic("string creation requires valid UTF-8");
  }
  auto *result = allocate_string(size);
  auto *destination = reinterpret_cast<uint8_t *>(result) +
                      sizeof(neri_string_prefix_v1);
  if (size != 0U) {
    std::memcpy(destination, bytes, size);
  }
  return result;
}

[[nodiscard]] bool string_has_nul(const string_view &value) {
  return std::memchr(value.bytes, 0, static_cast<size_t>(value.byte_length)) !=
         nullptr;
}

[[nodiscard]] bool host_string(neri_ref_v1 value, std::string &result,
                               const char *description) {
  const auto inspected = inspect_string(value);
  if (string_has_nul(inspected)) {
    current_state().host_error = std::string(description) + " contains a null byte.";
    return false;
  }
  result.assign(reinterpret_cast<const char *>(inspected.bytes),
                static_cast<size_t>(inspected.byte_length));
  return true;
}

struct array_view final {
  const neri_type_descriptor_v1 *type;
  uint64_t length;
  uint8_t *elements;
};

[[nodiscard]] array_view inspect_array(neri_ref_v1 value) {
  require_initialized();
  const auto *allocation = find_readable(value);
  const auto *type = value == nullptr ? nullptr : value->type;
  if (allocation == nullptr || type == nullptr ||
      type->kind != NERI_TYPE_KIND_ARRAY_V1 || type->element_size == 0U ||
      type->payload_size < sizeof(uint64_t)) {
    contract_panic("array operation received an invalid array");
  }
  const auto *prefix = reinterpret_cast<const neri_array_prefix_v1 *>(value);
  if (prefix->length >
      (UINT64_MAX - type->payload_size) / type->element_size) {
    contract_panic("array length is corrupt");
  }
  const auto required = type->payload_size + prefix->length * type->element_size;
  if (required > allocation->payload_size) {
    contract_panic("array payload is corrupt");
  }
  return {type, prefix->length,
          reinterpret_cast<uint8_t *>(value) +
              sizeof(neri_object_header_v1) + type->payload_size};
}

[[nodiscard]] neri_ref_v1 append_array(neri_ref_v1 values,
                                         const void *value,
                                         bool value_is_reference) {
  const auto initial = inspect_array(values);
  if (initial.length == UINT64_MAX ||
      initial.length + 1U >
          (UINT64_MAX - initial.type->payload_size) /
              initial.type->element_size) {
    out_of_memory("array append length overflow");
  }

  neri_ref_v1 roots[2] = {
      values, value_is_reference ? *static_cast<neri_ref_v1 const *>(value)
                                 : nullptr};
  neri_gc_root_frame_v1 frame = {nullptr, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  const auto payload_size =
      initial.type->payload_size + (initial.length + 1U) * initial.type->element_size;
  auto *result = neri_rt_v1_gc_alloc(initial.type, payload_size,
                                       initial.type->payload_alignment);
  const auto rooted = inspect_array(roots[0]);
  reinterpret_cast<neri_array_prefix_v1 *>(result)->length =
      rooted.length + 1U;
  auto *destination = reinterpret_cast<uint8_t *>(result) +
                      sizeof(neri_object_header_v1) + rooted.type->payload_size;
  const auto existing_size = rooted.length * rooted.type->element_size;
  if (existing_size != 0U) {
    std::memcpy(destination, rooted.elements, static_cast<size_t>(existing_size));
  }
  auto *slot = destination + existing_size;
  if (value_is_reference) {
    neri_rt_v1_gc_store_ref(result, reinterpret_cast<neri_ref_v1 *>(slot),
                              roots[1]);
  } else {
    std::memcpy(slot, value, static_cast<size_t>(rooted.type->element_size));
  }
  neri_rt_v1_gc_root_frame_leave(&frame);
  return result;
}

template <typename Value>
[[nodiscard]] neri_ref_v1 integer_string(Value value) {
  std::array<char, 32> buffer{};
  const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                       value);
  if (converted.ec != std::errc{}) {
    contract_panic("numeric string conversion failed");
  }
  return create_ascii_string(
      std::string_view(buffer.data(), converted.ptr - buffer.data()));
}

struct formatted_float final {
  std::array<char, 768> bytes{};
  std::size_t size = 0;
};

[[nodiscard]] bool has_decimal_float_syntax(std::string_view value) {
  std::size_t index = 0;
  if (index < value.size() && (value[index] == '+' || value[index] == '-')) {
    ++index;
  }

  bool has_digits = false;
  while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
    has_digits = true;
    ++index;
  }
  if (index < value.size() && value[index] == '.') {
    ++index;
    while (index < value.size() && value[index] >= '0' &&
           value[index] <= '9') {
      has_digits = true;
      ++index;
    }
  }
  if (!has_digits) {
    return false;
  }

  if (index < value.size() && (value[index] == 'e' || value[index] == 'E')) {
    ++index;
    if (index < value.size() && (value[index] == '+' || value[index] == '-')) {
      ++index;
    }
    const auto exponent_start = index;
    while (index < value.size() && value[index] >= '0' &&
           value[index] <= '9') {
      ++index;
    }
    if (index == exponent_start) {
      return false;
    }
  }
  return index == value.size();
}

[[nodiscard]] formatted_float format_finite_float(neri_float_v1 value) {
  std::array<char, 64> raw{};
  const auto converted = std::to_chars(raw.data(), raw.data() + raw.size(),
                                       value, std::chars_format::general);
  if (converted.ec != std::errc{}) {
    contract_panic("floating string conversion failed");
  }

  const auto raw_size = static_cast<std::size_t>(converted.ptr - raw.data());
  const bool negative = raw[0] == '-';
  const std::size_t mantissa_start = negative ? 1U : 0U;
  std::size_t mantissa_end = raw_size;
  int exponent = 0;
  for (std::size_t index = mantissa_start; index < raw_size; ++index) {
    if (raw[index] == 'e' || raw[index] == 'E') {
      mantissa_end = index;
      const auto exponent_start =
          index + 1U < raw_size && raw[index + 1U] == '+' ? index + 2U
                                                          : index + 1U;
      const auto parsed = std::from_chars(raw.data() + exponent_start,
                                          raw.data() + raw_size, exponent);
      if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw_size) {
        contract_panic("floating string exponent conversion failed");
      }
      break;
    }
  }

  std::array<char, 32> digits{};
  std::size_t digit_count = 0;
  std::size_t point = mantissa_end;
  for (std::size_t index = mantissa_start; index < mantissa_end; ++index) {
    if (raw[index] == '.') {
      point = index;
    } else {
      digits[digit_count++] = raw[index];
    }
  }
  int decimal_position =
      static_cast<int>((point == mantissa_end ? mantissa_end : point) -
                       mantissa_start) +
      exponent;
  std::size_t first_digit = 0;
  while (first_digit + 1U < digit_count && digits[first_digit] == '0') {
    ++first_digit;
    --decimal_position;
  }
  while (digit_count > first_digit + 1U && digits[digit_count - 1U] == '0') {
    --digit_count;
  }
  const std::size_t significant_count = digit_count - first_digit;

  const auto sign_size = negative ? 1U : 0U;
  const std::size_t fixed_size = sign_size +
      (decimal_position <= 0
           ? 2U + static_cast<std::size_t>(-decimal_position) + significant_count
           : decimal_position >= static_cast<int>(significant_count)
                 ? static_cast<std::size_t>(decimal_position)
                 : significant_count + 1U);
  const int scientific_exponent = decimal_position - 1;
  std::array<char, 16> exponent_bytes{};
  const auto formatted_exponent =
      std::to_chars(exponent_bytes.data(),
                    exponent_bytes.data() + exponent_bytes.size(),
                    scientific_exponent);
  if (formatted_exponent.ec != std::errc{}) {
    contract_panic("floating string exponent formatting failed");
  }
  const auto exponent_size = static_cast<std::size_t>(
      formatted_exponent.ptr - exponent_bytes.data());
  const std::size_t scientific_size =
      sign_size + significant_count + (significant_count > 1U ? 1U : 0U) +
      1U + exponent_size;

  formatted_float result{};
  auto append = [&](char byte) { result.bytes[result.size++] = byte; };
  auto append_range = [&](const char *begin, std::size_t size) {
    std::memcpy(result.bytes.data() + result.size, begin, size);
    result.size += size;
  };
  if (negative) {
    append('-');
  }
  if (scientific_size < fixed_size) {
    append(digits[first_digit]);
    if (significant_count > 1U) {
      append('.');
      append_range(digits.data() + first_digit + 1U,
                   significant_count - 1U);
    }
    append('e');
    append_range(exponent_bytes.data(), exponent_size);
    return result;
  }

  if (decimal_position <= 0) {
    append('0');
    append('.');
    for (int zero = 0; zero < -decimal_position; ++zero) {
      append('0');
    }
    append_range(digits.data() + first_digit, significant_count);
  } else if (decimal_position >= static_cast<int>(significant_count)) {
    append_range(digits.data() + first_digit, significant_count);
    for (int zero = static_cast<int>(significant_count);
         zero < decimal_position; ++zero) {
      append('0');
    }
  } else {
    const auto integral_count = static_cast<std::size_t>(decimal_position);
    append_range(digits.data() + first_digit, integral_count);
    append('.');
    append_range(digits.data() + first_digit + integral_count,
                 significant_count - integral_count);
  }
  return result;
}
void release_native_allocations(runtime_state &heap) {
  while (heap.native_head != nullptr) {
    auto *allocation = heap.native_head;
    heap.native_head = allocation->next;
    neri::platform::deallocate(allocation->pointer);
    std::free(allocation);
  }
  heap.native_byte_count = 0;
}

void release_heap() {
  require_initialized();
  auto &heap = current_state();
  if (heap.root_frame != nullptr || heap.borrow != nullptr || heap.collecting ||
      heap.foreign_entry != nullptr || !heap.persistent_roots.empty()) {
    contract_panic("runtime shutdown requires no live roots, borrows or foreign entries");
  }
  while (heap.managed_head != nullptr) {
    auto *allocation = heap.managed_head;
    heap.managed_head = allocation->next;
    std::free(allocation);
  }
  release_native_allocations(heap);
  heap = {};
}

void adopt_results(neri_task_scope &scope) {
  auto &parent = *scope.parent;
  while (scope.results != nullptr) {
    auto *result = scope.results;
    scope.results = result->next;
    auto &child = result->heap;
    if (parent.managed_object_count > UINT64_MAX - child.managed_object_count ||
        parent.managed_byte_count > UINT64_MAX - child.managed_byte_count) {
      out_of_memory("task result allocation accounting overflow");
    }
    managed_allocation *tail = nullptr;
    for (auto *allocation = child.managed_head; allocation != nullptr;
         allocation = allocation->next) {
      assert(allocation->owner == &child);
      allocation->owner = &parent;
      tail = allocation;
    }
    if (tail != nullptr) {
      tail->next = parent.managed_head;
      if (parent.managed_head != nullptr) {
        parent.managed_head->previous = tail;
      }
      parent.managed_head = child.managed_head;
    }
    parent.managed_object_count += child.managed_object_count;
    parent.managed_byte_count += child.managed_byte_count;
    delete result;
  }
}

struct generated_range {
  neri_ref_v1 callback;
  neri_task_generate_fn_v1 adapter;
  uint8_t *elements;
  uint64_t stride;
};

void generate_range(uint64_t begin, uint64_t end, void *context) {
  const auto &range = *static_cast<generated_range *>(context);
  for (auto index = begin; index < end; ++index) {
    range.adapter(range.callback, static_cast<neri_int_v1>(index),
                  range.elements + index * range.stride);
  }
}
} // namespace

struct session_layout_record final {
  std::string canonical_layout;
  uint32_t kind{};
  uint32_t flags{};
  uint64_t payload_size{};
  uint64_t payload_alignment{};
  std::vector<uint64_t> trace_offsets;
};

struct session_coordinator final {
  std::recursive_mutex mutex;
  std::thread::id owner_thread;
  bool busy{};
  bool reset_failed{};
  uint64_t nonce{};
  neri_ref_v1 state{};
  uint64_t display_offset{UINT64_MAX};
  std::string target_type;
  std::map<std::string, session_layout_record> layouts;
  std::vector<void *> modules;
  void *object_linker_library{};
  neri_session_linker_v1 *object_linker{};
  std::vector<neri_session_generation_v1 *> object_generations;
  decltype(&neri_session_linker_create_v1) object_create{};
  decltype(&neri_session_linker_destroy_v1) object_destroy{};
  decltype(&neri_session_linker_add_object_v1) object_add{};
  decltype(&neri_session_linker_symbol_v1) object_symbol{};
  decltype(&neri_session_linker_remove_v1) object_remove{};
  std::string error;
};

std::mutex session_mutex;
std::map<int64_t, std::shared_ptr<session_coordinator>> sessions;
std::atomic<uint64_t> next_session_nonce{1};
std::atomic<uint64_t> next_session_identity{1};
std::atomic<uint64_t> next_session_metric{1};

[[nodiscard]] bool session_power_of_two(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] bool session_layout_equal(const session_layout_record &left,
                                        const session_layout_record &right) {
  return left.kind == right.kind && left.flags == right.flags &&
         left.canonical_layout == right.canonical_layout &&
         left.payload_size == right.payload_size &&
         left.payload_alignment == right.payload_alignment &&
         left.trace_offsets == right.trace_offsets;
}

[[nodiscard]] int64_t session_validate(session_coordinator &session,
                                       void *accessor_symbol,
                                       void *entry_symbol,
                                       const char *entry_name,
                                       const neri_session_module_metadata_v1 *&metadata) {
  if (accessor_symbol == nullptr || entry_symbol == nullptr) {
    session.error = "session module exports are missing";
    return NERI_SESSION_INVALID_METADATA_V1;
  }
  neri_session_module_accessor_v1 accessor{};
  static_assert(sizeof(accessor) == sizeof(accessor_symbol));
  std::memcpy(&accessor, &accessor_symbol, sizeof(accessor));
  metadata = accessor();
  void *metadata_entry{};
  if (metadata != nullptr) std::memcpy(&metadata_entry, &metadata->entry, sizeof(metadata_entry));
  constexpr auto metadata_v1_size = offsetof(neri_session_module_metadata_v1, display_offset);
  if (metadata == nullptr || metadata->struct_size < metadata_v1_size ||
      metadata->version_major != 1 || metadata->version_minor > 1 ||
      (metadata->version_minor >= 1 &&
       metadata->struct_size < sizeof(neri_session_module_metadata_v1)) ||
      metadata->entry_name == nullptr ||
      std::strcmp(metadata->entry_name, entry_name) != 0 ||
      metadata_entry != entry_symbol || metadata->source_type_id == nullptr ||
      metadata->target_type_id == nullptr || metadata->layouts == nullptr ||
      metadata->layout_count == 0 || metadata->layout_count > 4096) {
    session.error = "session module metadata header is invalid";
    return NERI_SESSION_INVALID_METADATA_V1;
  }
  std::map<std::string, session_layout_record> candidate;
  for (uint64_t index = 0; index < metadata->layout_count; ++index) {
    const auto &layout = metadata->layouts[index];
    if (layout.type_id == nullptr || layout.type_id[0] == '\0' ||
        layout.canonical_layout == nullptr || layout.canonical_layout[0] == '\0' ||
        layout.kind != NERI_TYPE_KIND_CLASS_V1 || layout.payload_size > SIZE_MAX ||
        !session_power_of_two(layout.payload_alignment) ||
        layout.payload_alignment > alignof(std::max_align_t) ||
        layout.trace_offset_count > 4096 ||
        (layout.trace_offset_count != 0 && layout.trace_offsets == nullptr)) {
      session.error = "session module layout is invalid";
      return NERI_SESSION_INVALID_METADATA_V1;
    }
    session_layout_record record{layout.canonical_layout, layout.kind, layout.flags, layout.payload_size,
                                 layout.payload_alignment, {}};
    uint64_t previous = 0;
    for (uint64_t offset_index = 0; offset_index < layout.trace_offset_count; ++offset_index) {
      const auto offset = layout.trace_offsets[offset_index];
      if (offset < sizeof(neri_object_header_v1) ||
          offset > sizeof(neri_object_header_v1) + layout.payload_size - sizeof(neri_ref_v1) ||
          offset % alignof(neri_ref_v1) != 0 || (offset_index != 0 && offset <= previous)) {
        session.error = "session module trace layout is invalid";
        return NERI_SESSION_INVALID_METADATA_V1;
      }
      previous = offset;
      record.trace_offsets.push_back(offset);
    }
    if (((layout.flags & NERI_TYPE_FLAG_CONTAINS_REFS_V1) != 0) !=
        !record.trace_offsets.empty() || !candidate.emplace(layout.type_id, record).second) {
      session.error = "session module layout flags or identities are invalid";
      return NERI_SESSION_INVALID_METADATA_V1;
    }
  }
  if (!candidate.contains(metadata->target_type_id) ||
      (std::strcmp(metadata->source_type_id, "neri:type:v1:void") != 0 &&
       !candidate.contains(metadata->source_type_id))) {
    session.error = "session frame layout is absent";
    return NERI_SESSION_INVALID_METADATA_V1;
  }
  if (metadata->version_minor >= 1 &&
      metadata->struct_size >= sizeof(neri_session_module_metadata_v1) &&
      metadata->display_offset != UINT64_MAX) {
    const auto &target = candidate.at(metadata->target_type_id);
    if (metadata->display_offset < sizeof(neri_object_header_v1) ||
        metadata->display_offset >
            sizeof(neri_object_header_v1) + target.payload_size - sizeof(neri_ref_v1) ||
        !std::ranges::binary_search(target.trace_offsets,
                                    metadata->display_offset)) {
      session.error = "session display field offset is invalid";
      return NERI_SESSION_INVALID_METADATA_V1;
    }
  }
  if ((session.state == nullptr) !=
      (std::strcmp(metadata->source_type_id, "neri:type:v1:void") == 0) ||
      (session.state != nullptr && session.target_type != metadata->source_type_id)) {
    session.error = "session source frame identity does not match current state";
    return NERI_SESSION_INCOMPATIBLE_FRAME_V1;
  }
  for (const auto &[id, record] : candidate) {
    if (const auto found = session.layouts.find(id);
        found != session.layouts.end() && !session_layout_equal(found->second, record)) {
      session.error = "session managed layout changed for identity " + id;
      return NERI_SESSION_INCOMPATIBLE_LAYOUT_V1;
    }
  }
  return NERI_SESSION_OK_V1;
}

extern "C" {
NERI_RT_API extern const unsigned char neri_rt_v1_abi_anchor = 0;

NERI_RT_API extern const neri_type_descriptor_v1
    neri_rt_v1_string_literal_type = {
        sizeof(neri_type_descriptor_v1),
        NERI_RUNTIME_ABI_MAJOR,
        NERI_RUNTIME_ABI_MINOR,
        NERI_TYPE_KIND_STRING_V1,
        NERI_TYPE_FLAG_IMMUTABLE_V1,
        sizeof(uint64_t),
        alignof(uint64_t),
        0,
        0,
        nullptr,
        nullptr,
        "hk1_t_q1_n14_537472696e674c69746572616c",
        0,
        nullptr,
        nullptr,
        NERI_SCALAR_KIND_NONE_V1,
        0,
};

NERI_RT_API const neri_runtime_abi_info_v1 *neri_rt_v1_get_abi(void) {
  return &runtime_abi;
}

NERI_RT_API neri_ref_v1 neri_rt_v1_task_generate(neri_int_v1 count,
    neri_int_v1 parallelism, const neri_type_descriptor_v1 *array_type,
    neri_ref_v1 callback, neri_task_generate_fn_v1 adapter) {
  require_initialized();
  if (count < 0 || parallelism < 0 || static_cast<uint64_t>(parallelism) > UINT32_MAX ||
      array_type == nullptr || array_type->kind != NERI_TYPE_KIND_ARRAY_V1 ||
      array_type->element_size == 0 || callback == nullptr ||
      find_readable(callback) == nullptr || adapter == nullptr) {
    contract_panic("invalid task generation contract");
  }
  const auto length = static_cast<uint64_t>(count);
  if (length > (UINT64_MAX - array_type->payload_size) / array_type->element_size) {
    out_of_memory("task result array size overflow");
  }
  const bool references = (array_type->flags & NERI_TYPE_FLAG_CONTAINS_REFS_V1) != 0;
  if (references && (array_type->element_size != sizeof(neri_ref_v1) ||
                     array_type->element_alignment != alignof(neri_ref_v1))) {
    contract_panic("task result references require pointer-sized elements");
  }
  neri_ref_v1 roots[2]{callback, nullptr};
  neri_gc_root_frame_v1 frame{nullptr, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  roots[1] = neri_rt_v1_gc_alloc(array_type,
      array_type->payload_size + length * array_type->element_size,
      array_type->payload_alignment);
  reinterpret_cast<neri_array_prefix_v1 *>(roots[1])->length = length;
  auto *elements = reinterpret_cast<uint8_t *>(roots[1]) +
      sizeof(neri_object_header_v1) + array_type->payload_size;
  generated_range range{callback, adapter, elements, array_type->element_size};
  neri_task_parallel_for(length, parallelism == 0 ? UINT32_MAX : static_cast<uint32_t>(parallelism),
      generate_range, &range, references ? reinterpret_cast<neri_ref_v1 *>(elements) : nullptr,
      references ? 1 : 0);
  neri_rt_v1_gc_root_frame_leave(&frame);
  return roots[1];
}

void neri_task_scope_run(neri_task_coordinator coordinate, void *context) {
  require_initialized();
  auto &parent = current_state();
  if (coordinate == nullptr || parent.collecting || parent.borrow != nullptr) {
    contract_panic("task scopes require a coordinator and no active trace or native borrow");
  }
  neri_task_scope scope{&parent};
  parent.suspended = true;
  coordinate(&scope, context);
  {
    std::unique_lock lock(scope.mutex);
    scope.accepting = false;
    scope.completed.wait(lock, [&scope] { return scope.outstanding == 0; });
  }
  adopt_results(scope);
  parent.suspended = false;
  assert_consistent();
}

neri_task_ticket *neri_task_register(neri_task_scope *scope) {
  return neri_task_register_results(scope, nullptr, 0);
}

neri_task_ticket *neri_task_register_results(neri_task_scope *scope,
                                            neri_ref_v1 *slots, uint64_t count) {
  if (scope == nullptr || scope->parent != &current_state() ||
      !current_state().suspended) {
    contract_panic("only the suspended coordinator may register a task");
  }
  if ((count != 0 && slots == nullptr) || count > SIZE_MAX / sizeof(neri_ref_v1)) {
    contract_panic("task result span requires valid reference storage");
  }
  auto *ticket = static_cast<neri_task_ticket *>(std::malloc(sizeof(neri_task_ticket)));
  if (ticket == nullptr) {
    out_of_memory("task ticket allocation failed");
  }
  std::lock_guard lock(scope->mutex);
  if (!scope->accepting || scope->outstanding == UINT64_MAX) {
    contract_panic("task registration requires an open scope");
  }
  *ticket = {scope, slots, count};
  ++scope->outstanding;
  return ticket;
}

void neri_task_execute(neri_task_ticket *ticket, neri_task_body body, void *context) {
  if (ticket == nullptr || body == nullptr || active_mark_stack != nullptr) {
    contract_panic("task execution requires a ticket and body outside tracing");
  }
  auto *scope = ticket->scope;
  auto *parent = scope->parent;
  {
    std::lock_guard lock(scope->mutex);
    if (scope->outstanding == 0 || !parent->suspended) {
      contract_panic("task execution requires a suspended parent scope");
    }
  }
  runtime_state local{};
  task_result_heap *result = nullptr;
  if (ticket->result_count != 0) {
    result = new (std::nothrow) task_result_heap;
    if (result == nullptr) {
      out_of_memory("task result heap allocation failed");
    }
  }
  auto &child = result == nullptr ? local : result->heap;
  child.initialized = true;
  child.read_parent = parent;
  child.process_argument_count = parent->process_argument_count;
  child.process_arguments = parent->process_arguments;
  auto *previous = active_state;
  active_state = &child;
  neri_gc_root_frame_v1 results{nullptr, ticket->results, ticket->result_count, 0};
  if (result != nullptr) {
    neri_rt_v1_gc_root_frame_enter(&results);
  }
  body(context);
  if (result == nullptr) {
    release_heap();
  } else {
    require_initialized();
    if (child.root_frame != &results || child.borrow != nullptr || child.collecting ||
        child.foreign_entry != nullptr) {
      contract_panic("task results require no live body roots, borrows or foreign entries");
    }
    collect_impl();
    neri_rt_v1_gc_root_frame_leave(&results);
    release_native_allocations(child);
  }
  active_state = previous;
  std::free(ticket);
  // Completed result heaps stay alive until join; no sibling may read or adopt
  // them. No access to the scope or its ancestors follows this critical section.
  {
    std::lock_guard lock(scope->mutex);
    if (result != nullptr) {
      result->next = scope->results;
      scope->results = result;
    }
    --scope->outstanding;
    if (scope->outstanding == 0) {
      scope->completed.notify_all();
    }
  }
}

NERI_RT_API neri_abi_status_v1 neri_rt_v1_initialize(
    const neri_runtime_abi_requirements_v1 *requirements) {
  if (current_state().suspended) {
    contract_panic("cannot initialize a suspended task parent");
  }
  const auto status = validate_requirements(requirements);
  if (status != NERI_ABI_STATUS_OK_V1) return status;
  current_state().initialized = true;
  current_state().host_error.clear();
  return NERI_ABI_STATUS_OK_V1;
}

NERI_RT_API void neri_rt_v1_foreign_enter(
    neri_foreign_entry_v1 *entry,
    const neri_runtime_abi_requirements_v1 *requirements) {
  auto &heap = current_state();
  if (entry == nullptr || heap.suspended || heap.collecting) {
    contract_panic("foreign entry requires a token and an active, non-collecting heap");
  }
  // Inspect registered tokens only: caller storage may be uninitialized.
  // Suspended ancestor heaps may still own tokens while a task is helping.
  for (const auto *owner = &heap; owner != nullptr; owner = owner->read_parent) {
    for (auto *active = owner->foreign_entry; active != nullptr;
         active = reinterpret_cast<neri_foreign_entry_v1 *>(active->runtime_words[0])) {
      if (active == entry) contract_panic("foreign entry token is already active");
    }
  }
  if (validate_requirements(requirements) != NERI_ABI_STATUS_OK_V1) {
    panic_raw(NERI_PANIC_ABI_MISMATCH_V1,
              "foreign entry runtime ABI negotiation failed", nullptr);
  }
  const bool owns_heap = !heap.initialized;
  heap.initialized = true;
  *entry = {{reinterpret_cast<uintptr_t>(heap.foreign_entry),
             reinterpret_cast<uintptr_t>(&heap),
             reinterpret_cast<uintptr_t>(heap.root_frame),
             reinterpret_cast<uintptr_t>(heap.borrow),
             owns_heap ? uintptr_t{1} : uintptr_t{0}, foreign_entry_cookie}};
  heap.foreign_entry = entry;
}

NERI_RT_API void neri_rt_v1_foreign_leave(neri_foreign_entry_v1 *entry) {
  require_initialized();
  auto &heap = current_state();
  // Check membership before reading token storage, including wrong-thread use.
  if (entry == nullptr || heap.foreign_entry != entry ||
      entry->runtime_words[1] != reinterpret_cast<uintptr_t>(&heap) ||
      entry->runtime_words[5] != foreign_entry_cookie || heap.collecting) {
    contract_panic("foreign entries must leave their owning heap in LIFO order");
  }
  if (entry->runtime_words[2] != reinterpret_cast<uintptr_t>(heap.root_frame) ||
      entry->runtime_words[3] != reinterpret_cast<uintptr_t>(heap.borrow)) {
    contract_panic("foreign entry must restore its root and borrow chains");
  }
  const bool owns_heap = entry->runtime_words[4] != 0;
  heap.foreign_entry =
      reinterpret_cast<neri_foreign_entry_v1 *>(entry->runtime_words[0]);
  *entry = {};
  if (owns_heap) release_heap();
}

NERI_RT_API void
neri_rt_v1_set_process_arguments(int argc, const char *const *argv) {
  require_initialized();
  if (argc < 0 || (argc != 0 && argv == nullptr)) {
    contract_panic("invalid process argument vector");
  }
  current_state().process_argument_count = argc > 0 ? argc - 1 : 0;
  current_state().process_arguments = argc > 0 ? argv + 1 : nullptr;
}

NERI_RT_API void neri_rt_v1_shutdown(void) {
  if (!current_state().initialized) {
    return;
  }
  release_heap();
  if (std::fflush(stdout) != 0 || std::fflush(stderr) != 0) {
    contract_panic("console flush failed during runtime shutdown");
  }
}

NERI_RT_API neri_ref_v1
neri_rt_v1_gc_alloc(const neri_type_descriptor_v1 *type,
                      uint64_t payload_size, uint64_t payload_alignment) {
  require_initialized();
  auto &heap = current_state();
  if (heap.collecting) {
    contract_panic("managed allocation is not allowed during tracing");
  }
  validate_type(type, payload_size, payload_alignment);

  const size_t total_size =
      sizeof(neri_object_header_v1) + static_cast<size_t>(payload_size);
  if (total_size > SIZE_MAX - managed_prefix_size) {
    out_of_memory("managed allocation size is not representable");
  }
  if (heap.managed_byte_count >= heap.next_collection_bytes ||
      total_size > heap.next_collection_bytes - heap.managed_byte_count) {
    collect_impl();
  }

  const size_t physical_size = managed_prefix_size + total_size;
  auto *metadata =
      static_cast<managed_allocation *>(std::calloc(1, physical_size));
  if (metadata == nullptr) {
    collect_impl();
    metadata = static_cast<managed_allocation *>(
        std::calloc(1, physical_size));
  }
  if (metadata == nullptr ||
      heap.managed_byte_count > UINT64_MAX - total_size ||
      heap.managed_object_count == UINT64_MAX) {
    std::free(metadata);
    out_of_memory("managed allocation failed");
  }

  auto *object = reinterpret_cast<neri_ref_v1>(
      reinterpret_cast<unsigned char *>(metadata) + managed_prefix_size);

  *metadata = {nullptr, heap.managed_head, object, payload_size, &heap, false};
  if (heap.managed_head != nullptr) {
    heap.managed_head->previous = metadata;
  }
  heap.managed_head = metadata;
  object->type = type;
  object->runtime_word = reinterpret_cast<uintptr_t>(metadata);
  heap.managed_object_count += 1;
  heap.managed_byte_count += total_size;
  assert_consistent();
  return object;
}

NERI_RT_API void
neri_rt_v1_gc_root_frame_enter(neri_gc_root_frame_v1 *frame) {
  require_initialized();
  if (frame == nullptr || (frame->slot_count != 0 && frame->slots == nullptr) ||
      frame->previous != nullptr || frame->runtime_cookie != 0) {
    contract_panic("invalid GC root frame");
  }
  frame->previous = current_state().root_frame;
  frame->runtime_cookie = root_frame_cookie;
  current_state().root_frame = frame;
}

NERI_RT_API void
neri_rt_v1_gc_root_frame_leave(neri_gc_root_frame_v1 *frame) {
  require_initialized();
  if (frame == nullptr || frame != current_state().root_frame ||
      frame->runtime_cookie != root_frame_cookie) {
    contract_panic("GC root frames must leave in LIFO order");
  }
  current_state().root_frame = frame->previous;
  frame->previous = nullptr;
  frame->runtime_cookie = 0;
}

NERI_RT_API void neri_rt_v1_gc_store_ref(neri_ref_v1 owner,
                                             neri_ref_v1 *slot,
                                             neri_ref_v1 value) {
  require_initialized();
  auto *allocation = find_managed(owner);
  if (allocation == nullptr || slot == nullptr) {
    contract_panic("invalid managed reference store");
  }
  const uintptr_t payload_begin =
      reinterpret_cast<uintptr_t>(owner) + sizeof(neri_object_header_v1);
  const uintptr_t payload_end = payload_begin + allocation->payload_size;
  const uintptr_t slot_address = reinterpret_cast<uintptr_t>(slot);
  if (allocation->payload_size < sizeof(neri_ref_v1) ||
      slot_address < payload_begin ||
      slot_address > payload_end - sizeof(neri_ref_v1) ||
      slot_address % alignof(neri_ref_v1) != 0) {
    contract_panic("managed reference slot is outside the owner payload");
  }
  if (value != nullptr && find_readable(value) == nullptr &&
      !is_string_literal(value)) {
    contract_panic("managed reference value is not a live allocation");
  }
  *slot = value;
}

NERI_RT_API void neri_rt_v1_gc_keep_alive(neri_ref_v1 object) {
  require_initialized();
  if (object != nullptr && find_readable(object) == nullptr &&
      !is_string_literal(object)) {
    contract_panic("gc.keep_alive requires a live managed reference");
  }
  volatile uintptr_t observable = reinterpret_cast<uintptr_t>(object);
  static_cast<void>(observable);
}

NERI_RT_API void neri_rt_v1_gc_collect(void) {
  require_initialized();
  collect_impl();
}

NERI_RT_API void neri_rt_v1_gc_get_stats(neri_gc_stats_v1 *stats) {
  require_initialized();
  if (stats == nullptr || stats->struct_size < sizeof(neri_gc_stats_v1)) {
    contract_panic("invalid GC stats destination");
  }
  stats->reserved = 0;
  stats->managed_object_count = current_state().managed_object_count;
  stats->managed_byte_count = current_state().managed_byte_count;
  stats->collection_count = current_state().collection_count;
  stats->native_byte_count = current_state().native_byte_count;
}

NERI_RT_API neri_ref_v1
neri_rt_v1_string_concat(neri_ref_v1 left, neri_ref_v1 right) {
  require_initialized();
  const auto initial_left = inspect_string(left);
  const auto initial_right = inspect_string(right);
  if (initial_left.byte_length > UINT64_MAX - initial_right.byte_length) {
    out_of_memory("string concatenation length overflow");
  }
  const uint64_t byte_length =
      initial_left.byte_length + initial_right.byte_length;

  neri_ref_v1 roots[2] = {left, right};
  neri_gc_root_frame_v1 frame = {nullptr, roots, 2, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  auto *result = allocate_string(byte_length);
  const auto rooted_left = inspect_string(roots[0]);
  const auto rooted_right = inspect_string(roots[1]);
  auto *destination = reinterpret_cast<uint8_t *>(result) +
                      sizeof(neri_string_prefix_v1);
  std::memcpy(destination, rooted_left.bytes,
              static_cast<size_t>(rooted_left.byte_length));
  std::memcpy(destination + rooted_left.byte_length, rooted_right.bytes,
              static_cast<size_t>(rooted_right.byte_length));
  neri_rt_v1_gc_root_frame_leave(&frame);
  return result;
}

NERI_RT_API neri_bool_v1
neri_rt_v1_string_equal(neri_ref_v1 left, neri_ref_v1 right) {
  require_initialized();
  if (left == right) {
    return 1;
  }
  if (left == nullptr || right == nullptr) {
    return 0;
  }
  const auto left_view = inspect_string(left);
  const auto right_view = inspect_string(right);
  return left_view.byte_length == right_view.byte_length &&
                 std::memcmp(left_view.bytes, right_view.bytes,
                             static_cast<size_t>(left_view.byte_length)) == 0
             ? 1
             : 0;
}

NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_int(neri_int_v1 value) {
  require_initialized();
  return integer_string(value);
}

NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_byte(neri_byte_v1 value) {
  require_initialized();
  return integer_string(static_cast<unsigned int>(value));
}

NERI_RT_API neri_ref_v1
neri_rt_v1_string_from_float(neri_float_v1 value) {
  require_initialized();
  if (std::isnan(value)) {
    return create_ascii_string("nan");
  }
  if (std::isinf(value)) {
    return create_ascii_string(std::signbit(value) ? "-inf" : "inf");
  }

  const auto formatted = format_finite_float(value);
  return create_ascii_string(
      std::string_view(formatted.bytes.data(), formatted.size));
}

NERI_RT_API void neri_rt_v1_stdout_write(neri_ref_v1 value) {
  write_console(stdout, value, false, "standard output write failed");
}

NERI_RT_API void neri_rt_v1_stdout_write_line(neri_ref_v1 value) {
  write_console(stdout, value, true, "standard output write failed");
}

NERI_RT_API void neri_rt_v1_stderr_write(neri_ref_v1 value) {
  write_console(stderr, value, false, "standard error write failed");
}

NERI_RT_API neri_int_v1 neri_rt_v1_stderr_write_bytes(const uint8_t *bytes,
                                                   neri_int_v1 length) {
  if (!bytes || length < 0) return -1;
  return std::fwrite(bytes, 1, static_cast<size_t>(length), stderr) ==
                 static_cast<size_t>(length) && std::fflush(stderr) == 0
             ? 0 : -1;
}

static neri_ref_v1 read_console_line(bool distinguish_eof) {
  require_initialized();
  std::vector<uint8_t> bytes;
  bool reached_eof = false;
  while (true) {
    const auto next = std::fgetc(stdin);
    if (next == EOF) {
      if (std::ferror(stdin) != 0) {
        contract_panic("standard input read failed");
      }
      reached_eof = true;
      break;
    }
    if (next == '\n') {
      break;
    }
    bytes.push_back(static_cast<uint8_t>(next));
  }
  if (!bytes.empty() && bytes.back() == static_cast<uint8_t>('\r')) {
    bytes.pop_back();
  }
  if (!is_strict_utf8(bytes.data(), bytes.size())) {
    contract_panic("standard input is not valid UTF-8");
  }

  if (distinguish_eof && reached_eof && bytes.empty()) return nullptr;

  auto *result = allocate_string(bytes.size());
  auto *destination = reinterpret_cast<uint8_t *>(result) +
                      sizeof(neri_string_prefix_v1);
  if (!bytes.empty()) {
    std::memcpy(destination, bytes.data(), bytes.size());
  }
  return result;
}

NERI_RT_API neri_ref_v1 neri_rt_v1_stdin_read_line(void) {
  return read_console_line(false);
}

NERI_RT_API neri_ref_v1 neri_rt_v1_stdin_read_line_optional(void) {
  return read_console_line(true);
}

NERI_RT_API neri_int_v1
neri_rt_v1_host_string_byte_length(neri_ref_v1 value) {
  require_initialized();
  const auto inspected = inspect_string(value);
  if (inspected.byte_length > INT64_MAX) {
    contract_panic("string byte length is not representable as Int");
  }
  return static_cast<neri_int_v1>(inspected.byte_length);
}

NERI_RT_API neri_byte_v1
neri_rt_v1_host_string_byte_at(neri_ref_v1 value, neri_int_v1 index) {
  require_initialized();
  const auto inspected = inspect_string(value);
  if (index < 0 || static_cast<uint64_t>(index) >= inspected.byte_length) {
    panic_raw(NERI_PANIC_BOUNDS_V1,
              "String byte index is out of bounds.", nullptr);
  }
  return inspected.bytes[index];
}

NERI_RT_API neri_ref_v1 neri_rt_v1_host_string_slice(
    neri_ref_v1 value, neri_int_v1 start, neri_int_v1 length) {
  require_initialized();
  const auto inspected = inspect_string(value);
  if (start < 0 || length < 0 ||
      static_cast<uint64_t>(start) > inspected.byte_length ||
      static_cast<uint64_t>(length) >
          inspected.byte_length - static_cast<uint64_t>(start)) {
    panic_raw(NERI_PANIC_BOUNDS_V1,
              "String byte slice is out of bounds.", nullptr);
  }
  const auto *begin = inspected.bytes + start;
  if (!is_strict_utf8(begin, static_cast<size_t>(length))) {
    panic_raw(NERI_PANIC_BOUNDS_V1,
              "String byte slice splits a UTF-8 scalar.", nullptr);
  }
  neri_ref_v1 root = value;
  neri_gc_root_frame_v1 frame = {nullptr, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  const auto rooted = inspect_string(root);
  auto *result = create_utf8_string(rooted.bytes + start,
                                    static_cast<size_t>(length));
  neri_rt_v1_gc_root_frame_leave(&frame);
  return result;
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_string_from_bytes(neri_ref_v1 bytes) {
  require_initialized();
  const auto initial = inspect_array(bytes);
  if (initial.type->element_type == nullptr ||
      initial.type->element_type->scalar_kind != NERI_SCALAR_KIND_BYTE_V1 ||
      initial.type->element_size != sizeof(neri_byte_v1)) {
    contract_panic("stringFromBytes requires Byte[]");
  }
  if (!is_strict_utf8(initial.elements, static_cast<size_t>(initial.length))) {
    current_state().host_error = "Byte array is not valid UTF-8.";
    return nullptr;
  }
  neri_ref_v1 root = bytes;
  neri_gc_root_frame_v1 frame = {nullptr, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  const auto rooted = inspect_array(root);
  auto *result = create_utf8_string(rooted.elements,
                                    static_cast<size_t>(rooted.length));
  neri_rt_v1_gc_root_frame_leave(&frame);
  current_state().host_error.clear();
  return result;
}

NERI_RT_API void neri_rt_v1_host_parse_int(
    neri_optional_int_v1 *result, neri_ref_v1 value) {
  require_initialized();
  if (result == nullptr) {
    contract_panic("parseInt requires a result destination");
  }
  *result = {};
  const auto inspected = inspect_string(value);
  neri_int_v1 parsed = 0;
  const auto *begin = reinterpret_cast<const char *>(inspected.bytes);
  const auto converted = std::from_chars(
      begin, begin + static_cast<size_t>(inspected.byte_length), parsed, 10);
  if (converted.ec == std::errc{} &&
      converted.ptr == begin + inspected.byte_length) {
    result->has_value = 1;
    result->value = parsed;
  }
}

NERI_RT_API void neri_rt_v1_host_parse_float(
    neri_optional_float_v1 *result, neri_ref_v1 value) {
  require_initialized();
  if (result == nullptr) {
    contract_panic("parseFloat requires a result destination");
  }
  *result = {};
  const auto inspected = inspect_string(value);
  const auto *begin = reinterpret_cast<const char *>(inspected.bytes);
  const std::string_view text(begin,
                              static_cast<size_t>(inspected.byte_length));
  if (!has_decimal_float_syntax(text)) {
    return;
  }
  const std::string terminated(text);
  char *converted_end = nullptr;
  const auto parsed = neri::platform::parse_float(terminated.c_str(), &converted_end);
  if (converted_end == terminated.data() + terminated.size() &&
      std::isfinite(parsed)) {
    result->has_value = 1;
    result->value = parsed;
  }
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_byte(neri_ref_v1 values, neri_byte_v1 value) {
  const auto inspected = inspect_array(values);
  if (inspected.type->element_type == nullptr ||
      inspected.type->element_type->scalar_kind != NERI_SCALAR_KIND_BYTE_V1 ||
      inspected.type->element_size != sizeof(value)) {
    contract_panic("appendByte requires Byte[]");
  }
  return append_array(values, &value, false);
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_int(neri_ref_v1 values, neri_int_v1 value) {
  const auto inspected = inspect_array(values);
  if (inspected.type->element_type == nullptr ||
      inspected.type->element_type->scalar_kind != NERI_SCALAR_KIND_INT_V1 ||
      inspected.type->element_size != sizeof(value)) {
    contract_panic("appendInt requires Int[]");
  }
  return append_array(values, &value, false);
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_append_string(neri_ref_v1 values, neri_ref_v1 value) {
  const auto inspected = inspect_array(values);
  static_cast<void>(inspect_string(value));
  if (inspected.type->element_type == nullptr ||
      inspected.type->element_type->kind != NERI_TYPE_KIND_STRING_V1 ||
      inspected.type->element_size != sizeof(value)) {
    contract_panic("appendString requires String[]");
  }
  return append_array(values, &value, true);
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_read_text(neri_ref_v1 path) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return nullptr;
  }
  std::ifstream stream(neri::host_path(native_path), std::ios::binary | std::ios::ate);
  if (!stream) {
    current_state().host_error = "File open failed: " + native_path;
    return nullptr;
  }
  const auto end = stream.tellg();
  constexpr std::streamoff maximum_file_size = 128 * 1024 * 1024;
  if (end < 0 || end > maximum_file_size) {
    current_state().host_error = "File is larger than the 128 MiB host limit.";
    return nullptr;
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  stream.seekg(0);
  if (!bytes.empty() &&
      !stream.read(reinterpret_cast<char *>(bytes.data()), end)) {
    current_state().host_error = "File read failed: " + native_path;
    return nullptr;
  }
  if (!is_strict_utf8(bytes.data(), bytes.size())) {
    current_state().host_error = "File is not valid UTF-8: " + native_path;
    return nullptr;
  }
  current_state().host_error.clear();
  return create_utf8_string(bytes.data(), bytes.size());
}

NERI_RT_API neri_bool_v1 neri_rt_v1_host_write_text(
    neri_ref_v1 path, neri_ref_v1 contents) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return 0;
  }
  const auto text = inspect_string(contents);
  return neri::platform::write_atomic(native_path, text.bytes,
                      static_cast<size_t>(text.byte_length), current_state().host_error)
             ? 1
             : 0;
}

NERI_RT_API neri_bool_v1 neri_rt_v1_host_write_bytes(
    neri_ref_v1 path, neri_ref_v1 contents) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return 0;
  }
  const auto bytes = inspect_array(contents);
  if (bytes.type->element_type == nullptr ||
      bytes.type->element_type->scalar_kind != NERI_SCALAR_KIND_BYTE_V1 ||
      bytes.type->element_size != sizeof(neri_byte_v1)) {
    contract_panic("writeBytes requires Byte[]");
  }
  return neri::platform::write_atomic(native_path, bytes.elements,
                      static_cast<size_t>(bytes.length), current_state().host_error)
             ? 1
             : 0;
}

NERI_RT_API neri_bool_v1
neri_rt_v1_host_remove_file(neri_ref_v1 path) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return 0;
  }
  return neri::platform::remove_file(native_path, current_state().host_error);
}

NERI_RT_API neri_ref_v1 neri_rt_v1_host_path_join(
    neri_ref_v1 left, neri_ref_v1 right) {
  require_initialized();
  std::string left_path;
  std::string right_path;
  if (!host_string(left, left_path, "Path") ||
      !host_string(right, right_path, "Path")) {
    return nullptr;
  }
  const auto joined = neri::path_text(neri::host_path(left_path) / neri::host_path(right_path));
  if (!is_strict_utf8(reinterpret_cast<const uint8_t *>(joined.data()),
                      joined.size())) {
    current_state().host_error = "Joined path is not valid UTF-8.";
    return nullptr;
  }
  current_state().host_error.clear();
  return create_utf8_string(reinterpret_cast<const uint8_t *>(joined.data()),
                            joined.size());
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_path_absolute(neri_ref_v1 path) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return nullptr;
  }
  std::error_code error;
  const auto absolute = std::filesystem::absolute(neri::host_path(native_path), error);
  if (error) {
    current_state().host_error = "Absolute path resolution failed: " + error.message();
    return nullptr;
  }
  const auto normalized = neri::path_text(absolute.lexically_normal());
  current_state().host_error.clear();
  return create_utf8_string(
      reinterpret_cast<const uint8_t *>(normalized.data()), normalized.size());
}

NERI_RT_API neri_int_v1 neri_rt_v1_host_canonical_path(
    const neri_byte_v1 *path, neri_byte_v1 *output, neri_int_v1 capacity) {
  require_initialized();
  if (path == nullptr || capacity < 0 || (capacity > 0 && output == nullptr)) {
    return -1;
  }
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(
      neri::host_path(reinterpret_cast<const char *>(path)), error);
  if (error || canonical.empty()) return -1;
  const auto text = neri::path_text(canonical);
  if (!is_strict_utf8(reinterpret_cast<const uint8_t *>(text.data()),
                      text.size()) ||
      text.size() > static_cast<std::size_t>(INT64_MAX)) {
    return -1;
  }
  const auto length = static_cast<neri_int_v1>(text.size());
  if (capacity <= length) return length;
  std::memcpy(output, text.data(), text.size());
  output[text.size()] = 0;
  return length;
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_path_file_name(neri_ref_v1 path) {
  require_initialized();
  std::string native_path;
  if (!host_string(path, native_path, "Path")) {
    return nullptr;
  }
  const auto name = neri::path_text(neri::host_path(native_path).filename());
  if (!is_strict_utf8(reinterpret_cast<const uint8_t *>(name.data()),
                      name.size())) {
    current_state().host_error = "Path file name is not valid UTF-8.";
    return nullptr;
  }
  current_state().host_error.clear();
  return create_utf8_string(reinterpret_cast<const uint8_t *>(name.data()),
                            name.size());
}

NERI_RT_API neri_int_v1 neri_rt_v1_host_argument_count(void) {
  require_initialized();
  return current_state().process_argument_count;
}

NERI_RT_API neri_int_v1 neri_rt_v1_host_executable_path(
    neri_byte_v1 *output, neri_int_v1 capacity) {
  require_initialized();
  if (capacity < 0 || (capacity > 0 && output == nullptr)) return -1;
  const auto path = neri::platform::executable_path();
  if (!path || !is_strict_utf8(reinterpret_cast<const uint8_t *>(path->data()), path->size())) return -1;
  const auto length = static_cast<neri_int_v1>(path->size());
  if (capacity <= length) return length;
  std::memcpy(output, path->data(), path->size());
  output[path->size()] = 0;
  return length;
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_argument_at(neri_int_v1 index) {
  require_initialized();
  if (index < 0 || index >= current_state().process_argument_count) {
    current_state().host_error.clear();
    return nullptr;
  }
  const auto value = std::string_view(current_state().process_arguments[index]);
  if (!is_strict_utf8(reinterpret_cast<const uint8_t *>(value.data()),
                      value.size())) {
    current_state().host_error = "Process argument is not valid UTF-8.";
    return nullptr;
  }
  current_state().host_error.clear();
  return create_utf8_string(reinterpret_cast<const uint8_t *>(value.data()),
                            value.size());
}

NERI_RT_API neri_ref_v1
neri_rt_v1_host_environment(neri_ref_v1 name) {
  require_initialized();
  std::string native_name;
  if (!host_string(name, native_name, "Environment variable name")) {
    return nullptr;
  }
  const auto value = neri::platform::environment(native_name);
  if (!value) {
    current_state().host_error.clear();
    return nullptr;
  }
  const auto bytes = std::string_view(*value);
  if (!is_strict_utf8(reinterpret_cast<const uint8_t *>(bytes.data()),
                      bytes.size())) {
    current_state().host_error = "Environment variable is not valid UTF-8.";
    return nullptr;
  }
  current_state().host_error.clear();
  return create_utf8_string(reinterpret_cast<const uint8_t *>(bytes.data()),
                            bytes.size());
}

NERI_RT_API void neri_rt_v1_host_run(neri_optional_int_v1 *result,
                                         neri_ref_v1 executable,
                                         neri_ref_v1 arguments) {
  require_initialized();
  if (result == nullptr) {
    contract_panic("run requires a result destination");
  }
  *result = {};
  std::string native_executable;
  if (!host_string(executable, native_executable, "Executable path")) {
    return;
  }
  const auto argument_array = inspect_array(arguments);
  if (argument_array.type->element_type == nullptr ||
      argument_array.type->element_type->kind != NERI_TYPE_KIND_STRING_V1 ||
      argument_array.type->element_size != sizeof(neri_ref_v1) ||
      argument_array.length >
          static_cast<uint64_t>(std::numeric_limits<int>::max() - 2)) {
    contract_panic("run requires String[] arguments");
  }

  std::vector<std::string> storage;
  storage.reserve(static_cast<size_t>(argument_array.length) + 1U);
  storage.push_back(native_executable);
  auto *items = reinterpret_cast<neri_ref_v1 *>(argument_array.elements);
  for (uint64_t index = 0; index < argument_array.length; ++index) {
    std::string argument;
    if (!host_string(items[index], argument, "Process argument")) {
      return;
    }
    storage.push_back(std::move(argument));
  }
  const auto status = neri::platform::run(storage, current_state().host_error);
  if (status) { result->has_value = 1; result->value = *status; }
}

NERI_RT_API void neri_rt_v1_host_exit(neri_int_v1 status) {
  require_initialized();
  if (status < 0 || status > 125) {
    contract_panic("exit status must be between 0 and 125");
  }
  std::exit(static_cast<int>(status));
}

NERI_RT_API neri_ref_v1 neri_rt_v1_host_error_message(void) {
  require_initialized();
  return create_utf8_string(
      reinterpret_cast<const uint8_t *>(current_state().host_error.data()),
      current_state().host_error.size());
}

NERI_RT_API void *neri_rt_v1_gc_borrow_begin(
    neri_ref_v1 owner, uint64_t payload_offset, uint64_t byte_length,
    neri_gc_borrow_v1 *borrow) {
  require_initialized();
  auto *allocation = find_managed(owner);
  const bool empty_token = borrow != nullptr && borrow->runtime_words[0] == 0 &&
                           borrow->runtime_words[1] == 0 &&
                           borrow->runtime_words[2] == 0 &&
                           borrow->runtime_words[3] == 0;
  if (allocation == nullptr || !empty_token ||
      payload_offset > allocation->payload_size ||
      byte_length > allocation->payload_size - payload_offset) {
    contract_panic("invalid managed borrow");
  }
  borrow->runtime_words[0] = reinterpret_cast<uintptr_t>(current_state().borrow);
  borrow->runtime_words[1] = reinterpret_cast<uintptr_t>(owner);
  borrow->runtime_words[2] = byte_length;
  borrow->runtime_words[3] = borrow_cookie;
  current_state().borrow = borrow;
  return reinterpret_cast<uint8_t *>(owner) +
         sizeof(neri_object_header_v1) + payload_offset;
}

NERI_RT_API void neri_rt_v1_gc_borrow_end(neri_gc_borrow_v1 *borrow) {
  require_initialized();
  if (borrow == nullptr || borrow != current_state().borrow ||
      borrow->runtime_words[3] != borrow_cookie) {
    contract_panic("managed borrows must end in LIFO order");
  }
  current_state().borrow =
      reinterpret_cast<neri_gc_borrow_v1 *>(borrow->runtime_words[0]);
  std::memset(borrow, 0, sizeof(*borrow));
}

NERI_RT_API void *neri_rt_v1_native_alloc(uint64_t byte_count,
                                              uint64_t alignment) {
  require_initialized();
  return allocate_native_memory(byte_count, alignment, false);
}

NERI_RT_API void *neri_rt_v1_native_alloc_zeroed(uint64_t byte_count,
                                                     uint64_t alignment) {
  require_initialized();
  return allocate_native_memory(byte_count, alignment, true);
}

NERI_RT_API void *neri_rt_v1_native_realloc(void *pointer,
                                                uint64_t byte_count,
                                                uint64_t alignment) {
  require_initialized();
  if (pointer == nullptr) {
    return allocate_native_memory(byte_count, alignment, false);
  }
  auto *allocation = find_native(pointer);
  if (allocation == nullptr) {
    contract_panic("native realloc requires a runtime-owned allocation");
  }
  const uint64_t old_byte_count = allocation->byte_count;
  void *replacement = allocate_native_memory(byte_count, alignment, false);
  const uint64_t copied_byte_count =
      old_byte_count < byte_count ? old_byte_count : byte_count;
  std::memcpy(replacement, pointer, static_cast<size_t>(copied_byte_count));
  neri_rt_v1_native_free(pointer);
  return replacement;
}

NERI_RT_API void neri_rt_v1_native_free(void *pointer) {
  require_initialized();
  if (pointer == nullptr) {
    return;
  }
  auto *allocation = find_native(pointer);
  if (allocation == nullptr) {
    contract_panic("native free requires a runtime-owned allocation");
  }
  unlink_native(allocation);
  current_state().native_byte_count -= allocation->byte_count;
  neri::platform::deallocate(allocation->pointer);
  std::free(allocation);
  assert_consistent();
}

NERI_RT_API NERI_RT_NORETURN void
neri_rt_v1_panic(const neri_panic_v1 *panic) {
  report_panic(panic, nullptr);
}

NERI_RT_API NERI_RT_NORETURN void
neri_rt_v1_panic_at(const neri_panic_v1 *panic,
                      const neri_source_location_v1 *location) {
  report_panic(panic, location);
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_create(void) {
  require_initialized();
  auto value = std::make_shared<session_coordinator>();
  value->nonce = next_session_nonce.fetch_add(1, std::memory_order_relaxed);
  value->owner_thread = std::this_thread::get_id();
  const auto handle = static_cast<int64_t>(value->nonce);
  std::lock_guard lock(session_mutex);
  if (sessions.size() >= 128) return 0;
  sessions.emplace(handle, value);
  try {
    current_state().persistent_roots.push_back(&value->state);
  } catch (...) {
    sessions.erase(handle);
    throw;
  }
  return handle;
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_identity(void) {
  require_initialized();
  return static_cast<neri_int_v1>(
      next_session_identity.fetch_add(1, std::memory_order_relaxed));
}

NERI_RT_API neri_ref_v1 neri_rt_v1_session_owner(neri_int_v1 handle) {
  require_initialized();
  std::shared_ptr<session_coordinator> session;
  {
    std::lock_guard lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return create_ascii_string("");
    session = found->second;
  }
  std::lock_guard lock(session->mutex);
  return create_ascii_string("native-session-" + std::to_string(session->nonce));
}

[[nodiscard]] bool session_load_object_linker(session_coordinator &session,
                                              const std::string &path) {
  if (session.object_linker != nullptr) return true;
  auto *library = neri::platform::session_module_open(path, session.error);
  if (library == nullptr) return false;
  auto assign = [library](auto &function, const char *name) {
    auto *symbol = neri::platform::session_module_symbol(library, name);
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
  };
  assign(session.object_create, "neri_session_linker_create_v1");
  assign(session.object_destroy, "neri_session_linker_destroy_v1");
  assign(session.object_add, "neri_session_linker_add_object_v1");
  assign(session.object_symbol, "neri_session_linker_symbol_v1");
  assign(session.object_remove, "neri_session_linker_remove_v1");
  if (session.object_create == nullptr || session.object_destroy == nullptr ||
      session.object_add == nullptr || session.object_symbol == nullptr ||
      session.object_remove == nullptr) {
    static_cast<void>(neri::platform::session_module_close(library, session.error));
    session.error = "session object linker exports are missing";
    return false;
  }
  std::array<char, 512> error{};
  session.object_linker = session.object_create(error.data(), error.size());
  if (session.object_linker == nullptr) {
    static_cast<void>(neri::platform::session_module_close(library, session.error));
    session.error = error.data();
    return false;
  }
  session.object_linker_library = library;
  return true;
}

static neri_int_v1 session_load_execute_impl(
    neri_int_v1 handle, neri_ref_v1 module_path, const char *accessor_name,
    const char *entry_name) {
  require_initialized();
  std::string path;
  if (!host_string(module_path, path, "session module path"))
    return NERI_SESSION_LOAD_FAILED_V1;
  std::shared_ptr<session_coordinator> pinned;
  {
    std::lock_guard registry_lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return NERI_SESSION_INVALID_HANDLE_V1;
    pinned = found->second;
  }
  std::lock_guard coordinator_lock(pinned->mutex);
  auto &session = *pinned;
  if (session.owner_thread != std::this_thread::get_id() || session.busy)
    return NERI_SESSION_INVOKE_STATE_V1;
  if (session.reset_failed) {
    session.error = "session reset must be retried before execution";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  if (session.modules.size() >= 256 || session.layouts.size() >= 8192) {
    session.error = "session resource limit reached";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  const auto metrics_directory = std::getenv("NERI_SESSION_METRICS");
  const bool measure = metrics_directory != nullptr && metrics_directory[0] != '\0';
  const auto open_started = std::chrono::steady_clock::now();
  session.error.clear();
  auto *module = neri::platform::session_module_open(path, session.error);
  if (module == nullptr) return NERI_SESSION_LOAD_FAILED_V1;
  const auto validation_started = std::chrono::steady_clock::now();
  const neri_session_module_metadata_v1 *metadata = nullptr;
  int64_t validation = NERI_SESSION_INVALID_METADATA_V1;
  try {
    validation = session_validate(
        session, neri::platform::session_module_symbol(module, accessor_name),
        neri::platform::session_module_symbol(module, entry_name), entry_name,
        metadata);
  } catch (...) {
    static_cast<void>(neri::platform::session_module_close(module, session.error));
    session.error = "session metadata allocation failed";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  if (validation != NERI_SESSION_OK_V1) {
    static_cast<void>(neri::platform::session_module_close(module, session.error));
    return validation;
  }
  const auto metadata_started = std::chrono::steady_clock::now();
  std::map<std::string, session_layout_record> merged;
  std::string target;
  try {
    merged = session.layouts;
    target = metadata->target_type_id;
    for (uint64_t index = 0; index < metadata->layout_count; ++index) {
      const auto &layout = metadata->layouts[index];
      session_layout_record record{layout.canonical_layout, layout.kind, layout.flags,
                                   layout.payload_size, layout.payload_alignment, {}};
      if (layout.trace_offset_count != 0)
        record.trace_offsets.assign(layout.trace_offsets,
                                    layout.trace_offsets + layout.trace_offset_count);
      merged.insert_or_assign(layout.type_id, std::move(record));
    }
    if (merged.size() > 8192) throw std::length_error("session layout limit");
    session.modules.reserve(session.modules.size() + 1);
    session.modules.push_back(module);
  } catch (...) {
    static_cast<void>(neri::platform::session_module_close(module, session.error));
    session.error = "session metadata exceeds resource limits";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  const auto entry_started = std::chrono::steady_clock::now();
  session.busy = true;
  session.state = metadata->entry(session.state);
  session.busy = false;
  const auto gc_started = std::chrono::steady_clock::now();
  session.target_type.swap(target);
  session.display_offset = metadata->version_minor >= 1 &&
                                   metadata->struct_size >= sizeof(*metadata)
                               ? metadata->display_offset
                               : UINT64_MAX;
  session.layouts.swap(merged);
  neri_rt_v1_gc_collect();
  const auto finished = std::chrono::steady_clock::now();
  if (measure) {
    const auto nanoseconds = [](auto duration) {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    };
    const auto metric_id = next_session_metric.fetch_add(1, std::memory_order_relaxed);
    std::ofstream output(std::filesystem::path(metrics_directory) /
                         (std::to_string(metric_id) + ".runtime.json"));
    output << "{\"schemaVersion\":1,\"ownerIdentity\":\"native-session-"
           << session.nonce << "\",\"generation\":" << session.modules.size()
           << ",\"moduleOpenNs\":"
           << nanoseconds(validation_started - open_started)
           << ",\"validationNs\":"
           << nanoseconds(metadata_started - validation_started)
           << ",\"metadataCommitNs\":"
           << nanoseconds(entry_started - metadata_started)
           << ",\"entryNs\":" << nanoseconds(gc_started - entry_started)
           << ",\"gcNs\":" << nanoseconds(finished - gc_started) << "}\n";
  }
  return NERI_SESSION_OK_V1;
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute(
    neri_int_v1 handle, neri_ref_v1 module_path) {
  return session_load_execute_impl(handle, module_path,
                                   "neri_session_module_v1",
                                   "neri_session_entry_v1");
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute_retained(
    neri_int_v1 handle, neri_ref_v1 module_path,
    neri_ref_v1 artifact_identity) {
  std::string identity;
  if (!host_string(artifact_identity, identity, "session artifact identity"))
    return NERI_SESSION_LOAD_FAILED_V1;
  static constexpr char digits[] = "0123456789abcdef";
  std::string suffix;
  suffix.reserve(identity.size() * 2U);
  for (const auto byte : identity) {
    const auto value = static_cast<unsigned char>(byte);
    suffix.push_back(digits[value >> 4U]);
    suffix.push_back(digits[value & 15U]);
  }
  const auto accessor = "neri_session_module_v2_" + suffix;
  const auto entry = "neri_session_entry_v2_" + suffix;
  return session_load_execute_impl(handle, module_path, accessor.c_str(),
                                   entry.c_str());
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_load_execute_object(
    neri_int_v1 handle, neri_ref_v1 object_path,
    neri_ref_v1 artifact_identity, neri_ref_v1 linker_path) {
  require_initialized();
  std::string path;
  std::string identity;
  std::string bridge;
  if (!host_string(object_path, path, "session object path") ||
      !host_string(artifact_identity, identity, "session artifact identity") ||
      !host_string(linker_path, bridge, "session object linker path"))
    return NERI_SESSION_LOAD_FAILED_V1;
  std::shared_ptr<session_coordinator> pinned;
  {
    std::lock_guard registry_lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return NERI_SESSION_INVALID_HANDLE_V1;
    pinned = found->second;
  }
  std::lock_guard coordinator_lock(pinned->mutex);
  auto &session = *pinned;
  if (session.owner_thread != std::this_thread::get_id() || session.busy ||
      session.reset_failed)
    return NERI_SESSION_INVOKE_STATE_V1;
  if (session.object_generations.size() >= 256 || session.layouts.size() >= 8192) {
    session.error = "session resource limit reached";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  if (!session_load_object_linker(session, bridge))
    return NERI_SESSION_LOAD_FAILED_V1;
  static constexpr char digits[] = "0123456789abcdef";
  std::string suffix;
  suffix.reserve(identity.size() * 2U);
  for (const auto byte : identity) {
    const auto value = static_cast<unsigned char>(byte);
    suffix.push_back(digits[value >> 4U]);
    suffix.push_back(digits[value & 15U]);
  }
  const auto accessor_name = "neri_session_module_v2_" + suffix;
  const auto entry_name = "neri_session_entry_v2_" + suffix;
  std::array<char, 512> error{};
  auto *generation = session.object_add(
      session.object_linker, identity.c_str(), path.c_str(),
      session.object_generations.data(), session.object_generations.size(),
      error.data(), error.size());
  if (generation == nullptr) {
    session.error = error.data();
    return NERI_SESSION_LOAD_FAILED_V1;
  }
  auto *accessor = session.object_symbol(generation, accessor_name.c_str(),
                                         error.data(), error.size());
  auto *entry = session.object_symbol(generation, entry_name.c_str(), error.data(),
                                      error.size());
  if (accessor == nullptr || entry == nullptr) {
    static_cast<void>(session.object_remove(generation, error.data(), error.size()));
    session.error = error.data();
    return NERI_SESSION_INVALID_METADATA_V1;
  }
  const neri_session_module_metadata_v1 *metadata = nullptr;
  const auto validation = session_validate(session, accessor, entry,
                                           entry_name.c_str(), metadata);
  if (validation != NERI_SESSION_OK_V1) {
    static_cast<void>(session.object_remove(generation, error.data(), error.size()));
    return validation;
  }
  std::map<std::string, session_layout_record> merged;
  std::string target;
  try {
    merged = session.layouts;
    target = metadata->target_type_id;
    for (uint64_t index = 0; index < metadata->layout_count; ++index) {
      const auto &layout = metadata->layouts[index];
      session_layout_record record{layout.canonical_layout, layout.kind,
                                   layout.flags, layout.payload_size,
                                   layout.payload_alignment, {}};
      if (layout.trace_offset_count != 0)
        record.trace_offsets.assign(layout.trace_offsets,
                                    layout.trace_offsets + layout.trace_offset_count);
      merged.insert_or_assign(layout.type_id, std::move(record));
    }
    if (merged.size() > 8192) throw std::length_error("session layout limit");
    session.object_generations.push_back(generation);
  } catch (...) {
    static_cast<void>(session.object_remove(generation, error.data(), error.size()));
    session.error = "session metadata exceeds resource limits";
    return NERI_SESSION_INVOKE_STATE_V1;
  }
  session.busy = true;
  session.state = metadata->entry(session.state);
  session.busy = false;
  session.target_type.swap(target);
  session.display_offset = metadata->display_offset;
  session.layouts.swap(merged);
  neri_rt_v1_gc_collect();
  return NERI_SESSION_OK_V1;
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_reset(neri_int_v1 handle) {
  require_initialized();
  std::shared_ptr<session_coordinator> pinned;
  {
    std::lock_guard registry_lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return NERI_SESSION_INVALID_HANDLE_V1;
    pinned = found->second;
  }
  std::lock_guard coordinator_lock(pinned->mutex);
  auto &session = *pinned;
  if (session.owner_thread != std::this_thread::get_id() || session.busy)
    return NERI_SESSION_INVOKE_STATE_V1;
  session.state = nullptr;
  session.display_offset = UINT64_MAX;
  neri_rt_v1_gc_collect();
  // Once the root is cleared there is no preceding frame, even if unloading a
  // module fails. Keep only failed handles for a reset retry and make execution
  // explicitly unavailable until that retry succeeds.
  session.layouts.clear();
  session.target_type.clear();
  if (session.object_remove != nullptr) {
    std::array<char, 512> error{};
    while (!session.object_generations.empty()) {
      auto *generation = session.object_generations.back();
      if (session.object_remove(generation, error.data(), error.size()) == 0) {
        session.error = error.data();
        session.reset_failed = true;
        return NERI_SESSION_LOAD_FAILED_V1;
      }
      session.object_generations.pop_back();
    }
  }
  if (session.object_linker != nullptr) {
    session.object_destroy(session.object_linker);
    session.object_linker = nullptr;
  }
  if (session.object_linker_library != nullptr) {
    if (!neri::platform::session_module_close(session.object_linker_library,
                                              session.error)) {
      session.reset_failed = true;
      return NERI_SESSION_LOAD_FAILED_V1;
    }
    session.object_linker_library = nullptr;
  }
  std::vector<void *> remaining;
  remaining.reserve(session.modules.size());
  for (auto module = session.modules.rbegin(); module != session.modules.rend(); ++module) {
    if (!neri::platform::session_module_close(*module, session.error))
      remaining.push_back(*module);
  }
  if (!remaining.empty()) {
    std::reverse(remaining.begin(), remaining.end());
    session.modules.swap(remaining);
    session.reset_failed = true;
    return NERI_SESSION_LOAD_FAILED_V1;
  }
  session.modules.clear();
  session.reset_failed = false;
  return NERI_SESSION_OK_V1;
}

NERI_RT_API neri_int_v1 neri_rt_v1_session_destroy(neri_int_v1 handle) {
  const auto reset = neri_rt_v1_session_reset(handle);
  if (reset != NERI_SESSION_OK_V1) return reset;
  std::lock_guard lock(session_mutex);
  const auto found = sessions.find(handle);
  if (found == sessions.end()) return NERI_SESSION_INVALID_HANDLE_V1;
  auto *slot = &found->second->state;
  auto &roots = current_state().persistent_roots;
  roots.erase(std::remove(roots.begin(), roots.end(), slot), roots.end());
  sessions.erase(found);
  return NERI_SESSION_OK_V1;
}

NERI_RT_API neri_ref_v1 neri_rt_v1_session_error(neri_int_v1 handle) {
  require_initialized();
  std::shared_ptr<session_coordinator> session;
  {
    std::lock_guard lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return create_ascii_string("invalid session handle");
    session = found->second;
  }
  std::lock_guard lock(session->mutex);
  return create_ascii_string(session->error);
}

NERI_RT_API neri_ref_v1 neri_rt_v1_session_result(neri_int_v1 handle) {
  require_initialized();
  std::shared_ptr<session_coordinator> session;
  {
    std::lock_guard lock(session_mutex);
    const auto found = sessions.find(handle);
    if (found == sessions.end()) return create_ascii_string("");
    session = found->second;
  }
  std::lock_guard lock(session->mutex);
  if (session->state == nullptr) return create_ascii_string("");
  if (session->display_offset == UINT64_MAX)
    return create_ascii_string("");
  neri_ref_v1 result{};
  std::memcpy(&result,
              reinterpret_cast<const std::byte *>(session->state) +
                  session->display_offset,
              sizeof(result));
  if (result == nullptr) return create_ascii_string("");
  std::string bytes;
  if (!host_string(result, bytes, "session result"))
    return create_ascii_string("");
  return create_utf8_string(reinterpret_cast<const uint8_t *>(bytes.data()),
                            bytes.size());
}
}
