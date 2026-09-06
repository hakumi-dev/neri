#include "neri/codegen/reader.h"

#include "ir_verifier.h"
#include "neri/ir_transport.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/SHA256.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neri::codegen {
namespace {

constexpr std::string_view incompatible_major = "NIR001";
constexpr std::string_view unsupported_minor = "NIR002";
constexpr std::string_view unsupported_feature = "NIR003";
constexpr std::string_view malformed_module = "NIR004";
constexpr std::string_view invalid_source = "NIR010";

[[noreturn]] void fail(std::string_view code, std::string message,
                       std::size_t offset) {
  throw reader_error(std::string(code), std::move(message), offset);
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U;
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) {
  std::uint32_t result = 0;
  for (std::size_t index = 0; index < sizeof(result); ++index) {
    result |= static_cast<std::uint32_t>(bytes[offset + index]) <<
              static_cast<unsigned>(index * CHAR_BIT);
  }
  return result;
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) {
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(bytes[offset + index]) <<
              static_cast<unsigned>(index * CHAR_BIT);
  }
  return result;
}

[[nodiscard]] bool strict_utf8(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return true;
  }
  const auto *cursor = reinterpret_cast<const llvm::UTF8 *>(bytes.data());
  const auto *end = cursor + bytes.size();
  return llvm::isLegalUTF8String(&cursor, end) != 0;
}

[[nodiscard]] std::uint32_t decode_scalar(std::span<const std::uint8_t> bytes,
                                          std::size_t &index) {
  const auto first = bytes[index++];
  if (first < UINT8_C(0x80)) {
    return first;
  }

  std::uint32_t value = 0;
  std::size_t continuation_count = 0;
  if ((first & UINT8_C(0xe0)) == UINT8_C(0xc0)) {
    value = first & UINT8_C(0x1f);
    continuation_count = 1;
  } else if ((first & UINT8_C(0xf0)) == UINT8_C(0xe0)) {
    value = first & UINT8_C(0x0f);
    continuation_count = 2;
  } else {
    value = first & UINT8_C(0x07);
    continuation_count = 3;
  }

  for (std::size_t count = 0; count < continuation_count; ++count) {
    value = (value << 6U) | (bytes[index++] & UINT8_C(0x3f));
  }
  return value;
}

[[nodiscard]] bool unicode_whitespace(std::uint32_t value) {
  return value == UINT32_C(0x0009) || value == UINT32_C(0x000a) ||
         value == UINT32_C(0x000b) || value == UINT32_C(0x000c) ||
         value == UINT32_C(0x000d) || value == UINT32_C(0x0020) ||
         value == UINT32_C(0x0085) || value == UINT32_C(0x00a0) ||
         value == UINT32_C(0x1680) ||
         (value >= UINT32_C(0x2000) && value <= UINT32_C(0x200a)) ||
         value == UINT32_C(0x2028) || value == UINT32_C(0x2029) ||
         value == UINT32_C(0x202f) || value == UINT32_C(0x205f) ||
         value == UINT32_C(0x3000);
}

