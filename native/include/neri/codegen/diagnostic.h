#ifndef NERI_CODEGEN_DIAGNOSTIC_H
#define NERI_CODEGEN_DIAGNOSTIC_H

#include <exception>
#include <string_view>

namespace neri::codegen {

enum class reader_error_kind {
  incompatible_major,
  unsupported_minor,
  unsupported_feature,
  malformed_module,
  invalid_reference,
  invalid_type,
  invalid_control_flow,
  invalid_ssa,
  invalid_safety,
  invalid_source,
};

enum class codegen_error_kind {
  driver,
  lowering,
  verification,
  emission,
  output,
};

[[nodiscard]] constexpr std::string_view
diagnostic_code(reader_error_kind kind) noexcept {
  switch (kind) {
  case reader_error_kind::incompatible_major:
    return "NIR001";
  case reader_error_kind::unsupported_minor:
    return "NIR002";
  case reader_error_kind::unsupported_feature:
    return "NIR003";
  case reader_error_kind::malformed_module:
    return "NIR004";
  case reader_error_kind::invalid_reference:
    return "NIR005";
  case reader_error_kind::invalid_type:
    return "NIR006";
  case reader_error_kind::invalid_control_flow:
    return "NIR007";
  case reader_error_kind::invalid_ssa:
    return "NIR008";
  case reader_error_kind::invalid_safety:
    return "NIR009";
  case reader_error_kind::invalid_source:
    return "NIR010";
  }
  std::terminate();
}

[[nodiscard]] constexpr std::string_view
diagnostic_code(codegen_error_kind kind) noexcept {
  switch (kind) {
  case codegen_error_kind::driver:
    return "NCG001";
  case codegen_error_kind::lowering:
    return "NCG002";
  case codegen_error_kind::verification:
    return "NCG003";
  case codegen_error_kind::emission:
    return "NCG004";
  case codegen_error_kind::output:
    return "NCG005";
  }
  std::terminate();
}

} // namespace neri::codegen

#endif
