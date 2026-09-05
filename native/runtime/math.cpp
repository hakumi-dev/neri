#include "neri/runtime_abi.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {
[[noreturn]] void arithmetic_panic(const char *message) {
  const neri_panic_v1 panic = {
      NERI_PANIC_ARITHMETIC_V1,
      0,
      reinterpret_cast<const uint8_t *>(message),
      std::strlen(message),
  };
  neri_rt_v1_panic(&panic);
}
} // namespace

extern "C" {

NERI_RT_API neri_int_v1 neri_rt_v1_math_abs(neri_int_v1 value) {
  if (value == std::numeric_limits<neri_int_v1>::min()) {
    arithmetic_panic("Integer absolute value overflow.");
  }
  return value < 0 ? -value : value;
}

NERI_RT_API neri_int_v1 neri_rt_v1_math_max(neri_int_v1 left,
                                                  neri_int_v1 right) {
  return left > right ? left : right;
}

NERI_RT_API neri_int_v1 neri_rt_v1_math_min(neri_int_v1 left,
                                                  neri_int_v1 right) {
  return left < right ? left : right;
}

NERI_RT_API neri_int_v1 neri_rt_v1_math_pow(neri_int_v1 base,
                                                  neri_int_v1 exponent) {
  const auto result = std::pow(static_cast<double>(base),
                               static_cast<double>(exponent));
  constexpr auto minimum = -9223372036854775808.0;
  constexpr auto upper_bound = 9223372036854775808.0;
  if (!std::isfinite(result) || result < minimum || result >= upper_bound) {
    arithmetic_panic("Integer power result is outside the Int range.");
  }
  return static_cast<neri_int_v1>(result);
}

} // extern "C"
