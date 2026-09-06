/* Independent C ABI oracle; this library is not part of the Neri runtime. */
#include <stdint.h>
double neri_ffi_double(double value) { return value * 2.0; }
double neri_ffi_half(double value) { return value / 2.0; }
int32_t neri_ffi_i32(int32_t value) { return value; }
uint32_t neri_ffi_u32(uint32_t value) { return value; }
uint64_t neri_ffi_u64(uint64_t value) { return UINT64_MAX - value; }
float neri_ffi_f32(float value) { return value + 0.5f; }
void neri_ffi_scalars(int32_t *signed_value, uint32_t *unsigned_value, float *real_value) {
  signed_value[1] = INT32_MIN;
  unsigned_value[1] = UINT32_MAX;
  real_value[1] = 1.25f;
}