void validate_identity(std::string_view value, std::string_view description,
                       std::size_t offset) {
  const auto bytes = std::span(
      reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
  bool non_whitespace = false;
  for (std::size_t index = 0; index < bytes.size();) {
    const auto scalar = decode_scalar(bytes, index);
    if (scalar <= UINT32_C(0x001f) ||
        (scalar >= UINT32_C(0x007f) && scalar <= UINT32_C(0x009f))) {
      fail(malformed_module,
           std::string(description) + " cannot contain control characters.",
           offset);
    }
    non_whitespace |= !unicode_whitespace(scalar);
  }

  if (!non_whitespace) {
    fail(malformed_module, std::string(description) + " cannot be empty.",
         offset);
  }
}

class cursor final {
public:
  cursor(std::span<const std::uint8_t> input, const reader_options &options,
         std::size_t base_offset)
      : input_(input), options_(options), base_offset_(base_offset) {}

  [[nodiscard]] std::size_t offset() const noexcept {
    return base_offset_ + offset_;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return input_.size() - offset_;
  }

  [[nodiscard]] std::uint8_t u8() { return bytes(1U)[0]; }

  [[nodiscard]] bool boolean() {
    const auto field_offset = offset();
    const auto value = u8();
    if (value > 1U) {
      fail(malformed_module,
           "Boolean byte must be 0 or 1, found " + std::to_string(value) +
               ".",
           field_offset);
    }
    return value == 1U;
  }

  [[nodiscard]] std::uint16_t u16() {
    return read_u16(bytes(sizeof(std::uint16_t)), 0U);
  }

  [[nodiscard]] std::uint32_t u32() {
    return read_u32(bytes(sizeof(std::uint32_t)), 0U);
  }

  [[nodiscard]] std::uint64_t u64() {
    return read_u64(bytes(sizeof(std::uint64_t)), 0U);
  }

  [[nodiscard]] std::uint32_t model_id(std::string_view description) {
    const auto field_offset = offset();
    const auto value = u32();
    if (value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
      fail(malformed_module,
           std::string(description) + " " + std::to_string(value) +
               " exceeds transport v1's signed model range.",
           field_offset);
    }
    return value;
  }

  [[nodiscard]] std::uint32_t count(std::string_view description) {
    const auto field_offset = offset();
    const auto value = u32();
    if (value > options_.max_collection_count) {
      fail(malformed_module,
           std::string(description) + " count " + std::to_string(value) +
               " exceeds reader limit " +
               std::to_string(options_.max_collection_count) + ".",
           field_offset);
    }
    if (value > remaining()) {
      fail(malformed_module,
           std::string(description) + " count " + std::to_string(value) +
               " cannot fit in the remaining " +
               std::to_string(remaining()) + " bytes.",
           field_offset);
    }
    return value;
  }

  [[nodiscard]] std::string utf8() {
    const auto byte_count = length("UTF-8 field");
    const auto field_offset = offset();
    const auto contents = bytes(byte_count);
    if (!strict_utf8(contents)) {
      fail(invalid_source, "Field is not strict UTF-8.", field_offset);
    }
    return {reinterpret_cast<const char *>(contents.data()), contents.size()};
  }

  [[nodiscard]] std::vector<std::uint8_t> blob() {
    const auto size = length("byte field");
    const auto contents = bytes(size);
    return {contents.begin(), contents.end()};
  }

  [[nodiscard]] std::span<const std::uint8_t> bytes(std::size_t length) {
    if (length > remaining()) {
      fail(malformed_module,
           "Unexpected end of payload while reading " +
               std::to_string(length) + " byte(s).",
           offset());
    }
    const auto result = input_.subspan(offset_, length);
    offset_ += length;
    return result;
  }

  void require_end() const {
    if (remaining() != 0U) {
      fail(malformed_module,
           "Payload has " + std::to_string(remaining()) +
               " trailing corruption byte(s).",
           offset());
    }
  }

private:
  [[nodiscard]] std::size_t length(std::string_view description) {
    const auto field_offset = offset();
    const auto value = u32();
    if (value > options_.max_string_bytes) {
      fail(malformed_module,
           std::string(description) + " length " + std::to_string(value) +
               " exceeds reader limit " +
               std::to_string(options_.max_string_bytes) + ".",
           field_offset);
    }
    if (value > remaining()) {
      fail(malformed_module,
           std::string(description) + " length " + std::to_string(value) +
               " exceeds remaining payload " +
               std::to_string(remaining()) + ".",
           field_offset);
    }
    return value;
  }

  std::span<const std::uint8_t> input_;
  const reader_options &options_;
  std::size_t base_offset_{};
  std::size_t offset_{};
};

template <typename T, typename Reader>
[[nodiscard]] std::vector<T> read_vector(cursor &input,
                                         std::string_view description,
                                         Reader &&read) {
  const auto count = input.count(description);
  std::vector<T> values;
  values.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    values.push_back(read());
  }
  return values;
}

template <typename T, typename Reader>
[[nodiscard]] std::optional<T> read_optional(cursor &input, Reader &&read) {
  if (!input.boolean()) {
    return std::nullopt;
  }
  return read();
}

