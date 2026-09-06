#ifndef NERI_CODEGEN_NUMERIC_TYPES_H
#define NERI_CODEGEN_NUMERIC_TYPES_H
#include "neri/ir_transport.h"

namespace neri::codegen {
constexpr bool extended_scalar(unsigned tag) {
  return tag >= NERI_IR_TYPE_INT32_V1 && tag <= NERI_IR_TYPE_FLOAT32_V1;
}
constexpr bool floating_scalar(unsigned tag) {
  return tag == NERI_IR_TYPE_FLOAT_V1 || tag == NERI_IR_TYPE_FLOAT32_V1;
}
constexpr bool integer_scalar(unsigned tag) {
  return tag == NERI_IR_TYPE_INT_V1 || tag == NERI_IR_TYPE_INT32_V1 ||
         tag == NERI_IR_TYPE_UINT32_V1 || tag == NERI_IR_TYPE_UINT64_V1 || tag == NERI_IR_TYPE_BYTE_V1;
}
constexpr bool unsigned_scalar(unsigned tag) {
  return tag == NERI_IR_TYPE_BYTE_V1 || tag == NERI_IR_TYPE_UINT32_V1 || tag == NERI_IR_TYPE_UINT64_V1;
}
constexpr unsigned scalar_width(unsigned tag) {
  return tag == NERI_IR_TYPE_BYTE_V1 ? 8U :
         (tag == NERI_IR_TYPE_INT32_V1 || tag == NERI_IR_TYPE_UINT32_V1 || tag == NERI_IR_TYPE_FLOAT32_V1) ? 32U : 64U;
}
}
#endif
