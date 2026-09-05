#include "neri/runtime_abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {
#define NERI_STRING_CHECK(condition)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "String check failed: %s (%s:%d)\n", #condition,   \
                   __FILE__, __LINE__);                                        \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

template <std::size_t Size> struct literal final {
  neri_string_prefix_v1 prefix;
  std::array<std::uint8_t, Size + 1U> bytes;
};

const literal<2> embedded_left = {
    {{&neri_rt_v1_string_literal_type, 0}, 2}, {'a', 0, 0}};
const literal<1> suffix = {
    {{&neri_rt_v1_string_literal_type, 0}, 1}, {'b', 0}};
const literal<3> embedded_joined = {
    {{&neri_rt_v1_string_literal_type, 0}, 3}, {'a', 0, 'b', 0}};
const literal<3> embedded_other = {
    {{&neri_rt_v1_string_literal_type, 0}, 3}, {'a', 0, 'c', 0}};
template <std::size_t Size>
[[nodiscard]] neri_ref_v1 ref(const literal<Size> &value) {
  return const_cast<neri_ref_v1>(
      reinterpret_cast<const neri_object_header_v1 *>(&value));
}

[[nodiscard]] neri_gc_stats_v1 stats() {
  neri_gc_stats_v1 result{sizeof(neri_gc_stats_v1), 0, 0, 0, 0, 0};
  neri_rt_v1_gc_get_stats(&result);
  return result;
}

[[nodiscard]] std::string_view bytes(neri_ref_v1 value) {
  const auto *prefix = reinterpret_cast<const neri_string_prefix_v1 *>(value);
  const auto *data = reinterpret_cast<const char *>(value) +
                     sizeof(neri_string_prefix_v1);
  return {data, static_cast<std::size_t>(prefix->byte_length)};
}

void check_text(neri_ref_v1 value, std::string_view expected) {
  NERI_STRING_CHECK(bytes(value) == expected);
  const auto *terminator = reinterpret_cast<const std::uint8_t *>(value) +
                           sizeof(neri_string_prefix_v1) + expected.size();
  NERI_STRING_CHECK(*terminator == 0);
}
} // namespace

int main() {
  const neri_runtime_abi_requirements_v1 requirements = {
      sizeof(neri_runtime_abi_requirements_v1),
      NERI_RUNTIME_ABI_MAJOR,
      NERI_RUNTIME_ABI_MINOR,
      NERI_RT_FEATURE_NATIVE_STRINGS | NERI_RT_FEATURE_ROOT_FRAMES |
          NERI_RT_FEATURE_PRECISE_GC,
  };
  NERI_STRING_CHECK(neri_rt_v1_initialize(&requirements) ==
                      NERI_ABI_STATUS_OK_V1);
  NERI_STRING_CHECK(neri_rt_v1_string_equal(ref(embedded_joined),
                                                ref(embedded_other)) == 0);

  neri_ref_v1 roots[1] = {nullptr};
  neri_gc_root_frame_v1 frame = {nullptr, roots, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);

  roots[0] =
      neri_rt_v1_string_concat(ref(embedded_left), ref(suffix));
  neri_rt_v1_gc_collect();
  NERI_STRING_CHECK(neri_rt_v1_string_equal(roots[0], ref(embedded_joined)) == 1);

  roots[0] = neri_rt_v1_string_from_int(-9223372036854775807LL - 1LL);
  check_text(roots[0], "-9223372036854775808");
  roots[0] = neri_rt_v1_string_from_byte(255);
  check_text(roots[0], "255");
  roots[0] = neri_rt_v1_string_from_float(12.5);
  check_text(roots[0], "12.5");
  roots[0] = neri_rt_v1_string_from_float(10000000000.0);
  check_text(roots[0], "1e10");

  roots[0] = nullptr;
  neri_rt_v1_gc_collect();
  NERI_STRING_CHECK(stats().managed_object_count == 0);
  neri_rt_v1_gc_root_frame_leave(&frame);

  neri_rt_v1_shutdown();
  return EXIT_SUCCESS;
}