void require_tag(std::uint64_t tag, std::uint64_t first, std::uint64_t last,
                 std::string_view description, std::size_t offset) {
  if (tag < first || tag > last) {
    fail(unsupported_feature,
         "Unknown " + std::string(description) + " tag '" +
             std::to_string(tag) + "'.",
         offset);
  }
}

class payload_reader final {
public:
  payload_reader(std::span<const std::uint8_t> payload,
                 const reader_options &options, std::uint16_t transport_minor)
      : input_(payload, options, NERI_IR_HEADER_SIZE_V1), options_(options),
        transport_minor_(transport_minor) {}

  [[nodiscard]] ir_module read() {
    ir_module result;
    result.semantic_version = {input_.u16(), input_.u16()};
    result.id = identity("Module identity");
    result.required_features = read_vector<std::string>(
        input_, "required features", [this] { return input_.utf8(); });
    for (const auto &feature : result.required_features) {
      if (feature == "native-libraries-v1") native_libraries_ = true;
      if (feature == "native-records-v1") native_records_ = true;
      if (feature == "scoped-tasks-v1" && transport_minor_ < 4U)
        fail(unsupported_feature, "Scoped tasks require IR transport 1.4.", input_.offset());
    }
    if (native_libraries_ && transport_minor_ < 2U) {
      fail(unsupported_feature, "Native libraries require IR transport 1.2.", input_.offset());
    }
    result.sources = read_vector<source>(input_, "sources",
                                         [this] { return read_source(); });
    result.classes = read_vector<class_declaration>(
        input_, "classes", [this] { return read_class(); });
    result.globals = read_vector<global_declaration>(
        input_, "globals", [this] { return read_global(); });
    result.imports = read_vector<import_declaration>(
        input_, "imports", [this] { return read_import(); });
    result.functions = read_vector<function>(
        input_, "functions", [this] { return read_function(); });
    if (native_records_) {
      if (transport_minor_ < 3U)
        fail(unsupported_feature, "Native records require transport 1.3.", input_.offset());
      result.native_records = read_vector<native_record>(input_, "native records", [this] {
        native_record record;
        record.id = read_symbol();
        record.is_union = input_.boolean();
        record.fields = read_vector<field>(input_, "native fields", [this] { return read_field(); });
        return record;
      });
    }
    input_.require_end();
    return result;
  }

private:
  [[nodiscard]] std::string identity(std::string_view description) {
    const auto field_offset = input_.offset();
    auto value = input_.utf8();
    validate_identity(value, description, field_offset);
    return value;
  }

  [[nodiscard]] symbol_id read_symbol() {
    symbol_id value;
    value.module = identity("Symbol module identity");
    const auto kind_offset = input_.offset();
    value.kind = input_.u8();
    require_tag(value.kind, NERI_IR_SYMBOL_CLASS_V1,
                NERI_IR_SYMBOL_NATIVE_RECORD_V1, "symbol-kind", kind_offset);
    value.semantic_name = identity("Symbol semantic identity");
    return value;
  }

  [[nodiscard]] std::optional<source_location> read_location() {
    return read_optional<source_location>(input_, [this] {
      return source_location{identity("Source identity"),
                             input_.model_id("source byte start"),
                             input_.model_id("source byte length")};
    });
  }

