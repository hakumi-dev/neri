#include "neri/runtime_abi.h"

#include <cstring>

namespace {
[[noreturn]] void assertion_panic(const char *message) {
  const neri_panic_v1 panic = {
      NERI_PANIC_RUNTIME_CONTRACT_V1,
      0,
      reinterpret_cast<const uint8_t *>(message),
      std::strlen(message),
  };
  neri_rt_v1_panic(&panic);
}
} // namespace

extern "C" {

NERI_RT_API void neri_rt_v1_test_assert(neri_bool_v1 condition) {
  if (condition != 1) {
    assertion_panic("Assertion failed.");
  }
}

NERI_RT_API void neri_rt_v1_test_assert_true(neri_bool_v1 value) {
  if (value != 1) {
    assertion_panic("Assertion failed: expected true.");
  }
}

NERI_RT_API void neri_rt_v1_test_assert_false(neri_bool_v1 value) {
  if (value != 0) {
    assertion_panic("Assertion failed: expected false.");
  }
}

} // extern "C"
