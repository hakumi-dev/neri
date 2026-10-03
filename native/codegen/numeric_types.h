#ifndef NERI_CODEGEN_NUMERIC_TYPES_H
#define NERI_CODEGEN_NUMERIC_TYPES_H
#include "neri/ir_transport.h"
#include "neri/codegen/scalar_metadata.h"

namespace neri::codegen {
constexpr bool extended_scalar(neri_ir_type_tag_v1 tag) {
  return scalar_properties(tag).extended;
}
constexpr bool floating_scalar(neri_ir_type_tag_v1 tag) {
  return scalar_properties(tag).floating;
}
constexpr bool integer_scalar(neri_ir_type_tag_v1 tag) {
  return scalar_properties(tag).integer_representation;
}
constexpr bool unsigned_scalar(neri_ir_type_tag_v1 tag) {
  return scalar_properties(tag).is_unsigned;
}
constexpr unsigned scalar_width(neri_ir_type_tag_v1 tag) {
  const auto scalar = scalar_properties(tag);
  return scalar.integer_representation || scalar.floating ? scalar.semantic_bits : 64U;
}
}
#endif