  [[nodiscard]] type read_type(std::uint32_t depth = 0U) {
    if (depth > options_.max_nesting_depth) {
      fail(malformed_module,
           "Semantic type nesting exceeds reader limit " +
               std::to_string(options_.max_nesting_depth) + ".",
           input_.offset());
    }

    type result;
    const auto tag_offset = input_.offset();
    result.tag = input_.u8();
    switch (result.tag) {
    case NERI_IR_TYPE_VOID_V1:
    case NERI_IR_TYPE_BOOL_V1:
    case NERI_IR_TYPE_BYTE_V1:
    case NERI_IR_TYPE_INT_V1:
    case NERI_IR_TYPE_FLOAT_V1:
    case NERI_IR_TYPE_STRING_V1:
    case NERI_IR_TYPE_UNSAFE_CAPABILITY_V1:
      return result;
    case NERI_IR_TYPE_INT32_V1:
    case NERI_IR_TYPE_UINT32_V1:
    case NERI_IR_TYPE_UINT64_V1:
    case NERI_IR_TYPE_FLOAT32_V1:
      if (transport_minor_ < 2U) fail(unsupported_feature, "Extended scalars require transport 1.2.", tag_offset);
      return result;
    case NERI_IR_TYPE_CLASS_V1:
      result.symbol = read_symbol();
      return result;
    case NERI_IR_TYPE_NATIVE_RECORD_V1:
      if (transport_minor_ < 3U) fail(unsupported_feature, "Native records require transport 1.3.", tag_offset);
      result.symbol = read_symbol();
      return result;
    case NERI_IR_TYPE_FIXED_ARRAY_V1:
      if (transport_minor_ < 3U) fail(unsupported_feature, "Fixed arrays require transport 1.3.", tag_offset);
      result.element_count = input_.u32();
      result.arguments.push_back(read_type(depth + 1U));
      return result;
    case NERI_IR_TYPE_ARRAY_V1:
    case NERI_IR_TYPE_OPTIONAL_V1:
    case NERI_IR_TYPE_POINTER_V1:
    case NERI_IR_TYPE_BORROW_CAPABILITY_V1:
      result.arguments.push_back(read_type(depth + 1U));
      return result;
    default:
      fail(unsupported_feature,
           "Unknown semantic type tag '" + std::to_string(result.tag) +
               "'.",
           tag_offset);
    }
  }

  [[nodiscard]] constant read_constant(std::uint32_t depth = 0U) {
    if (depth > options_.max_nesting_depth) {
      fail(malformed_module,
           "Constant nesting exceeds reader limit " +
               std::to_string(options_.max_nesting_depth) + ".",
           input_.offset());
    }

    constant result;
    const auto tag_offset = input_.offset();
    result.tag = input_.u8();
    switch (result.tag) {
    case NERI_IR_CONSTANT_BOOL_V1:
      result.bits = input_.boolean() ? 1U : 0U;
      break;
    case NERI_IR_CONSTANT_BYTE_V1:
      result.bits = input_.u8();
      break;
    case NERI_IR_CONSTANT_INT_V1:
    case NERI_IR_CONSTANT_FLOAT_V1:
      result.bits = input_.u64();
      break;
    case NERI_IR_CONSTANT_OPTIONAL_NONE_V1:
      result.types.push_back(read_type(depth + 1U));
      break;
    case NERI_IR_CONSTANT_OPTIONAL_SOME_V1:
      result.types.push_back(read_type(depth + 1U));
      result.nested.push_back(read_constant(depth + 1U));
      break;
    case NERI_IR_CONSTANT_STRING_GLOBAL_V1:
      result.symbol = read_symbol();
      break;
    case NERI_IR_CONSTANT_STRING_UTF8_V1:
      result.text = input_.utf8();
      break;
    default:
      fail(unsupported_feature,
           "Unknown constant tag '" + std::to_string(result.tag) + "'.",
           tag_offset);
    }
    return result;
  }

  [[nodiscard]] source read_source() {
    return {identity("Source identity"), input_.blob()};
  }

  [[nodiscard]] field read_field() {
    field result{read_symbol(), read_type(), {}, {}};
    const auto access_offset = input_.offset();
    result.access = input_.u8();
    require_tag(result.access, NERI_IR_ACCESS_PUBLIC_V1,
                NERI_IR_ACCESS_INTERNAL_V1, "access-modifier",
                access_offset);
    result.location = read_location();
    return result;
  }

  [[nodiscard]] method read_method() {
    method result;
    result.function_id = read_symbol();
    const auto dispatch_offset = input_.offset();
    result.dispatch = input_.u8();
    require_tag(result.dispatch, NERI_IR_DISPATCH_STATIC_V1,
                NERI_IR_DISPATCH_VIRTUAL_V1, "dispatch-kind",
                dispatch_offset);
    result.dispatch_slot =
        read_optional<symbol_id>(input_, [this] { return read_symbol(); });
    result.location = read_location();
    return result;
  }

