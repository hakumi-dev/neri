#include "ir_verifier.h"

#include "neri/codegen/reader.h"
#include "neri/ir_transport.h"
#include "numeric_types.h"
#include "native_layout.h"

#include <llvm/Support/ConvertUTF.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace neri::codegen {
namespace {

constexpr std::string_view incompatible_major = "NIR001";
constexpr std::string_view unsupported_minor = "NIR002";
constexpr std::string_view unsupported_feature = "NIR003";
constexpr std::string_view malformed_module = "NIR004";
constexpr std::string_view invalid_reference = "NIR005";
constexpr std::string_view invalid_type = "NIR006";
constexpr std::string_view invalid_control_flow = "NIR007";
constexpr std::string_view invalid_ssa = "NIR008";
constexpr std::string_view invalid_safety = "NIR009";
constexpr std::string_view invalid_source = "NIR010";

[[noreturn]] void fail(std::string_view code, std::string message) {
  throw reader_error(std::string(code), std::move(message), 0U);
}

[[nodiscard]] auto symbol_key(const symbol_id &value) {
  return std::tuple(value.module, value.kind, value.semantic_name);
}

[[nodiscard]] bool same_symbol(const symbol_id &left, const symbol_id &right) {
  return symbol_key(left) == symbol_key(right);
}

[[nodiscard]] bool same_type(const type &left, const type &right) {
  if (left.tag != right.tag || left.element_count != right.element_count || left.symbol.has_value() != right.symbol.has_value() ||
      left.arguments.size() != right.arguments.size()) {
    return false;
  }
  if (left.symbol.has_value() && !same_symbol(*left.symbol, *right.symbol)) {
    return false;
  }
  for (std::size_t index = 0; index < left.arguments.size(); ++index) {
    if (!same_type(left.arguments[index], right.arguments[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool same_runtime_requirement(
    const std::optional<runtime_requirement> &left,
    const std::optional<runtime_requirement> &right) {
  return left.has_value() == right.has_value() &&
         (!left.has_value() ||
          (left->abi_major == right->abi_major &&
           left->abi_minor == right->abi_minor &&
           left->feature_bits == right->feature_bits));
}

[[nodiscard]] bool strict_utf8(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return true;
  }
  const auto *cursor = reinterpret_cast<const llvm::UTF8 *>(bytes.data());
  const auto *end = cursor + bytes.size();
  return llvm::isLegalUTF8String(&cursor, end) != 0;
}

[[nodiscard]] bool utf8_boundary(std::span<const std::uint8_t> bytes,
                                 std::size_t index) {
  return index == 0U || index == bytes.size() ||
         (bytes[index] & UINT8_C(0xc0)) != UINT8_C(0x80);
}

[[nodiscard]] bool is_void(const type &value) {
  return value.tag == NERI_IR_TYPE_VOID_V1 && !value.symbol.has_value() &&
         value.arguments.empty();
}

[[nodiscard]] bool is_scalar(const type &value) {
  return ((value.tag >= NERI_IR_TYPE_BOOL_V1 &&
         value.tag <= NERI_IR_TYPE_FLOAT_V1) || extended_scalar(value.tag)) && !value.symbol.has_value() &&
         value.arguments.empty();
}

[[nodiscard]] bool is_string(const type &value) {
  return value.tag == NERI_IR_TYPE_STRING_V1 && !value.symbol.has_value() &&
         value.arguments.empty();
}

[[nodiscard]] bool is_class(const type &value) {
  return value.tag == NERI_IR_TYPE_CLASS_V1 && value.symbol.has_value() &&
         value.arguments.empty();
}

[[nodiscard]] bool is_pointer(const type &value) {
  return value.tag == NERI_IR_TYPE_POINTER_V1 && !value.symbol.has_value() &&
         value.arguments.size() == 1U;
}

[[nodiscard]] bool is_nullable_pointer(const type &value) {
  return value.tag == NERI_IR_TYPE_OPTIONAL_V1 && !value.symbol.has_value() &&
         value.arguments.size() == 1U && is_pointer(value.arguments.front());
}

[[nodiscard]] bool is_c_abi_type(const type &value, bool allow_void) {
  return is_scalar(value) || is_pointer(value) || is_nullable_pointer(value) ||
         (allow_void && is_void(value));
}

[[nodiscard]] bool is_unsafe_capability(const type &value) {
  return value.tag == NERI_IR_TYPE_UNSAFE_CAPABILITY_V1 &&
         !value.symbol.has_value() && value.arguments.empty();
}

[[nodiscard]] bool is_borrow_capability(const type &value) {
  return value.tag == NERI_IR_TYPE_BORROW_CAPABILITY_V1 &&
         !value.symbol.has_value() && value.arguments.size() == 1U;
}

[[nodiscard]] bool is_pointer_element(const type &value) {
  return is_void(value) || is_scalar(value) ||
         (value.tag == NERI_IR_TYPE_NATIVE_RECORD_V1 && value.symbol && value.arguments.empty()) ||
         (value.tag == NERI_IR_TYPE_FIXED_ARRAY_V1 && !value.symbol && value.arguments.size() == 1U && value.element_count > 0U && is_pointer_element(value.arguments.front())) ||
         is_nullable_pointer(value) ||
         (is_pointer(value) && is_pointer_element(value.arguments.front()));
}

[[nodiscard]] bool is_managed_reference(const type &value) {
  return is_string(value) || is_class(value) ||
         value.tag == NERI_IR_TYPE_ARRAY_V1 ||
         (value.tag == NERI_IR_TYPE_OPTIONAL_V1 &&
          value.arguments.size() == 1U &&
          is_managed_reference(value.arguments.front()));
}

[[nodiscard]] bool is_supported_value(const type &value) {
  return is_scalar(value) || is_string(value) || is_class(value) ||
         (value.tag == NERI_IR_TYPE_NATIVE_RECORD_V1 && is_pointer_element(value)) ||
         (value.tag == NERI_IR_TYPE_FIXED_ARRAY_V1 && is_pointer_element(value)) ||
         (is_pointer(value) && is_pointer_element(value.arguments.front())) ||
         is_unsafe_capability(value) ||
         (is_borrow_capability(value) &&
          !is_void(value.arguments.front()) &&
          is_pointer_element(value.arguments.front())) ||
         (value.tag == NERI_IR_TYPE_ARRAY_V1 &&
          !value.symbol.has_value() && value.arguments.size() == 1U &&
          !is_void(value.arguments.front()) &&
          (is_scalar(value.arguments.front()) ||
           is_managed_reference(value.arguments.front())) &&
          is_supported_value(value.arguments.front())) ||
         (value.tag == NERI_IR_TYPE_OPTIONAL_V1 &&
          !value.symbol.has_value() && value.arguments.size() == 1U &&
          !is_void(value.arguments.front()) &&
          value.arguments.front().tag != NERI_IR_TYPE_OPTIONAL_V1 &&
          value.arguments.front().tag != NERI_IR_TYPE_NATIVE_RECORD_V1 &&
          value.arguments.front().tag != NERI_IR_TYPE_FIXED_ARRAY_V1 &&
          is_supported_value(value.arguments.front()));
}

[[nodiscard]] bool contains_string(const type &value) {
  return is_string(value) ||
         std::ranges::any_of(value.arguments, contains_string);
}

void require_result_type(const type &value, std::string_view description) {
  if (!is_void(value) && !is_supported_value(value)) {
    fail(unsupported_feature,
         std::string(description) +
             " uses a semantic type whose native lowering belongs to a later "
             "roadmap card.");
  }
}

void require_value_type(const type &value, std::string_view description) {
  if (!is_supported_value(value)) {
    fail(unsupported_feature,
         std::string(description) +
             " uses a semantic type whose native lowering belongs to a later "
             "roadmap card.");
  }
}

void verify_location(const std::optional<source_location> &location,
                     const ir_module &module) {
  if (!location.has_value()) {
    return;
  }
  const auto source = std::ranges::find_if(
      module.sources, [&location](const auto &candidate) {
        return candidate.id == location->source;
      });
  if (source == module.sources.end()) {
    fail(invalid_source,
         "Source location references missing source '" + location->source +
             "'.");
  }
  const auto start = static_cast<std::size_t>(location->utf8_start);
  const auto length = static_cast<std::size_t>(location->utf8_length);
  const auto contents = std::span(source->utf8);
  if (start > contents.size() || length > contents.size() - start ||
      !utf8_boundary(contents, start) ||
      !utf8_boundary(contents, start + length)) {
    fail(invalid_source,
         "Source location is outside UTF-8 scalar boundaries for source '" +
             location->source + "'.");
  }
}

void verify_constant(const constant &value, const type &expected,
                     std::uint32_t depth = 0U) {
  if (depth > 64U) {
    fail(invalid_type, "Constant nesting exceeds semantic depth 64.");
  }
  const auto no_extra_metadata = [&value] {
    return value.types.empty() && value.nested.empty() &&
           !value.symbol.has_value() && value.text.empty();
  };
  switch (value.tag) {
  case NERI_IR_CONSTANT_BOOL_V1:
    if (expected.tag != NERI_IR_TYPE_BOOL_V1 || value.bits > 1U ||
        !no_extra_metadata()) {
      fail(invalid_type, "Malformed bool constant.");
    }
    return;
  case NERI_IR_CONSTANT_BYTE_V1:
    if (expected.tag != NERI_IR_TYPE_BYTE_V1 || value.bits > UINT64_C(255) ||
        !no_extra_metadata()) {
      fail(invalid_type, "Malformed byte constant.");
    }
    return;
  case NERI_IR_CONSTANT_INT_V1:
    if (expected.tag != NERI_IR_TYPE_INT_V1 || !no_extra_metadata()) {
      fail(invalid_type, "Malformed int constant.");
    }
    return;
  case NERI_IR_CONSTANT_FLOAT_V1:
    if (expected.tag != NERI_IR_TYPE_FLOAT_V1 || !no_extra_metadata()) {
      fail(invalid_type, "Malformed float constant.");
    }
    return;
  case NERI_IR_CONSTANT_OPTIONAL_NONE_V1:
    if (expected.tag != NERI_IR_TYPE_OPTIONAL_V1 ||
        value.types.size() != 1U || !same_type(value.types.front(), expected) ||
        !value.nested.empty() || value.symbol.has_value() ||
        !value.text.empty()) {
      fail(invalid_type, "Malformed optional.none constant.");
    }
    return;
  case NERI_IR_CONSTANT_OPTIONAL_SOME_V1:
    if (expected.tag != NERI_IR_TYPE_OPTIONAL_V1 ||
        expected.arguments.size() != 1U || value.types.size() != 1U ||
        !same_type(value.types.front(), expected) || value.nested.size() != 1U ||
        value.symbol.has_value() || !value.text.empty()) {
      fail(invalid_type, "Malformed optional.some constant.");
    }
    verify_constant(value.nested.front(), expected.arguments.front(), depth + 1U);
    return;
  case NERI_IR_CONSTANT_STRING_GLOBAL_V1:
    if (!is_string(expected) || value.types.size() != 0U ||
        !value.nested.empty() || !value.symbol.has_value() ||
        !value.text.empty()) {
      fail(invalid_type, "Malformed string.global constant.");
    }
    return;
  case NERI_IR_CONSTANT_STRING_UTF8_V1:
    if (!is_string(expected) || !value.types.empty() ||
        !value.nested.empty() || value.symbol.has_value() ||
        !strict_utf8(std::span(
            reinterpret_cast<const std::uint8_t *>(value.text.data()),
            value.text.size()))) {
      fail(invalid_type, "Malformed string.utf8 constant.");
    }
    return;
  default:
    fail(unsupported_feature,
         "Constant form is not supported by native lowering.");
  }
}

[[nodiscard]] bool printable_ascii_symbol(std::string_view value) {
  return !value.empty() &&
         std::ranges::all_of(value, [](unsigned char character) {
           return character >= static_cast<unsigned char>('!') &&
                  character <= static_cast<unsigned char>('~');
         });
}

[[nodiscard]] bool portable_c_identifier(std::string_view value) {
  if (value.empty() ||
      !(value.front() == '_' ||
        (value.front() >= 'A' && value.front() <= 'Z') ||
        (value.front() >= 'a' && value.front() <= 'z'))) {
    return false;
  }
  return std::ranges::all_of(value.substr(1U), [](const char character) {
    return character == '_' || (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9');
  });
}

[[nodiscard]] bool native_library_name(std::string_view value) {
  const auto alphanumeric = [](char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9');
  };
  return !value.empty() && value.size() <= 128U && alphanumeric(value.front()) &&
         std::ranges::all_of(value, [&](char character) {
           return alphanumeric(character) || character == '_' || character == '-' || character == '.';
         });
}

template <typename T, typename Projection>
void require_unique_order(const std::vector<T> &values,
                          std::string_view description,
                          Projection projection) {
  for (std::size_t index = 1; index < values.size(); ++index) {
    if (!(projection(values[index - 1U]) < projection(values[index]))) {
      fail(malformed_module,
           std::string(description) + " are not in unique canonical order.");
    }
  }
}

[[nodiscard]] const function *find_function(const ir_module &module,
                                            const symbol_id &id) {
  const auto match = std::ranges::find_if(module.functions, [&id](const auto &item) {
    return same_symbol(item.id, id);
  });
  return match == module.functions.end() ? nullptr : &*match;
}

[[nodiscard]] const import_declaration *
find_import(const ir_module &module, const symbol_id &id) {
  const auto match = std::ranges::find_if(module.imports, [&id](const auto &item) {
    return same_symbol(item.id, id);
  });
  return match == module.imports.end() ? nullptr : &*match;
}

[[nodiscard]] const class_declaration *find_class(const ir_module &module,
                                                  const symbol_id &id) {
  const auto match = std::ranges::find_if(module.classes, [&id](const auto &item) {
    return same_symbol(item.id, id);
  });
  return match == module.classes.end() ? nullptr : &*match;
}

struct found_field final {
  const class_declaration *owner;
  const field *value;
};

[[nodiscard]] std::optional<found_field> find_field(const ir_module &module,
                                                    const symbol_id &id) {
  for (const auto &owner : module.classes) {
    const auto match = std::ranges::find_if(owner.fields, [&id](const auto &item) {
      return same_symbol(item.id, id);
    });
    if (match != owner.fields.end()) {
      return found_field{&owner, &*match};
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool is_same_or_base(const ir_module &module,
                                   const symbol_id &derived,
                                   const symbol_id &possible_base) {
  const symbol_id *current = &derived;
  auto remaining = module.classes.size() + 1U;
  while (remaining-- != 0U) {
    if (same_symbol(*current, possible_base)) {
      return true;
    }
    const auto *declaration = find_class(module, *current);
    if (declaration == nullptr || !declaration->base.has_value()) {
      return false;
    }
    current = &*declaration->base;
  }
  return false;
}

void require_declared_type(const ir_module &module, const type &value,
                           std::string_view description) {
  if (value.tag != NERI_IR_TYPE_FIXED_ARRAY_V1 && value.element_count != 0U)
    fail(invalid_type, "Only fixed arrays carry an element count.");
  require_value_type(value, description);
  if (value.tag == NERI_IR_TYPE_NATIVE_RECORD_V1 || value.tag == NERI_IR_TYPE_FIXED_ARRAY_V1) {
    if (std::ranges::find(module.required_features, "native-records-v1") == module.required_features.end())
      fail(unsupported_feature, "Native aggregate types require native-records-v1.");
    (void)native_layouts(module).layout(value);
  }
  if (extended_scalar(value.tag) &&
      std::ranges::find(module.required_features, "extended-scalars-v1") == module.required_features.end())
    fail(unsupported_feature, "Extended scalar types require extended-scalars-v1.");
  if (is_class(value) && find_class(module, *value.symbol) == nullptr) {
    fail(invalid_reference,
         std::string(description) + " references a missing class.");
  }
  for (const auto &argument : value.arguments) {
    if (!(is_pointer(value) && is_void(argument)))
      require_declared_type(module, argument, description);
  }
}

struct definition final {
  const type *value_type{};
  std::uint32_t block{};
  std::int64_t instruction{};
};

struct use_site final {
  std::uint32_t value{};
  std::uint32_t block{};
  std::int64_t instruction{};
};

struct function_context final {
  function_context(const ir_module &input_module,
                   const function &input_function)
      : module(input_module), value(input_function) {}

  const ir_module &module;
  const function &value;
  std::map<std::uint32_t, const block *> blocks;
  std::map<std::uint32_t, std::vector<std::uint32_t>> successors;
  std::map<std::uint32_t, std::vector<std::uint32_t>> predecessors;
  std::map<std::uint32_t, definition> definitions;
  std::vector<use_site> uses;
  std::uint32_t required_effects{};
};

void verify_instruction_shape(const instruction &value, std::size_t results,
                              std::size_t operands, std::size_t type_arguments,
                              bool symbol, bool constant_value, bool predicate) {
  if (value.results.size() != results || value.operands.size() != operands ||
      value.type_arguments.size() != type_arguments ||
      value.symbol.has_value() != symbol ||
      value.constant_value.has_value() != constant_value ||
      value.predicate.has_value() != predicate || value.flag) {
    fail(invalid_type, "Instruction has invalid operand or metadata shape.");
  }
}

[[nodiscard]] const type &definition_type(const function_context &context,
                                          std::uint32_t id) {
  const auto found = context.definitions.find(id);
  if (found == context.definitions.end()) {
    fail(invalid_ssa, "Instruction references undefined SSA value " +
                          std::to_string(id) + ".");
  }
  return *found->second.value_type;
}

void require_operand_type(const function_context &context,
                          const instruction &value, std::size_t index,
                          const type &expected) {
  if (index >= value.operands.size() ||
      !same_type(definition_type(context, value.operands[index]), expected)) {
    fail(invalid_type, "Instruction operand has the wrong semantic type.");
  }
}

void require_result_type(const instruction &value, std::size_t index,
                         const type &expected) {
  if (index >= value.results.size() ||
      !same_type(value.results[index].value_type, expected)) {
    fail(invalid_type, "Instruction result has the wrong semantic type.");
  }
}

void require_unary(const function_context &context, const instruction &value,
                   std::uint8_t operand_tag, std::uint8_t result_tag) {
  verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
  const type operand{operand_tag, std::nullopt, {}};
  const type result{result_tag, std::nullopt, {}};
  require_operand_type(context, value, 0U, operand);
  require_result_type(value, 0U, result);
}

void require_binary(const function_context &context, const instruction &value,
                    std::uint8_t operand_tag, std::uint8_t result_tag) {
  verify_instruction_shape(value, 1U, 2U, 0U, false, false, false);
  const type operand{operand_tag, std::nullopt, {}};
  const type result{result_tag, std::nullopt, {}};
  require_operand_type(context, value, 0U, operand);
  require_operand_type(context, value, 1U, operand);
  require_result_type(value, 0U, result);
}

[[nodiscard]] std::uint32_t verify_call(function_context &context,
                                        const instruction &value,
                                        bool imported,
                                        bool require_method = false,
                                        bool c_abi = false) {
  if (!value.symbol.has_value()) {
    fail(invalid_reference, "Call instruction has no target symbol.");
  }
  const std::vector<type> *parameters = nullptr;
  const type *result = nullptr;
  std::uint32_t effects = 0U;
  if (imported) {
    const auto *target = find_import(context.module, *value.symbol);
    const auto expected_kind = c_abi ? NERI_IR_IMPORT_C_ABI_V1
                                     : NERI_IR_IMPORT_RUNTIME_V1;
    if (target == nullptr || target->kind != expected_kind) {
      fail(invalid_reference,
           "Import call references a missing or wrong-kind import.");
    }
    parameters = &target->parameter_types;
    result = &target->result_type;
    effects = target->effects;
  } else {
    const auto *target = find_function(context.module, *value.symbol);
    const auto valid_kind = target != nullptr &&
                            (require_method
                                 ? target->kind == NERI_IR_INSTANCE_METHOD_V1 ||
                                       target->kind == NERI_IR_CONSTRUCTOR_V1
                                 : target->kind == NERI_IR_FUNCTION_V1 ||
                                       target->kind == NERI_IR_STATIC_METHOD_V1 ||
                                       target->kind == NERI_IR_DEFAULT_ADAPTER_V1);
    if (!valid_kind || target->unsafe_call) {
      fail(invalid_reference,
           "Direct call references a missing or unsupported function.");
    }
    parameters = &target->parameter_types;
    result = &target->result_type;
    effects = target->effects;
  }

  const auto operand_offset = c_abi ? 1U : 0U;
  const auto result_count = is_void(*result) ? 0U : 1U;
  verify_instruction_shape(value, result_count,
                           parameters->size() + operand_offset, 0U, true,
                           false, false);
  if (c_abi) {
    const type capability{NERI_IR_TYPE_UNSAFE_CAPABILITY_V1, std::nullopt,
                          {}};
    require_operand_type(context, value, 0U, capability);
  }
  for (std::size_t index = 0; index < parameters->size(); ++index) {
    require_operand_type(context, value, index + operand_offset,
                         (*parameters)[index]);
  }
  if (result_count == 1U) {
    require_result_type(value, 0U, *result);
  }
  return effects & ~NERI_IR_EFFECT_NO_RETURN_V1;
}

[[nodiscard]] std::uint32_t verify_virtual_call(function_context &context,
                                                const instruction &value) {
  if (!value.symbol.has_value() ||
      value.symbol->kind != NERI_IR_SYMBOL_METHOD_V1) {
    fail(invalid_reference,
         "Virtual call requires a dispatch-slot method symbol.");
  }

  std::vector<const function *> candidates;
  for (const auto &candidate : context.module.functions) {
    if (candidate.dispatch_slot.has_value() &&
        same_symbol(*candidate.dispatch_slot, *value.symbol)) {
      candidates.push_back(&candidate);
    }
  }
  if (candidates.empty()) {
    fail(invalid_reference,
         "Virtual call references a dispatch slot without implementations.");
  }

  const function *signature = candidates.front();
  std::uint32_t effects = 0U;
  for (const auto *candidate : candidates) {
    if (candidate->unsafe_call ||
        candidate->kind != NERI_IR_INSTANCE_METHOD_V1 ||
        candidate->parameter_types.empty() ||
        !is_class(candidate->parameter_types.front()) ||
        candidate->parameter_types.size() != signature->parameter_types.size() ||
        !same_type(candidate->result_type, signature->result_type)) {
      fail(invalid_reference,
           "Virtual dispatch slot has an incompatible implementation.");
    }
    for (std::size_t index = 1U; index < candidate->parameter_types.size();
         ++index) {
      if (!same_type(candidate->parameter_types[index],
                     signature->parameter_types[index])) {
        fail(invalid_reference,
             "Virtual dispatch slot has incompatible parameter types.");
      }
    }
    if (is_same_or_base(context.module,
                        *signature->parameter_types.front().symbol,
                        *candidate->parameter_types.front().symbol)) {
      signature = candidate;
    }
    effects |= candidate->effects;
  }

  const auto result_count = is_void(signature->result_type) ? 0U : 1U;
  verify_instruction_shape(value, result_count,
                           signature->parameter_types.size(), 0U, true, false,
                           false);
  for (std::size_t index = 0; index < signature->parameter_types.size(); ++index) {
    require_operand_type(context, value, index,
                         signature->parameter_types[index]);
  }
  if (result_count == 1U) {
    require_result_type(value, 0U, signature->result_type);
  }
  // One non-returning implementation does not make the dispatch slot noreturn.
  return effects & ~NERI_IR_EFFECT_NO_RETURN_V1;
}

[[nodiscard]] std::uint32_t verify_instruction(function_context &context,
                                               const instruction &value) {
  const type bool_type{NERI_IR_TYPE_BOOL_V1, std::nullopt, {}};
  for (const auto operand : value.operands) {
    static_cast<void>(definition_type(context, operand));
  }
  if (value.opcode >= NERI_IR_OPCODE_INT_NEG_CHECKED_V1 && value.opcode <= NERI_IR_OPCODE_FLOAT_DIV_V1 && value.results.size() != 1U)
    fail(invalid_type, "Numeric arithmetic requires one result.");

  switch (value.opcode) {
  case NERI_IR_OPCODE_CONSTANT_V1:
    verify_instruction_shape(value, 1U, 0U, 0U, false, true, false);
    verify_constant(*value.constant_value, value.results.front().value_type);
    if (value.constant_value->tag == NERI_IR_CONSTANT_STRING_GLOBAL_V1) {
      const auto match = std::ranges::find_if(
          context.module.globals, [&value](const auto &global) {
            return same_symbol(global.id, *value.constant_value->symbol) &&
                   is_string(global.value_type) &&
                   global.initializer.tag == NERI_IR_CONSTANT_STRING_UTF8_V1;
          });
      if (match == context.module.globals.end()) {
        fail(invalid_reference,
             "string.global references a missing or non-string global.");
      }
    }
    return 0U;
  case NERI_IR_OPCODE_INT_NEG_CHECKED_V1:
    if (!integer_scalar(value.results.at(0).value_type.tag) || value.results.at(0).value_type.tag == NERI_IR_TYPE_BYTE_V1)
      fail(invalid_type, "Integer negation requires an arithmetic integer.");
    require_unary(context, value, value.results.at(0).value_type.tag,
                  value.results.at(0).value_type.tag);
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_INT_ADD_CHECKED_V1:
  case NERI_IR_OPCODE_INT_SUB_CHECKED_V1:
  case NERI_IR_OPCODE_INT_MUL_CHECKED_V1:
  case NERI_IR_OPCODE_INT_DIV_CHECKED_V1:
    if (!integer_scalar(value.results.at(0).value_type.tag) || value.results.at(0).value_type.tag == NERI_IR_TYPE_BYTE_V1)
      fail(invalid_type, "Integer arithmetic requires an arithmetic integer.");
    require_binary(context, value, value.results.at(0).value_type.tag,
                   value.results.at(0).value_type.tag);
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_FLOAT_NEG_V1:
    if (!floating_scalar(value.results.at(0).value_type.tag)) fail(invalid_type, "Float negation requires a floating-point type.");
    require_unary(context, value, value.results.at(0).value_type.tag,
                  value.results.at(0).value_type.tag);
    return 0U;
  case NERI_IR_OPCODE_FLOAT_ADD_V1:
  case NERI_IR_OPCODE_FLOAT_SUB_V1:
  case NERI_IR_OPCODE_FLOAT_MUL_V1:
  case NERI_IR_OPCODE_FLOAT_DIV_V1:
    if (!floating_scalar(value.results.at(0).value_type.tag)) fail(invalid_type, "Float arithmetic requires a floating-point type.");
    require_binary(context, value, value.results.at(0).value_type.tag,
                   value.results.at(0).value_type.tag);
    return 0U;
  case NERI_IR_OPCODE_BOOL_NOT_V1:
    require_unary(context, value, NERI_IR_TYPE_BOOL_V1,
                  NERI_IR_TYPE_BOOL_V1);
    return 0U;
  case NERI_IR_OPCODE_BOOL_AND_V1:
  case NERI_IR_OPCODE_BOOL_OR_V1:
    require_binary(context, value, NERI_IR_TYPE_BOOL_V1,
                   NERI_IR_TYPE_BOOL_V1);
    return 0U;
  case NERI_IR_OPCODE_COMPARE_V1: {
    verify_instruction_shape(value, 1U, 2U, 0U, false, false, true);
    require_result_type(value, 0U, bool_type);
    const auto &left = definition_type(context, value.operands[0]);
    const auto &right = definition_type(context, value.operands[1]);
    if (!same_type(left, right)) {
      fail(invalid_type, "Comparison operands have different semantic types.");
    }
    const auto equality = *value.predicate == NERI_IR_COMPARISON_EQUAL_V1 ||
                          *value.predicate == NERI_IR_COMPARISON_NOT_EQUAL_V1;
    if ((!is_scalar(left) && left.tag != NERI_IR_TYPE_OPTIONAL_V1) ||
        ((left.tag == NERI_IR_TYPE_BOOL_V1 ||
          left.tag == NERI_IR_TYPE_OPTIONAL_V1) &&
         !equality)) {
      fail(invalid_type, "Comparison predicate is invalid for its operand type.");
    }
    return 0U;
  }
  case NERI_IR_OPCODE_CAST_INT_TO_FLOAT_V1:
    require_unary(context, value, NERI_IR_TYPE_INT_V1,
                  NERI_IR_TYPE_FLOAT_V1);
    return 0U;
  case NERI_IR_OPCODE_NUMERIC_CAST_CHECKED_V1: {
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    const auto source = definition_type(context, value.operands.front()).tag;
    const auto target = value.results.front().value_type.tag;
    if (!(integer_scalar(source) || floating_scalar(source)) ||
        !(integer_scalar(target) || floating_scalar(target)))
      fail(invalid_type, "Numeric conversion requires numeric operands and results.");
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_CAST_FLOAT_TO_INT_CHECKED_V1:
    require_unary(context, value, NERI_IR_TYPE_FLOAT_V1,
                  NERI_IR_TYPE_INT_V1);
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_CAST_INT_TO_BYTE_CHECKED_V1:
    require_unary(context, value, NERI_IR_TYPE_INT_V1,
                  NERI_IR_TYPE_BYTE_V1);
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_CAST_BYTE_TO_INT_V1:
    require_unary(context, value, NERI_IR_TYPE_BYTE_V1,
                  NERI_IR_TYPE_INT_V1);
    return 0U;
  case NERI_IR_OPCODE_CLASS_UPCAST_V1: {
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    const auto &source = definition_type(context, value.operands.front());
    const auto &target = value.results.front().value_type;
    if (!is_class(source) || !is_class(target) ||
        same_symbol(*source.symbol, *target.symbol) ||
        !is_same_or_base(context.module, *source.symbol, *target.symbol)) {
      fail(invalid_type,
           "class.upcast requires a strict declared base-class conversion.");
    }
    return 0U;
  }
  case NERI_IR_OPCODE_OPTIONAL_NONE_V1: {
    verify_instruction_shape(value, 1U, 0U, 1U, false, false, false);
    const auto &result = value.results.front().value_type;
    if (result.tag != NERI_IR_TYPE_OPTIONAL_V1 ||
        result.arguments.size() != 1U ||
        !same_type(result.arguments.front(), value.type_arguments.front())) {
      fail(invalid_type, "optional.none has inconsistent type metadata.");
    }
    return 0U;
  }
  case NERI_IR_OPCODE_OPTIONAL_SOME_V1: {
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    const auto &result = value.results.front().value_type;
    if (result.tag != NERI_IR_TYPE_OPTIONAL_V1 ||
        result.arguments.size() != 1U ||
        !same_type(result.arguments.front(),
                   definition_type(context, value.operands.front()))) {
      fail(invalid_type, "optional.some has inconsistent operand type.");
    }
    return 0U;
  }
  case NERI_IR_OPCODE_OPTIONAL_IS_SOME_V1:
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    require_result_type(value, 0U, bool_type);
    if (definition_type(context, value.operands.front()).tag !=
        NERI_IR_TYPE_OPTIONAL_V1) {
      fail(invalid_type, "optional.is_some requires an optional operand.");
    }
    return 0U;
  case NERI_IR_OPCODE_OPTIONAL_GET_CHECKED_V1: {
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    const auto &operand = definition_type(context, value.operands.front());
    if (operand.tag != NERI_IR_TYPE_OPTIONAL_V1 ||
        operand.arguments.size() != 1U ||
        !same_type(operand.arguments.front(), value.results.front().value_type)) {
      fail(invalid_type, "optional.get.checked has an invalid result type.");
    }
    return NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_STRING_CONCAT_V1:
    require_binary(context, value, NERI_IR_TYPE_STRING_V1,
                   NERI_IR_TYPE_STRING_V1);
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_STRING_EQUAL_V1:
  case NERI_IR_OPCODE_STRING_NOT_EQUAL_V1:
    require_binary(context, value, NERI_IR_TYPE_STRING_V1,
                   NERI_IR_TYPE_BOOL_V1);
    return NERI_IR_EFFECT_READ_V1;
  case NERI_IR_OPCODE_STRING_FROM_INT_V1:
    require_unary(context, value, NERI_IR_TYPE_INT_V1,
                  NERI_IR_TYPE_STRING_V1);
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_STRING_FROM_BYTE_V1:
    require_unary(context, value, NERI_IR_TYPE_BYTE_V1,
                  NERI_IR_TYPE_STRING_V1);
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_STRING_FROM_FLOAT_V1:
    require_unary(context, value, NERI_IR_TYPE_FLOAT_V1,
                  NERI_IR_TYPE_STRING_V1);
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  case NERI_IR_OPCODE_CALL_V1:
    return verify_call(context, value, false);
  case NERI_IR_OPCODE_CALL_DIRECT_V1:
    return verify_call(context, value, false, true);
  case NERI_IR_OPCODE_CALL_VIRTUAL_V1:
    return verify_virtual_call(context, value);
  case NERI_IR_OPCODE_CALL_IMPORT_V1:
    return verify_call(context, value, true);
  case NERI_IR_OPCODE_CALL_C_ABI_V1:
    return verify_call(context, value, true, false, true);
  case NERI_IR_OPCODE_ARRAY_NEW_V1: {
    if (value.type_arguments.size() != 1U) {
      fail(invalid_type, "array.new requires one element type argument.");
    }
    verify_instruction_shape(value, 1U, value.operands.size(), 1U, false,
                             false, false);
    const auto &element = value.type_arguments.front();
    const type array_type{NERI_IR_TYPE_ARRAY_V1, std::nullopt, {element}};
    require_result_type(value, 0U, array_type);
    for (std::size_t index = 0; index < value.operands.size(); ++index) {
      require_operand_type(context, value, index, element);
    }
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_ARRAY_LENGTH_V1: {
    verify_instruction_shape(value, 1U, 1U, 0U, false, false, false);
    const auto &array = definition_type(context, value.operands.front());
    if (array.tag != NERI_IR_TYPE_ARRAY_V1 || array.arguments.size() != 1U) {
      fail(invalid_type, "array.length requires an array operand.");
    }
    const type int_type{NERI_IR_TYPE_INT_V1, std::nullopt, {}};
    require_result_type(value, 0U, int_type);
    return NERI_IR_EFFECT_READ_V1;
  }
  case NERI_IR_OPCODE_ARRAY_LOAD_CHECKED_V1: {
    verify_instruction_shape(value, 1U, 2U, 0U, false, false, false);
    const auto &array = definition_type(context, value.operands[0]);
    const type int_type{NERI_IR_TYPE_INT_V1, std::nullopt, {}};
    if (array.tag != NERI_IR_TYPE_ARRAY_V1 || array.arguments.size() != 1U) {
      fail(invalid_type, "array.load.checked requires an array operand.");
    }
    require_operand_type(context, value, 1U, int_type);
    require_result_type(value, 0U, array.arguments.front());
    return NERI_IR_EFFECT_READ_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_ARRAY_STORE_CHECKED_V1: {
    verify_instruction_shape(value, 0U, 3U, 0U, false, false, false);
    const auto &array = definition_type(context, value.operands[0]);
    const type int_type{NERI_IR_TYPE_INT_V1, std::nullopt, {}};
    if (array.tag != NERI_IR_TYPE_ARRAY_V1 || array.arguments.size() != 1U) {
      fail(invalid_type, "array.store.checked requires an array operand.");
    }
    require_operand_type(context, value, 1U, int_type);
    require_operand_type(context, value, 2U, array.arguments.front());
    return NERI_IR_EFFECT_WRITE_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_OBJECT_ALLOC_V1: {
    verify_instruction_shape(value, 1U, 0U, 0U, true, false, false);
    if (!value.symbol.has_value() ||
        value.symbol->kind != NERI_IR_SYMBOL_CLASS_V1 ||
        find_class(context.module, *value.symbol) == nullptr) {
      fail(invalid_reference,
           "object.alloc references a missing or wrong-kind class.");
    }
    const type expected{NERI_IR_TYPE_CLASS_V1, value.symbol, {}};
    require_result_type(value, 0U, expected);
    return NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
           NERI_IR_EFFECT_SAFEPOINT_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_FIELD_LOAD_V1:
  case NERI_IR_OPCODE_FIELD_STORE_V1: {
    const auto store = value.opcode == NERI_IR_OPCODE_FIELD_STORE_V1;
    verify_instruction_shape(value, store ? 0U : 1U, store ? 2U : 1U, 0U,
                             true, false, false);
    if (!value.symbol.has_value()) {
      fail(invalid_reference, "Field access has no field symbol.");
    }
    const auto match = find_field(context.module, *value.symbol);
    if (!match.has_value()) {
      fail(invalid_reference,
           "Field access references a missing or wrong-kind field.");
    }
    const auto &receiver = definition_type(context, value.operands.front());
    if (!is_class(receiver) ||
        !is_same_or_base(context.module, *receiver.symbol, match->owner->id)) {
      fail(invalid_type,
           "Field access receiver is incompatible with its declaring class.");
    }
    if (store) {
      require_operand_type(context, value, 1U, match->value->value_type);
    } else {
      require_result_type(value, 0U, match->value->value_type);
    }
    return store ? NERI_IR_EFFECT_WRITE_V1 : NERI_IR_EFFECT_READ_V1;
  }
  case NERI_IR_OPCODE_UNSAFE_BEGIN_V1: {
    verify_instruction_shape(value, 1U, 0U, 0U, false, false, false);
    require_result_type(value, 0U,
                        type{NERI_IR_TYPE_UNSAFE_CAPABILITY_V1,
                             std::nullopt, {}});
    return NERI_IR_EFFECT_UNSAFE_V1;
  }
  case NERI_IR_OPCODE_UNSAFE_END_V1: {
    verify_instruction_shape(value, 0U, 1U, 0U, false, false, false);
    if (!is_unsafe_capability(
            definition_type(context, value.operands.front()))) {
      fail(invalid_safety, "unsafe.end requires an unsafe capability.");
    }
    return NERI_IR_EFFECT_UNSAFE_V1;
  }
  case NERI_IR_OPCODE_NATIVE_FIELD_ADDRESS_V1: {
    verify_instruction_shape(value, 1U, 2U, 0U, true, false, false);
    if (!is_unsafe_capability(definition_type(context, value.operands[0])))
      fail(invalid_safety, "Native field address requires an unsafe capability.");
    const auto &pointer = definition_type(context, value.operands[1]);
    if (!is_pointer(pointer) || pointer.arguments.front().tag != NERI_IR_TYPE_NATIVE_RECORD_V1)
      fail(invalid_type, "Native field address requires a record pointer.");
    const auto &record = native_layouts(context.module).declaration(pointer.arguments.front());
    const auto field = std::ranges::find_if(record.fields, [&](const auto &item) { return same_symbol(item.id, *value.symbol); });
    if (field == record.fields.end()) fail(invalid_reference, "Native field does not belong to its pointer type.");
    require_result_type(value, 0U, type{NERI_IR_TYPE_POINTER_V1, std::nullopt, {field->value_type}});
    return NERI_IR_EFFECT_UNSAFE_V1;
  }
  case NERI_IR_OPCODE_NATIVE_INDEX_ADDRESS_CHECKED_V1: {
    verify_instruction_shape(value, 1U, 3U, 0U, false, false, false);
    if (!is_unsafe_capability(definition_type(context, value.operands[0])))
      fail(invalid_safety, "Native array access requires an unsafe capability.");
    const auto &pointer = definition_type(context, value.operands[1]);
    if (!is_pointer(pointer) || pointer.arguments.front().tag != NERI_IR_TYPE_FIXED_ARRAY_V1)
      fail(invalid_type, "Native array access requires a fixed-array pointer.");
    const auto &array = pointer.arguments.front();
    require_operand_type(context, value, 2U, type{NERI_IR_TYPE_INT_V1, std::nullopt, {}});
    require_result_type(value, 0U, type{NERI_IR_TYPE_POINTER_V1, std::nullopt, {array.arguments.front()}});
    return NERI_IR_EFFECT_UNSAFE_V1 | NERI_IR_EFFECT_MAY_PANIC_V1;
  }
  case NERI_IR_OPCODE_STACK_ALLOC_V1:
  case NERI_IR_OPCODE_POINTER_LOAD_V1:
  case NERI_IR_OPCODE_POINTER_STORE_V1:
  case NERI_IR_OPCODE_POINTER_OFFSET_V1:
  case NERI_IR_OPCODE_POINTER_DIFFERENCE_V1:
  case NERI_IR_OPCODE_POINTER_COMPARE_V1:
  case NERI_IR_OPCODE_POINTER_CAST_V1:
  case NERI_IR_OPCODE_NATIVE_ALLOC_V1:
  case NERI_IR_OPCODE_NATIVE_REALLOC_V1:
  case NERI_IR_OPCODE_NATIVE_FREE_V1:
  case NERI_IR_OPCODE_BORROW_BEGIN_V1:
  case NERI_IR_OPCODE_BORROW_END_V1: {
    const auto opcode = value.opcode;
    const auto is_store = opcode == NERI_IR_OPCODE_POINTER_STORE_V1;
    const auto is_free = opcode == NERI_IR_OPCODE_NATIVE_FREE_V1;
    const auto is_borrow_end = opcode == NERI_IR_OPCODE_BORROW_END_V1;
    const auto is_borrow_begin = opcode == NERI_IR_OPCODE_BORROW_BEGIN_V1;
    const auto is_cast = opcode == NERI_IR_OPCODE_POINTER_CAST_V1;
    const auto is_compare = opcode == NERI_IR_OPCODE_POINTER_COMPARE_V1;
    const auto is_three_operands =
        is_store || opcode == NERI_IR_OPCODE_POINTER_OFFSET_V1 ||
        opcode == NERI_IR_OPCODE_POINTER_DIFFERENCE_V1 || is_compare ||
        opcode == NERI_IR_OPCODE_NATIVE_REALLOC_V1;
    const auto expected_results =
        is_store || is_free || is_borrow_end ? 0U : is_borrow_begin ? 2U : 1U;
    const auto expected_operands = is_three_operands ? 3U : 2U;
    const auto expected_types = is_cast ? 2U : is_borrow_end ? 0U : 1U;
    const auto permits_flag = opcode == NERI_IR_OPCODE_NATIVE_ALLOC_V1;
    if (value.results.size() != expected_results ||
        value.operands.size() != expected_operands ||
        value.type_arguments.size() != expected_types ||
        value.symbol.has_value() || value.constant_value.has_value() ||
        value.predicate.has_value() != is_compare ||
        (value.flag && !permits_flag)) {
      fail(invalid_type,
           "Pointer instruction has invalid operand or metadata shape.");
    }
    if (!is_unsafe_capability(definition_type(context, value.operands[0]))) {
      fail(invalid_safety,
           "Pointer instruction requires an unsafe capability operand.");
    }
    if (is_borrow_end) {
      if (!is_borrow_capability(
              definition_type(context, value.operands[1]))) {
        fail(invalid_safety, "borrow.end requires a borrow capability.");
      }
      return NERI_IR_EFFECT_UNSAFE_V1;
    }

    const auto &element = value.type_arguments.front();
    if (is_void(element) && !is_cast && !is_compare && !is_free) {
      fail(invalid_type,
           "Pointer instruction requires a sized element type.");
    }
    if (!is_pointer_element(element)) {
      fail(invalid_type,
           "Pointer instruction uses an unsupported element type.");
    }
    const type pointer{NERI_IR_TYPE_POINTER_V1, std::nullopt, {element}};
    const type integer{NERI_IR_TYPE_INT_V1, std::nullopt, {}};
    const type boolean{NERI_IR_TYPE_BOOL_V1, std::nullopt, {}};

    if (opcode == NERI_IR_OPCODE_STACK_ALLOC_V1 ||
        opcode == NERI_IR_OPCODE_NATIVE_ALLOC_V1) {
      require_operand_type(context, value, 1U, integer);
      require_result_type(value, 0U, pointer);
    } else if (opcode == NERI_IR_OPCODE_POINTER_LOAD_V1) {
      require_operand_type(context, value, 1U, pointer);
      require_result_type(value, 0U, element);
    } else if (is_store) {
      require_operand_type(context, value, 1U, pointer);
      require_operand_type(context, value, 2U, element);
    } else if (opcode == NERI_IR_OPCODE_POINTER_OFFSET_V1) {
      require_operand_type(context, value, 1U, pointer);
      require_operand_type(context, value, 2U, integer);
      require_result_type(value, 0U, pointer);
    } else if (opcode == NERI_IR_OPCODE_POINTER_DIFFERENCE_V1) {
      require_operand_type(context, value, 1U, pointer);
      require_operand_type(context, value, 2U, pointer);
      require_result_type(value, 0U, integer);
    } else if (is_compare) {
      require_operand_type(context, value, 1U, pointer);
      require_operand_type(context, value, 2U, pointer);
      require_result_type(value, 0U, boolean);
      if (*value.predicate != NERI_IR_COMPARISON_EQUAL_V1 &&
          *value.predicate != NERI_IR_COMPARISON_NOT_EQUAL_V1 &&
          *value.predicate != NERI_IR_COMPARISON_LESS_V1 &&
          *value.predicate != NERI_IR_COMPARISON_LESS_OR_EQUAL_V1 &&
          *value.predicate != NERI_IR_COMPARISON_GREATER_V1 &&
          *value.predicate != NERI_IR_COMPARISON_GREATER_OR_EQUAL_V1) {
        fail(invalid_type, "pointer.compare has an invalid predicate.");
      }
    } else if (is_cast) {
      const auto &target = value.type_arguments[1];
      if (!is_pointer_element(target)) {
        fail(invalid_type, "pointer.cast has an invalid target element type.");
      }
      require_operand_type(context, value, 1U, pointer);
      require_result_type(
          value, 0U,
          type{NERI_IR_TYPE_POINTER_V1, std::nullopt, {target}});
    } else if (opcode == NERI_IR_OPCODE_NATIVE_REALLOC_V1) {
      require_operand_type(context, value, 1U, pointer);
      require_operand_type(context, value, 2U, integer);
      require_result_type(value, 0U, pointer);
    } else if (is_free) {
      require_operand_type(context, value, 1U, pointer);
    } else if (is_borrow_begin) {
      require_operand_type(
          context, value, 1U,
          type{NERI_IR_TYPE_ARRAY_V1, std::nullopt, {element}});
      require_result_type(value, 0U, pointer);
      require_result_type(
          value, 1U,
          type{NERI_IR_TYPE_BORROW_CAPABILITY_V1, std::nullopt, {element}});
    }

    std::uint32_t effects = NERI_IR_EFFECT_UNSAFE_V1;
    if (opcode == NERI_IR_OPCODE_STACK_ALLOC_V1)
      effects |= NERI_IR_EFFECT_MAY_PANIC_V1;
    if (opcode == NERI_IR_OPCODE_POINTER_LOAD_V1 || is_borrow_begin)
      effects |= NERI_IR_EFFECT_READ_V1;
    if (is_store || is_free)
      effects |= NERI_IR_EFFECT_WRITE_V1;
    if (opcode == NERI_IR_OPCODE_NATIVE_ALLOC_V1 ||
        opcode == NERI_IR_OPCODE_NATIVE_REALLOC_V1)
      effects |= NERI_IR_EFFECT_NATIVE_ALLOCATE_V1 |
                 NERI_IR_EFFECT_MAY_PANIC_V1;
    if (is_borrow_begin)
      effects |= NERI_IR_EFFECT_SAFEPOINT_V1;
    return effects;
  }
  default:
    fail(unsupported_feature,
         "Opcode " + std::to_string(value.opcode) +
             " belongs to a later native-lowering card.");
  }
}

void register_definitions(function_context &context) {
  if (context.value.unsafe_root.has_value()) {
    const auto &root = *context.value.unsafe_root;
    if (!is_unsafe_capability(root.value_type) ||
        !context.definitions
             .emplace(root.id,
                      definition{&root.value_type,
                                 context.value.entry_block, -1})
             .second) {
      fail(invalid_safety,
           "Function unsafe root is malformed or has a duplicate SSA ID.");
    }
  }
  for (const auto &block : context.value.blocks) {
    for (const auto &parameter : block.parameters) {
      require_value_type(parameter.value_type, "Block parameter");
      verify_location(parameter.location, context.module);
      if (!context.definitions
               .emplace(parameter.id,
                        definition{&parameter.value_type, block.id, -1})
               .second) {
        fail(invalid_ssa, "SSA value has multiple definitions.");
      }
    }
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      const auto &instruction = block.instructions[index];
      verify_location(instruction.location, context.module);
      for (const auto &result : instruction.results) {
        require_value_type(result.value_type, "Instruction result");
        if (!context.definitions
                 .emplace(result.id,
                          definition{&result.value_type, block.id,
                                     static_cast<std::int64_t>(index)})
                 .second) {
          fail(invalid_ssa, "SSA value has multiple definitions.");
        }
      }
    }
  }
}

void register_use(function_context &context, std::uint32_t value,
                  std::uint32_t block, std::int64_t instruction) {
  if (!context.definitions.contains(value)) {
    fail(invalid_ssa,
         "Use references undefined SSA value " + std::to_string(value) + ".");
  }
  context.uses.push_back({value, block, instruction});
}

void verify_edge(function_context &context, const block &source,
                 const edge &value, std::int64_t instruction) {
  const auto target = context.blocks.find(value.target);
  if (target == context.blocks.end()) {
    fail(invalid_control_flow, "Edge references a missing block.");
  }
  if (value.target == context.value.entry_block) {
    fail(unsupported_feature,
         "Native lowering does not accept backedges to the "
         "function entry block.");
  }
  if (value.arguments.size() != target->second->parameters.size()) {
    fail(invalid_control_flow,
         "Edge argument count does not match target block parameters.");
  }
  for (std::size_t index = 0; index < value.arguments.size(); ++index) {
    register_use(context, value.arguments[index], source.id, instruction);
    if (!same_type(definition_type(context, value.arguments[index]),
                   target->second->parameters[index].value_type)) {
      fail(invalid_control_flow,
           "Edge argument type does not match target block parameter.");
    }
  }
  context.successors[source.id].push_back(value.target);
  context.predecessors[value.target].push_back(source.id);
}

void verify_blocks_and_uses(function_context &context) {
  for (const auto &block : context.value.blocks) {
    context.blocks.emplace(block.id, &block);
  }
  if (context.blocks.size() != context.value.blocks.size()) {
    fail(invalid_control_flow, "Function contains duplicate block identities.");
  }
  if (context.value.blocks.empty() ||
      !context.blocks.contains(context.value.entry_block)) {
    fail(invalid_control_flow, "Function has no valid entry block.");
  }
  if (context.value.blocks.front().id != context.value.entry_block) {
    fail(invalid_control_flow, "Function entry block is not first.");
  }

  register_definitions(context);
  const auto &entry = *context.blocks.at(context.value.entry_block);
  if (entry.parameters.size() != context.value.parameter_types.size()) {
    fail(invalid_type,
         "Entry block parameter count does not match function signature.");
  }
  for (std::size_t index = 0; index < entry.parameters.size(); ++index) {
    if (!same_type(entry.parameters[index].value_type,
                   context.value.parameter_types[index])) {
      fail(invalid_type,
           "Entry block parameter type does not match function signature.");
    }
  }

  for (const auto &block : context.value.blocks) {
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      const auto &instruction = block.instructions[index];
      for (const auto operand : instruction.operands) {
        register_use(context, operand, block.id,
                     static_cast<std::int64_t>(index));
      }
      const auto effects = verify_instruction(context, instruction);
      context.required_effects |= effects;
      if (((effects & NERI_IR_EFFECT_MAY_PANIC_V1) != 0U ||
           instruction.opcode == NERI_IR_OPCODE_CALL_V1 ||
           instruction.opcode == NERI_IR_OPCODE_CALL_DIRECT_V1 ||
           instruction.opcode == NERI_IR_OPCODE_CALL_VIRTUAL_V1 ||
           instruction.opcode == NERI_IR_OPCODE_CALL_IMPORT_V1 ||
           instruction.opcode == NERI_IR_OPCODE_CALL_C_ABI_V1) &&
          !instruction.location.has_value()) {
        fail(invalid_source,
             "Panicking and call instructions require a source location.");
      }
    }

    const auto instruction_index =
        static_cast<std::int64_t>(block.instructions.size());
    verify_location(block.ending.location, context.module);
    switch (block.ending.tag) {
    case NERI_IR_TERMINATOR_BRANCH_V1:
      if (block.ending.edges.size() != 1U ||
          block.ending.condition.has_value() ||
          block.ending.return_value.has_value()) {
        fail(invalid_control_flow, "Malformed branch terminator.");
      }
      verify_edge(context, block, block.ending.edges.front(), instruction_index);
      break;
    case NERI_IR_TERMINATOR_CONDITIONAL_BRANCH_V1: {
      if (block.ending.edges.size() != 2U ||
          !block.ending.condition.has_value() ||
          block.ending.return_value.has_value()) {
        fail(invalid_control_flow, "Malformed conditional branch terminator.");
      }
      register_use(context, *block.ending.condition, block.id,
                   instruction_index);
      const type bool_type{NERI_IR_TYPE_BOOL_V1, std::nullopt, {}};
      if (!same_type(definition_type(context, *block.ending.condition),
                     bool_type)) {
        fail(invalid_type, "Conditional branch condition is not bool.");
      }
      verify_edge(context, block, block.ending.edges[0], instruction_index);
      verify_edge(context, block, block.ending.edges[1], instruction_index);
      break;
    }
    case NERI_IR_TERMINATOR_RETURN_V1:
      if (!block.ending.edges.empty() || block.ending.condition.has_value()) {
        fail(invalid_control_flow, "Malformed return terminator.");
      }
      if (is_void(context.value.result_type)) {
        if (block.ending.return_value.has_value()) {
          fail(invalid_type, "Void function returns an SSA value.");
        }
      } else {
        if (!block.ending.return_value.has_value()) {
          fail(invalid_type, "Non-void function returns no SSA value.");
        }
        register_use(context, *block.ending.return_value, block.id,
                     instruction_index);
        if (!same_type(definition_type(context, *block.ending.return_value),
                       context.value.result_type)) {
          fail(invalid_type, "Return value does not match function result type.");
        }
      }
      break;
    case NERI_IR_TERMINATOR_PANIC_V1:
      if (!block.ending.edges.empty() || block.ending.condition.has_value() ||
          block.ending.return_value.has_value() ||
          block.ending.panic_code.empty() ||
          block.ending.panic_message.empty() ||
          !block.ending.location.has_value()) {
        fail(invalid_control_flow, "Malformed panic terminator.");
      }
      context.required_effects |= NERI_IR_EFFECT_MAY_PANIC_V1;
      break;
    default:
      fail(unsupported_feature, "Unsupported terminator tag.");
    }
  }
}

void verify_canonical_cfg(const function_context &context) {
  std::vector<std::uint32_t> discovered;
  std::set<std::uint32_t> seen;
  std::queue<std::uint32_t> pending;
  pending.push(context.value.entry_block);
  while (!pending.empty()) {
    const auto current = pending.front();
    pending.pop();
    if (!seen.insert(current).second) {
      continue;
    }
    discovered.push_back(current);
    const auto successors = context.successors.find(current);
    if (successors == context.successors.end()) {
      continue;
    }
    for (const auto successor : successors->second) {
      if (!seen.contains(successor)) {
        pending.push(successor);
      }
    }
  }
  if (discovered.size() != context.blocks.size()) {
    fail(invalid_control_flow, "Function contains unreachable blocks.");
  }
  for (std::size_t index = 0; index < discovered.size(); ++index) {
    if (context.value.blocks[index].id != discovered[index]) {
      fail(invalid_control_flow,
           "Function block order is not canonical entry/successor order.");
    }
  }
}

void verify_dominance(const function_context &context) {
  using block_set = std::set<std::uint32_t>;
  block_set all;
  for (const auto &[id, ignored] : context.blocks) {
    static_cast<void>(ignored);
    all.insert(id);
  }
  std::map<std::uint32_t, block_set> dominators;
  for (const auto id : all) {
    dominators[id] = id == context.value.entry_block ? block_set{id} : all;
  }

  bool changed = true;
  auto remaining = context.blocks.size() * context.blocks.size() + 1U;
  while (changed && remaining-- > 0U) {
    changed = false;
    for (const auto id : all) {
      if (id == context.value.entry_block) {
        continue;
      }
      const auto predecessors = context.predecessors.find(id);
      if (predecessors == context.predecessors.end() ||
          predecessors->second.empty()) {
        fail(invalid_control_flow, "Non-entry block has no predecessor.");
      }
      block_set intersection = dominators.at(predecessors->second.front());
      for (const auto predecessor :
           std::span(predecessors->second).subspan(1U)) {
        block_set next;
        std::ranges::set_intersection(intersection,
                                      dominators.at(predecessor),
                                      std::inserter(next, next.end()));
        intersection = std::move(next);
      }
      intersection.insert(id);
      if (intersection != dominators.at(id)) {
        dominators[id] = std::move(intersection);
        changed = true;
      }
    }
  }
  if (changed) {
    fail(invalid_control_flow,
         "Dominance computation exceeded its bounded iteration budget.");
  }

  for (const auto &use : context.uses) {
    const auto &definition = context.definitions.at(use.value);
    if (definition.block == use.block) {
      if (definition.instruction >= use.instruction &&
          definition.instruction >= 0) {
        fail(invalid_ssa, "SSA value is used before its definition.");
      }
    } else if (!dominators.at(use.block).contains(definition.block)) {
      fail(invalid_ssa, "SSA definition does not dominate its use.");
    }
  }
}

void verify_function(const ir_module &module, const function &value) {
  verify_location(value.location, module);
  const auto is_method = value.kind == NERI_IR_STATIC_METHOD_V1 ||
                         value.kind == NERI_IR_INSTANCE_METHOD_V1 ||
                         value.kind == NERI_IR_CONSTRUCTOR_V1;
  const auto expected_symbol = is_method ? NERI_IR_SYMBOL_METHOD_V1
                                         : NERI_IR_SYMBOL_FUNCTION_V1;
  if (value.id.module != module.id || value.id.kind != expected_symbol) {
    fail(invalid_reference,
         "Function symbol kind disagrees with its declared function kind.");
  }
  if (value.kind < NERI_IR_FUNCTION_V1 ||
      value.kind > NERI_IR_DEFAULT_ADAPTER_V1) {
    fail(malformed_module, "Function has an invalid function kind.");
  }
  if (value.unsafe_call) {
    fail(unsupported_feature,
         "Unsafe-call function lowering belongs to a later native card.");
  }
  if (is_method != value.declaring_class.has_value()) {
    fail(malformed_module,
         "Function has inconsistent method and declaring-class metadata.");
  }
  if (value.declaring_class.has_value()) {
    if (find_class(module, *value.declaring_class) == nullptr) {
      fail(invalid_reference, "Method references a missing declaring class.");
    }
    if (value.kind == NERI_IR_INSTANCE_METHOD_V1 ||
        value.kind == NERI_IR_CONSTRUCTOR_V1) {
      const type receiver{NERI_IR_TYPE_CLASS_V1, value.declaring_class, {}};
      if (value.parameter_types.empty() ||
          !same_type(value.parameter_types.front(), receiver)) {
        fail(invalid_type,
             "Instance method or constructor requires its exact receiver as "
             "parameter zero.");
      }
    }
  }
  if (value.dispatch_slot.has_value() &&
      (value.kind != NERI_IR_INSTANCE_METHOD_V1 ||
       value.dispatch_slot->kind != NERI_IR_SYMBOL_METHOD_V1)) {
    fail(malformed_module,
         "Only virtual instance methods may declare a dispatch slot.");
  }
  for (const auto &parameter : value.parameter_types) {
    require_declared_type(module, parameter, "Function parameter");
  }
  require_result_type(value.result_type, "Function result");
  if (!is_void(value.result_type)) {
    require_declared_type(module, value.result_type, "Function result");
  }

  function_context context{module, value};
  verify_blocks_and_uses(context);
  std::set<std::pair<std::uint32_t, std::string>> debug_values;
  std::optional<std::pair<std::uint32_t, std::string_view>> previous_debug;
  for (const auto &local : value.debug_locals) {
    verify_location(local.location, module);
    const auto definition = context.definitions.find(local.value);
    if (local.name.empty() || definition == context.definitions.end() ||
        is_unsafe_capability(*definition->second.value_type) ||
        is_borrow_capability(*definition->second.value_type) ||
        !debug_values.emplace(local.value, local.name).second) {
      fail(invalid_safety,
           "Debug local has an invalid name, value, type, or duplicate value.");
    }
    const auto key = std::pair(local.value, std::string_view(local.name));
    if (previous_debug.has_value() && !(previous_debug.value() < key)) {
      fail(malformed_module,
           "Debug locals are not in canonical value/name order.");
    }
    previous_debug = key;
  }
  verify_canonical_cfg(context);
  verify_dominance(context);

  const auto missing_effects = context.required_effects & ~value.effects;
  if (missing_effects != 0U) {
    fail(invalid_safety,
         "Function effect summary omits required effects " +
             std::to_string(missing_effects) + ".");
  }
  const auto has_return = std::ranges::any_of(value.blocks, [](const block &item) {
    return item.ending.tag == NERI_IR_TERMINATOR_RETURN_V1;
  });
  if (!has_return && (value.effects & NERI_IR_EFFECT_NO_RETURN_V1) == 0U) {
    fail(invalid_safety, "Function without returns omits noreturn effect.");
  }
  if (has_return && (value.effects & NERI_IR_EFFECT_NO_RETURN_V1) != 0U) {
    fail(invalid_safety, "Noreturn function contains a return terminator.");
  }
}

} // namespace

void verify_supported_module(const ir_module &value) {
  if (value.semantic_version.major != 1U) {
    fail(incompatible_major,
         "IR major " + std::to_string(value.semantic_version.major) +
             " is incompatible with supported major 1.");
  }
  if (value.semantic_version.minor > 0U) {
    fail(unsupported_minor,
         "IR minor " + std::to_string(value.semantic_version.minor) +
             " is newer than supported minor 0.");
  }

  if (!std::ranges::is_sorted(value.required_features) ||
      std::adjacent_find(value.required_features.begin(),
                         value.required_features.end()) !=
          value.required_features.end()) {
    fail(malformed_module,
         "Required features are not in unique canonical order.");
  }
  for (const auto &feature : value.required_features) {
    const auto valid_character = [](char character) {
      return (character >= 'a' && character <= 'z') ||
             (character >= '0' && character <= '9') || character == '-';
    };
    if (feature.empty() || feature.front() == '-' || feature.back() == '-' ||
        !std::ranges::all_of(feature, valid_character) ||
        feature.find("--") != std::string::npos) {
      fail(malformed_module,
           "Required feature '" + feature +
               "' is not a canonical feature identifier.");
    }
    if (feature != "string-data-v1" && feature != "native-strings-v1" && feature != "native-libraries-v1" && feature != "extended-scalars-v1" && feature != "native-records-v1") {
      fail(unsupported_feature,
           "Unknown required semantic feature '" + feature + "'.");
    }
  }

  if (!value.native_records.empty() && std::ranges::find(value.required_features, "native-records-v1") == value.required_features.end())
    fail(unsupported_feature, "Native records require native-records-v1.");
  native_layouts layouts(value);
  std::set<std::string> native_names;
  for (const auto &record : value.native_records) {
    if (record.id.module != value.id || record.id.kind != NERI_IR_SYMBOL_NATIVE_RECORD_V1 ||
        !native_names.insert(record.id.semantic_name).second)
      fail(invalid_reference, "Invalid or duplicate native record identity.");
    std::set<std::string> field_names;
    for (const auto &field : record.fields) {
      if (field.id.module != value.id || field.id.kind != NERI_IR_SYMBOL_FIELD_V1 ||
          !field.id.semantic_name.starts_with(record.id.semantic_name + ".") ||
          field.access != NERI_IR_ACCESS_PUBLIC_V1 || !field_names.insert(field.id.semantic_name).second)
        fail(invalid_reference, "Invalid or duplicate native field identity.");
      require_declared_type(value, field.value_type, "Native field");
      verify_location(field.location, value);
    }
    (void)layouts.layout(type{NERI_IR_TYPE_NATIVE_RECORD_V1, record.id, {}});
  }

  const bool has_string_data =
      std::ranges::find(value.required_features, "string-data-v1") !=
      value.required_features.end();
  const bool has_native_strings =
      std::ranges::find(value.required_features, "native-strings-v1") !=
      value.required_features.end();

  bool uses_native_strings = std::ranges::any_of(
      value.globals,
      [](const auto &global) { return contains_string(global.value_type); });
  for (const auto &declaration : value.classes) {
    uses_native_strings |= std::ranges::any_of(
        declaration.fields,
        [](const auto &item) { return contains_string(item.value_type); });
  }
  for (const auto &function : value.functions) {
    uses_native_strings |= contains_string(function.result_type) ||
                           std::ranges::any_of(function.parameter_types,
                                               contains_string);
    for (const auto &block : function.blocks) {
      uses_native_strings |= std::ranges::any_of(
          block.parameters,
          [](const auto &parameter) {
            return contains_string(parameter.value_type);
          });
      for (const auto &instruction : block.instructions) {
        uses_native_strings |= std::ranges::any_of(
            instruction.results,
            [](const auto &result) {
              return contains_string(result.value_type);
            });
      }
    }
  }
  if (uses_native_strings && !has_native_strings) {
    fail(unsupported_feature,
         "Native string values require semantic feature 'native-strings-v1'.");
  }

  require_unique_order(value.sources, "Sources",
                       [](const source &item) { return item.id; });
  for (const auto &source : value.sources) {
    if (!strict_utf8(source.utf8)) {
      fail(invalid_source,
           "Source '" + source.id + "' does not contain strict UTF-8.");
    }
  }
  require_unique_order(value.classes, "Classes", [](const auto &item) {
    return symbol_key(item.id);
  });
  require_unique_order(value.globals, "Globals", [](const auto &item) {
    return symbol_key(item.id);
  });
  require_unique_order(value.imports, "Imports", [](const auto &item) {
    return symbol_key(item.id);
  });
  require_unique_order(value.functions, "Functions", [](const auto &item) {
    return symbol_key(item.id);
  });

  std::set<decltype(symbol_key(symbol_id{}))> declarations;
  const auto valid_access = [](std::uint8_t access) {
    return access >= NERI_IR_ACCESS_PUBLIC_V1 &&
           access <= NERI_IR_ACCESS_INTERNAL_V1;
  };
  for (const auto &declaration : value.classes) {
    verify_location(declaration.location, value);
    if (declaration.id.module != value.id ||
        declaration.id.kind != NERI_IR_SYMBOL_CLASS_V1 ||
        !valid_access(declaration.access) ||
        !declarations.insert(symbol_key(declaration.id)).second) {
      fail(invalid_reference, "Class has invalid or duplicate metadata.");
    }
    if (declaration.base.has_value() &&
        (declaration.base->kind != NERI_IR_SYMBOL_CLASS_V1 ||
         find_class(value, *declaration.base) == nullptr)) {
      fail(invalid_reference, "Class references a missing base class.");
    }

    std::set<decltype(symbol_key(symbol_id{}))> fields;
    for (const auto &item : declaration.fields) {
      verify_location(item.location, value);
      if (item.id.module != value.id ||
          item.id.kind != NERI_IR_SYMBOL_FIELD_V1 ||
          !valid_access(item.access) ||
          !fields.insert(symbol_key(item.id)).second ||
          !declarations.insert(symbol_key(item.id)).second) {
        fail(invalid_reference, "Class field has invalid or duplicate metadata.");
      }
      require_declared_type(value, item.value_type, "Class field");
    }

    std::set<decltype(symbol_key(symbol_id{}))> methods;
    for (const auto &item : declaration.methods) {
      verify_location(item.location, value);
      if (!methods.insert(symbol_key(item.function_id)).second) {
        fail(malformed_module, "Class has a duplicate method entry.");
      }
      const auto *target = find_function(value, item.function_id);
      if (target == nullptr || !target->declaring_class.has_value() ||
          !same_symbol(*target->declaring_class, declaration.id)) {
        fail(invalid_reference,
             "Class method entry disagrees with function metadata.");
      }
      const auto virtual_dispatch = item.dispatch == NERI_IR_DISPATCH_VIRTUAL_V1;
      const auto static_dispatch = item.dispatch == NERI_IR_DISPATCH_STATIC_V1;
      const auto direct_dispatch = item.dispatch == NERI_IR_DISPATCH_DIRECT_V1;
      if ((!virtual_dispatch && !static_dispatch && !direct_dispatch) ||
          (virtual_dispatch != item.dispatch_slot.has_value()) ||
          (virtual_dispatch != target->dispatch_slot.has_value()) ||
          (virtual_dispatch &&
           !same_symbol(*item.dispatch_slot, *target->dispatch_slot)) ||
          (static_dispatch != (target->kind == NERI_IR_STATIC_METHOD_V1)) ||
          (virtual_dispatch && target->kind != NERI_IR_INSTANCE_METHOD_V1) ||
          (direct_dispatch &&
           target->kind != NERI_IR_INSTANCE_METHOD_V1 &&
           target->kind != NERI_IR_CONSTRUCTOR_V1)) {
        fail(invalid_reference,
             "Class method dispatch metadata is inconsistent.");
      }
    }
  }
  for (const auto &declaration : value.classes) {
    const auto *current = &declaration;
    auto remaining = value.classes.size();
    while (current->base.has_value()) {
      if (remaining-- == 0U) {
        fail(malformed_module, "Class inheritance contains a cycle.");
      }
      current = find_class(value, *current->base);
      if (current == nullptr) {
        fail(invalid_reference, "Class inheritance references a missing class.");
      }
    }
  }
  for (const auto &global : value.globals) {
    verify_location(global.location, value);
    if (global.id.module != value.id ||
        global.id.kind != NERI_IR_SYMBOL_GLOBAL_V1 ||
        !declarations.insert(symbol_key(global.id)).second) {
      fail(invalid_reference, "Global has an invalid or duplicate symbol.");
    }
    require_declared_type(value, global.value_type, "Global");
    verify_constant(global.initializer, global.value_type);
    if (global.initializer.tag == NERI_IR_CONSTANT_STRING_UTF8_V1 &&
        !has_string_data) {
      fail(unsupported_feature,
           "String UTF-8 data requires semantic feature 'string-data-v1'.");
    }
    if (is_string(global.value_type) &&
        global.initializer.tag != NERI_IR_CONSTANT_STRING_UTF8_V1) {
      fail(invalid_type,
           "String global requires a canonical string.utf8 initializer.");
    }
  }
  std::map<std::string, const import_declaration *> links;
  for (const auto &import : value.imports) {
    verify_location(import.location, value);
    if (import.id.module != value.id ||
        (import.id.kind != NERI_IR_SYMBOL_LIBRARY_FUNCTION_V1 &&
         import.id.kind != NERI_IR_SYMBOL_INTRINSIC_V1 &&
         import.id.kind != NERI_IR_SYMBOL_FUNCTION_V1) ||
        !declarations.insert(symbol_key(import.id)).second) {
      fail(invalid_reference, "Import has an invalid or duplicate symbol.");
    }
    for (const auto &parameter : import.parameter_types) {
      require_declared_type(value, parameter, "Import parameter");
    }
    require_result_type(import.result_type, "Import result");
    if (!is_void(import.result_type)) {
      require_declared_type(value, import.result_type, "Import result");
    }
    if (!printable_ascii_symbol(import.link_name)) {
      fail(malformed_module, "Import link name is not printable ASCII.");
    }
    if (!import.native_library.empty() &&
        (import.kind != NERI_IR_IMPORT_C_ABI_V1 || !native_library_name(import.native_library) ||
         std::ranges::find(value.required_features, "native-libraries-v1") == value.required_features.end())) {
      fail(invalid_type, "Native library requires a C ABI import, a portable library name, and native-libraries-v1.");
    }
    if (import.kind == NERI_IR_IMPORT_RUNTIME_V1) {
      if (!import.minimum_runtime.has_value() ||
          import.minimum_runtime->abi_major != 1U ||
          !import.link_name.starts_with("neri_rt_v1_") ||
          import.link_name == "neri_rt_v1_panic") {
        fail(unsupported_feature,
             "Runtime import does not match the reviewed ABI v1 surface.");
      }
    } else if (import.kind == NERI_IR_IMPORT_C_ABI_V1) {
      const auto required_effects = NERI_IR_EFFECT_READ_V1 |
                                    NERI_IR_EFFECT_WRITE_V1 |
                                    NERI_IR_EFFECT_UNSAFE_V1;
      if ((import.effects & required_effects) != required_effects) {
        fail(invalid_safety,
             "C ABI import weakens the conservative effect contract.");
      }
      if (import.minimum_runtime.has_value() ||
          !portable_c_identifier(import.link_name) ||
          !is_c_abi_type(import.result_type, true) ||
          !std::ranges::all_of(import.parameter_types, [](const auto &type) {
            return is_c_abi_type(type, false);
          })) {
        fail(invalid_type,
             "C ABI import has a non-portable symbol, signature, or runtime "
             "requirement.");
      }
    } else {
      fail(unsupported_feature, "Import kind is unsupported.");
    }
    if (const auto existing = links.find(import.link_name);
        existing != links.end()) {
      const auto *previous = existing->second;
      const auto same_parameters =
          previous->parameter_types.size() == import.parameter_types.size() &&
          std::ranges::equal(previous->parameter_types, import.parameter_types,
                             same_type);
      if (previous->kind != import.kind ||
          !same_type(previous->result_type, import.result_type) ||
          previous->effects != import.effects ||
          !same_runtime_requirement(previous->minimum_runtime,
                                    import.minimum_runtime) ||
          !same_parameters) {
        fail(malformed_module,
             "Import link name is declared with incompatible contracts.");
      }
    } else {
      links.emplace(import.link_name, &import);
    }
  }
  for (const auto &function : value.functions) {
    if (!declarations.insert(symbol_key(function.id)).second) {
      fail(malformed_module,
           "Function symbol duplicates another module declaration.");
    }
  }
  for (const auto &function : value.functions) {
    verify_function(value, function);
  }
}

} // namespace neri::codegen
