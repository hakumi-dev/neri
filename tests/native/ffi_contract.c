/* Independent C ABI oracle; this library is not part of the Neri runtime. */
#include <stdint.h>
#include <stddef.h>
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

struct neri_ffi_record { uint8_t tag; float real; uint64_t wide; int32_t coordinates[3]; };
struct neri_ffi_nested { uint8_t tag; struct neri_ffi_record value; void *next; };
union neri_ffi_event { uint32_t type; struct neri_ffi_record value; uint8_t padding[128]; };
struct neri_ffi_node { uint32_t tag; struct neri_ffi_node *next; };
int64_t neri_ffi_layout(int64_t query) {
  switch (query) {
    case 0: return sizeof(struct neri_ffi_record);
    case 1: return _Alignof(struct neri_ffi_record);
    case 2: return offsetof(struct neri_ffi_record, real);
    case 3: return offsetof(struct neri_ffi_record, coordinates);
    case 4: return sizeof(struct neri_ffi_nested);
    case 5: return offsetof(struct neri_ffi_nested, value);
    case 6: return offsetof(struct neri_ffi_nested, next);
    case 7: return sizeof(union neri_ffi_event);
    case 8: return _Alignof(union neri_ffi_event);
    case 9: return offsetof(union neri_ffi_event, value);
    case 10: return sizeof(struct neri_ffi_node);
    case 11: return sizeof(float[2][3]);
    default: return -1;
  }
}