  [[nodiscard]] class_declaration read_class() {
    class_declaration result;
    result.id = read_symbol();
    result.base =
        read_optional<symbol_id>(input_, [this] { return read_symbol(); });
    const auto access_offset = input_.offset();
    result.access = input_.u8();
    require_tag(result.access, NERI_IR_ACCESS_PUBLIC_V1,
                NERI_IR_ACCESS_INTERNAL_V1, "access-modifier",
                access_offset);
    result.fields = read_vector<field>(input_, "class fields",
                                       [this] { return read_field(); });
    result.methods = read_vector<method>(input_, "class methods",
                                         [this] { return read_method(); });
    result.location = read_location();
    return result;
  }

  [[nodiscard]] global_declaration read_global() {
    global_declaration result{read_symbol(), read_type(), read_constant(), {}, {}};
    const auto linkage_offset = input_.offset();
    result.linkage = input_.u8();
    require_tag(result.linkage, NERI_IR_GLOBAL_INTERNAL_V1,
                NERI_IR_GLOBAL_EXPORTED_V1, "global-linkage",
                linkage_offset);
    result.location = read_location();
    return result;
  }

  [[nodiscard]] import_declaration read_import() {
    import_declaration result;
    result.id = read_symbol();
    result.parameter_types = read_vector<type>(
        input_, "import parameters", [this] { return read_type(); });
    result.result_type = read_type();
    const auto kind_offset = input_.offset();
    result.kind = input_.u8();
    require_tag(result.kind, NERI_IR_IMPORT_RUNTIME_V1,
                NERI_IR_IMPORT_C_ABI_V1, "import-kind", kind_offset);
    result.link_name = input_.utf8();
    result.effects = read_effects();
    result.minimum_runtime = read_optional<runtime_requirement>(input_, [this] {
      return runtime_requirement{input_.u16(), input_.u16(), input_.u64()};
    });
    result.location = read_location();
    if (native_libraries_) result.native_library = input_.utf8();
    return result;
  }

  [[nodiscard]] std::uint32_t read_effects() {
    const auto effects_offset = input_.offset();
    const auto effects = input_.u32();
    constexpr auto all_effects = NERI_IR_EFFECT_READ_V1 |
                                 NERI_IR_EFFECT_WRITE_V1 |
                                 NERI_IR_EFFECT_MAY_PANIC_V1 |
                                 NERI_IR_EFFECT_MANAGED_ALLOCATE_V1 |
                                 NERI_IR_EFFECT_NATIVE_ALLOCATE_V1 |
                                 NERI_IR_EFFECT_SAFEPOINT_V1 |
                                 NERI_IR_EFFECT_UNSAFE_V1 |
                                 NERI_IR_EFFECT_NO_RETURN_V1;
    if ((effects & ~all_effects) != 0U) {
      fail(unsupported_feature,
           "Unknown effect bits '" + std::to_string(effects & ~all_effects) +
               "'.",
           effects_offset);
    }
    return effects;
  }

  [[nodiscard]] value_definition read_value_definition() {
    return {input_.model_id("value ID"), read_type()};
  }

  [[nodiscard]] block_parameter read_block_parameter() {
    return {input_.model_id("value ID"), read_type(), read_location()};
  }

  [[nodiscard]] edge read_edge() {
    edge result;
    result.target = input_.model_id("block ID");
    result.arguments = read_vector<std::uint32_t>(
        input_, "edge arguments",
        [this] { return input_.model_id("value ID"); });
    return result;
  }

  [[nodiscard]] instruction read_instruction() {
    instruction result;
    const auto opcode_offset = input_.offset();
    result.opcode = input_.u16();
    require_tag(result.opcode, NERI_IR_OPCODE_CONSTANT_V1,
                NERI_IR_OPCODE_TASK_GENERATE_V1, "opcode", opcode_offset);
    result.results = read_vector<value_definition>(
        input_, "instruction results",
        [this] { return read_value_definition(); });
    result.operands = read_vector<std::uint32_t>(
        input_, "instruction operands",
        [this] { return input_.model_id("value ID"); });
    result.type_arguments = read_vector<type>(
        input_, "instruction type arguments", [this] { return read_type(); });
    result.symbol =
        read_optional<symbol_id>(input_, [this] { return read_symbol(); });
    result.constant_value = read_optional<constant>(
        input_, [this] { return read_constant(); });
    result.predicate = read_optional<std::uint8_t>(input_, [this] {
      const auto predicate_offset = input_.offset();
      const auto predicate = input_.u8();
      require_tag(predicate, NERI_IR_COMPARISON_EQUAL_V1,
                  NERI_IR_COMPARISON_GREATER_OR_EQUAL_V1,
                  "comparison-predicate", predicate_offset);
      return predicate;
    });
    result.flag = input_.boolean();
    result.location = read_location();
    return result;
  }

  [[nodiscard]] terminator read_terminator() {
    terminator result;
    const auto tag_offset = input_.offset();
    result.tag = input_.u8();
    switch (result.tag) {
    case NERI_IR_TERMINATOR_BRANCH_V1:
      result.edges.push_back(read_edge());
      break;
    case NERI_IR_TERMINATOR_CONDITIONAL_BRANCH_V1:
      result.condition = input_.model_id("value ID");
      result.edges.push_back(read_edge());
      result.edges.push_back(read_edge());
      break;
    case NERI_IR_TERMINATOR_RETURN_V1:
      result.return_value = read_optional<std::uint32_t>(
          input_, [this] { return input_.model_id("value ID"); });
      break;
    case NERI_IR_TERMINATOR_PANIC_V1:
      result.panic_code = input_.utf8();
      result.panic_message = input_.utf8();
      break;
    default:
      fail(unsupported_feature,
           "Unknown terminator tag '" + std::to_string(result.tag) + "'.",
           tag_offset);
    }
    result.location = read_location();
    return result;
  }

  [[nodiscard]] block read_block() {
    block result;
    result.id = input_.model_id("block ID");
    result.parameters = read_vector<block_parameter>(
        input_, "block parameters", [this] { return read_block_parameter(); });
    result.instructions = read_vector<instruction>(
        input_, "block instructions", [this] { return read_instruction(); });
    result.ending = read_terminator();
    return result;
  }

  [[nodiscard]] function read_function() {
    function result;
    result.id = read_symbol();
    result.parameter_types = read_vector<type>(
        input_, "function parameters", [this] { return read_type(); });
    result.result_type = read_type();
    const auto kind_offset = input_.offset();
    result.kind = input_.u8();
    require_tag(result.kind, NERI_IR_FUNCTION_V1,
                NERI_IR_DEFAULT_ADAPTER_V1, "function-kind", kind_offset);
    result.effects = read_effects();
    result.unsafe_call = input_.boolean();
    result.entry_block = input_.model_id("block ID");
    result.unsafe_root = read_optional<value_definition>(
        input_, [this] { return read_value_definition(); });
    result.declaring_class =
        read_optional<symbol_id>(input_, [this] { return read_symbol(); });
    result.dispatch_slot =
        read_optional<symbol_id>(input_, [this] { return read_symbol(); });
    result.location = read_location();
    result.blocks = read_vector<block>(input_, "function blocks",
                                       [this] { return read_block(); });
    if (transport_minor_ >= 1U) {
      result.debug_locals = read_vector<debug_local>(
          input_, "debug locals", [this] {
            debug_local value{input_.utf8(),
                              input_.model_id("debug local value"), {}};
            const auto location = read_location();
            if (!location.has_value()) {
              fail(malformed_module, "Debug local requires a source location.",
                   input_.offset());
            }
            value.location = *location;
            return value;
          });
    }
    return result;
  }

  cursor input_;
  const reader_options &options_;
  std::uint16_t transport_minor_{};
  bool native_libraries_{};
  bool native_records_{};
};

void validate_options(const reader_options &options) {
  if (options.max_input_bytes < NERI_IR_HEADER_SIZE_V1 ||
      options.max_string_bytes == 0U ||
      options.max_string_bytes > options.max_input_bytes ||
      options.max_collection_count == 0U || options.max_nesting_depth == 0U) {
    throw std::invalid_argument(
        "Neri IR reader limits are inconsistent or non-positive.");
  }
}

} // namespace

reader_error::reader_error(std::string code, std::string message,
                           std::size_t byte_offset)
    : std::runtime_error(std::move(message)), code_(std::move(code)),
      byte_offset_(byte_offset) {}

const std::string &reader_error::code() const noexcept { return code_; }

std::size_t reader_error::byte_offset() const noexcept { return byte_offset_; }

verified_module::verified_module(ir_module value) : value_(std::move(value)) {}

const ir_module &verified_module::value() const noexcept { return value_; }

verified_module read_verified_module(std::span<const std::uint8_t> input,
                                     const reader_options &options) {
  validate_options(options);
  if (input.size() > options.max_input_bytes) {
    fail(malformed_module,
         "Input length " + std::to_string(input.size()) +
             " exceeds reader limit " +
             std::to_string(options.max_input_bytes) + ".",
         0U);
  }

  const std::vector<std::uint8_t> snapshot(input.begin(), input.end());
  const auto envelope = std::span(snapshot);
  if (envelope.size() < NERI_IR_HEADER_SIZE_V1) {
    fail(malformed_module,
         "Input is shorter than the Neri IR envelope header.",
         envelope.size());
  }
  if (!std::ranges::equal(std::span(NERI_IR_MAGIC_V1),
                          envelope.first(std::size(NERI_IR_MAGIC_V1)))) {
    fail(malformed_module, "Neri IR magic is missing or corrupt.", 0U);
  }

  const auto transport_major =
      read_u16(envelope, NERI_IR_VERSION_MAJOR_OFFSET_V1);
  const auto transport_minor =
      read_u16(envelope, NERI_IR_VERSION_MINOR_OFFSET_V1);
  const auto flags = read_u32(envelope, NERI_IR_FLAGS_OFFSET_V1);
  const auto payload_length =
      read_u64(envelope, NERI_IR_PAYLOAD_LENGTH_OFFSET_V1);
  if (transport_major != NERI_IR_TRANSPORT_MAJOR_V1) {
    fail(incompatible_major,
         "Transport major " + std::to_string(transport_major) +
             " is incompatible with supported major " +
             std::to_string(NERI_IR_TRANSPORT_MAJOR_V1) + ".",
         NERI_IR_VERSION_MAJOR_OFFSET_V1);
  }
  if (transport_minor > NERI_IR_TRANSPORT_MINOR_V1) {
    fail(unsupported_minor,
         "Transport minor " + std::to_string(transport_minor) +
             " is newer than supported minor " +
             std::to_string(NERI_IR_TRANSPORT_MINOR_V1) + ".",
         NERI_IR_VERSION_MINOR_OFFSET_V1);
  }
  if (flags != 0U) {
    std::ostringstream message;
    message << "Unknown required envelope flags '0x" << std::hex << flags
            << "'.";
    fail(unsupported_feature, message.str(), NERI_IR_FLAGS_OFFSET_V1);
  }
  if (payload_length >
      options.max_input_bytes - NERI_IR_HEADER_SIZE_V1) {
    fail(malformed_module,
         "Declared payload length " + std::to_string(payload_length) +
             " exceeds reader limits.",
         NERI_IR_PAYLOAD_LENGTH_OFFSET_V1);
  }

  const auto expected_length = NERI_IR_HEADER_SIZE_V1 +
                               static_cast<std::size_t>(payload_length);
  if (envelope.size() != expected_length) {
    const auto description = envelope.size() < expected_length
                                 ? "Payload is truncated."
                                 : "Envelope has trailing corruption bytes.";
    fail(malformed_module, description,
         std::min(envelope.size(), expected_length));
  }

  const auto payload = envelope.subspan(NERI_IR_HEADER_SIZE_V1);
  const auto digest = llvm::SHA256::hash(
      llvm::ArrayRef<std::uint8_t>(payload.data(), payload.size()));
  std::uint8_t digest_difference = 0U;
  for (std::size_t index = 0; index < digest.size(); ++index) {
    digest_difference |= static_cast<std::uint8_t>(
        digest[index] ^ envelope[NERI_IR_DIGEST_OFFSET_V1 + index]);
  }
  if (digest_difference != 0U) {
    fail(malformed_module,
         "Payload SHA-256 digest does not match the envelope.",
         NERI_IR_DIGEST_OFFSET_V1);
  }

  auto value = payload_reader(payload, options, transport_minor).read();
  verify_supported_module(value);
  return verified_module(std::move(value));
}

} // namespace neri::codegen
