#include "neri/codegen/emitter.h"
#include "neri/codegen/reader.h"
#include "neri/ir_transport.h"
#include "../../native/codegen/ir_verifier.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/SHA256.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::uint8_t hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>(value - 'A' + 10);
  }
  throw std::runtime_error("golden vector contains non-hexadecimal text");
}

[[nodiscard]] std::vector<std::uint8_t> read_hex(const char *path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not open Neri IR golden vector");
  }

  std::string digits;
  for (char value; input.get(value);) {
    if (std::isspace(static_cast<unsigned char>(value)) == 0) {
      digits.push_back(value);
    }
  }
  if (digits.size() % 2U != 0U) {
    throw std::runtime_error("golden vector has an incomplete hexadecimal byte");
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(digits.size() / 2U);
  for (std::size_t index = 0; index < digits.size(); index += 2U) {
    bytes.push_back(static_cast<std::uint8_t>(
        (hex_nibble(digits[index]) << 4U) | hex_nibble(digits[index + 1U])));
  }
  return bytes;
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_unsafe_call_boundary() {
  using namespace neri::codegen;
  ir_module module;
  module.semantic_version = {1, 0};
  module.id = "unsafe-call-contract";
  const type capability{NERI_IR_TYPE_UNSAFE_CAPABILITY_V1, std::nullopt, {}};
  function target;
  target.id = {module.id, NERI_IR_SYMBOL_FUNCTION_V1, "aaa_owned"};
  target.kind = NERI_IR_FUNCTION_V1;
  target.result_type.tag = NERI_IR_TYPE_VOID_V1;
  target.unsafe_call = true;
  target.unsafe_root = value_definition{0, capability};
  block body;
  body.ending.tag = NERI_IR_TERMINATOR_RETURN_V1;
  target.blocks.push_back(body);
  module.functions.push_back(target);

  function caller;
  caller.id = {module.id, NERI_IR_SYMBOL_FUNCTION_V1, "bbb_caller"};
  caller.kind = NERI_IR_FUNCTION_V1;
  caller.result_type.tag = NERI_IR_TYPE_VOID_V1;
  caller.effects = NERI_IR_EFFECT_UNSAFE_V1;
  instruction begin;
  begin.opcode = NERI_IR_OPCODE_UNSAFE_BEGIN_V1;
  begin.results.push_back({0, capability});
  instruction call;
  call.opcode = NERI_IR_OPCODE_CALL_UNSAFE_V1;
  call.operands.push_back(0);
  call.symbol = target.id;
  instruction end;
  end.opcode = NERI_IR_OPCODE_UNSAFE_END_V1;
  end.operands.push_back(0);
  body.instructions = {begin, call, end};
  caller.blocks.push_back(body);
  module.functions.push_back(caller);
  verify_supported_module(module);

  auto rejected = [](const ir_module &candidate) {
    try {
      verify_supported_module(candidate);
    } catch (const reader_error &) {
      return;
    }
    throw std::runtime_error("unsafe call escaped native capability validation");
  };
  auto malformed = module;
  malformed.functions[1].blocks[0].instructions[1].operands.clear();
  rejected(malformed);
  malformed = module;
  malformed.functions[1].blocks[0].instructions[1].opcode = NERI_IR_OPCODE_CALL_V1;
  malformed.functions[1].blocks[0].instructions[1].operands.clear();
  rejected(malformed);
  malformed = module;
  malformed.functions[0].unsafe_root.reset();
  rejected(malformed);
}

void test_debug_scope_validation() {
  using namespace neri::codegen;
  ir_module module;
  module.semantic_version = {1, 0};
  module.id = "debug-scope-contract";
  module.required_features = {ir_feature::DebugScopes};
  module.sources.push_back({"scope.hk", {'d', 'e', 'f', ' ', 'm', 'a', 'i',
                                          'n', '\n', ' ', ' ', 'l', 'e', 't',
                                          ' ', 'v', '\n'}});
  const source_location function_location{"scope.hk", 0U, 17U};
  const source_location scope_location{"scope.hk", 9U, 8U};
  const source_location local_location{"scope.hk", 11U, 5U};
  const type integer{NERI_IR_TYPE_INT_V1, std::nullopt, {}};
  function body;
  body.id = {module.id, NERI_IR_SYMBOL_FUNCTION_V1, "main"};
  body.result_type.tag = NERI_IR_TYPE_VOID_V1;
  body.kind = NERI_IR_FUNCTION_V1;
  body.location = function_location;
  block entry;
  entry.parameters.push_back({0U, integer, function_location});
  entry.ending.tag = NERI_IR_TERMINATOR_RETURN_V1;
  body.parameter_types.push_back(integer);
  body.blocks.push_back(entry);
  body.debug_scopes.push_back({1U, 0U, scope_location});
  body.debug_locals.push_back({"value", 0U, 1U, local_location});
  module.functions.push_back(body);

  verify_supported_module(module);

  const auto rejected = [](const ir_module &candidate) {
    try {
      verify_supported_module(candidate);
    } catch (const reader_error &) {
      return;
    }
    throw std::runtime_error("invalid debug scope escaped verification");
  };
  auto malformed = module;
  malformed.functions[0].debug_scopes[0].parent_id = 2U;
  rejected(malformed);
  malformed = module;
  malformed.functions[0].blocks[0].ending.debug_scope_id = 2U;
  rejected(malformed);
  malformed = module;
  malformed.functions[0].debug_locals[0].scope_id = 2U;
  rejected(malformed);
}

void test_retained_module_lowering() {
  using namespace neri::codegen;
  ir_module module;
  module.semantic_version = {1, 0};
  module.id = "retained-contract";
  module.required_features = {ir_feature::RetainedModules};

  class_declaration retained_class;
  retained_class.id = {module.id, NERI_IR_SYMBOL_CLASS_V1, "Prior"};
  retained_class.access = NERI_IR_ACCESS_INTERNAL_V1;
  retained_class.retained = true;
  module.classes.push_back(retained_class);

  function retained;
  retained.id = {module.id, NERI_IR_SYMBOL_FUNCTION_V1, "prior"};
  retained.result_type.tag = NERI_IR_TYPE_VOID_V1;
  retained.kind = NERI_IR_FUNCTION_V1;
  retained.retained = true;
  function caller;
  caller.id = {module.id, NERI_IR_SYMBOL_FUNCTION_V1, "current"};
  caller.result_type.tag = NERI_IR_TYPE_VOID_V1;
  caller.kind = NERI_IR_FUNCTION_V1;
  block body;
  body.ending.tag = NERI_IR_TERMINATOR_RETURN_V1;
  caller.blocks.push_back(body);
  module.functions.push_back(caller);
  module.functions.push_back(retained);

  verify_supported_module(module);
  auto malformed = module;
  malformed.functions.back().blocks.push_back(body);
  try {
    verify_supported_module(malformed);
  } catch (const reader_error &) {
    return;
  }
  throw std::runtime_error("retained function body escaped verification");
}

void write_u16(std::vector<std::uint8_t> &bytes, std::size_t offset,
               std::uint16_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::vector<std::uint8_t> &bytes, std::size_t offset,
               std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes.at(offset + index) =
        static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void resign(std::vector<std::uint8_t> &bytes) {
  const auto payload = std::span(bytes).subspan(NERI_IR_HEADER_SIZE_V1);
  const auto digest = llvm::SHA256::hash(
      llvm::ArrayRef<std::uint8_t>(payload.data(), payload.size()));
  for (std::size_t index = 0; index < digest.size(); ++index) {
    bytes.at(NERI_IR_DIGEST_OFFSET_V1 + index) = digest[index];
  }
}

void expect_error(std::span<const std::uint8_t> bytes,
                  std::string_view expected_code) {
  try {
    static_cast<void>(neri::codegen::read_verified_module(bytes));
  } catch (const neri::codegen::reader_error &error) {
    require(diagnostic_code(error.code()) == expected_code,
            "reader returned a non-canonical diagnostic code");
    return;
  }
  throw std::runtime_error("reader accepted invalid Neri IR");
}

void test_primitive_codegen(const std::vector<std::uint8_t> &bytes) {
  const auto input = neri::codegen::read_verified_module(bytes);
  const auto debug_ir = neri::codegen::emit_module(
      input, neri::codegen::target_platform::macos_arm64,
      neri::codegen::optimization_mode::debug,
      neri::codegen::output_kind::llvm_ir);
  const std::string debug_text(debug_ir.bytes.begin(), debug_ir.bytes.end());

  require(debug_ir.text &&
              debug_text.find("DILocalVariable(name: \"left\", arg: 1") !=
                  std::string::npos,
          "entry parameters must be emitted as formal debug parameters");

  const auto windows_target = neri::codegen::parse_target("windows-x86_64");
  const auto windows_object = neri::codegen::emit_module(input, windows_target,
      neri::codegen::optimization_mode::release, neri::codegen::output_kind::object);
  const auto repeated_windows_object = neri::codegen::emit_module(input, windows_target,
      neri::codegen::optimization_mode::release, neri::codegen::output_kind::object);
  require(!windows_object.text && windows_object.bytes.size() >= 20 &&
      windows_object.bytes[0] == 0x64 && windows_object.bytes[1] == 0x86 &&
      windows_object.bytes == repeated_windows_object.bytes,
      "object emission is not deterministic Windows x86-64 COFF");
  const auto linux_object = neri::codegen::emit_module(
      input, neri::codegen::target_platform::linux_x86_64,
      neri::codegen::optimization_mode::release,
      neri::codegen::output_kind::object);
  const auto repeated_linux_object = neri::codegen::emit_module(
      input, neri::codegen::target_platform::linux_x86_64,
      neri::codegen::optimization_mode::release,
      neri::codegen::output_kind::object);
  const auto macos_object = neri::codegen::emit_module(
      input, neri::codegen::target_platform::macos_arm64,
      neri::codegen::optimization_mode::release,
      neri::codegen::output_kind::object);
  const auto repeated_macos_object = neri::codegen::emit_module(
      input, neri::codegen::target_platform::macos_arm64,
      neri::codegen::optimization_mode::release,
      neri::codegen::output_kind::object);
  require(!linux_object.text && !macos_object.text &&
              linux_object.bytes.size() >= 4U && macos_object.bytes.size() >= 4U &&
              linux_object.bytes[0] == UINT8_C(0x7f) &&
              linux_object.bytes[1] == static_cast<std::uint8_t>('E') &&
              linux_object.bytes[2] == static_cast<std::uint8_t>('L') &&
              linux_object.bytes[3] == static_cast<std::uint8_t>('F') &&
              macos_object.bytes[0] == UINT8_C(0xcf) &&
              macos_object.bytes[1] == UINT8_C(0xfa) &&
              macos_object.bytes[2] == UINT8_C(0xed) &&
              macos_object.bytes[3] == UINT8_C(0xfe) &&
              linux_object.bytes == repeated_linux_object.bytes &&
              macos_object.bytes == repeated_macos_object.bytes,
          "object emission is not deterministic ELF x86-64 and Mach-O ARM64");

  // Mach-O load commands start after the 32-byte mach_header_64.
  const auto word = [&](std::size_t offset) {
    require(offset + 4 <= macos_object.bytes.size(), "truncated Mach-O command");
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < 4; ++byte)
      value |= std::uint32_t(macos_object.bytes[offset + byte]) << (byte * 8);
    return value;
  };
  bool platform_found = false;
  std::size_t offset = 32;
  for (std::uint32_t command = 0; command < word(16); ++command) {
    const auto size = word(offset + 4);
    require(size >= 8 && size <= macos_object.bytes.size() - offset,
            "invalid Mach-O load command size");
    if (word(offset) == 0x32) { // LC_BUILD_VERSION
      platform_found = word(offset + 8) == 1 && word(offset + 12) == (15U << 16);
    }
    offset += size;
  }
  require(platform_found, "Mach-O object must declare macOS 15 as its platform");

  try {
    static_cast<void>(neri::codegen::parse_target("invalid-triple"));
  } catch (const neri::codegen::codegen_error &error) {
    require(diagnostic_code(error.code()) == "NCG001",
            "invalid target returned a non-canonical diagnostic");
    return;
  }
  throw std::runtime_error("invalid target was accepted");
}

void test_envelope_rejections(const std::vector<std::uint8_t> &valid) {
  for (const auto length : {std::size_t{0}, std::size_t{NERI_IR_HEADER_SIZE_V1 - 1}, valid.size() - 1}) {
    expect_error(std::span(valid).first(length), "NIR004");
  }

  auto malformed = valid;
  malformed[0] ^= UINT8_C(0xff);
  expect_error(malformed, "NIR004");

  malformed = valid;
  write_u16(malformed, NERI_IR_VERSION_MAJOR_OFFSET_V1, 2U);
  expect_error(malformed, "NIR001");

  malformed = valid;
  write_u16(malformed, NERI_IR_VERSION_MINOR_OFFSET_V1, NERI_IR_TRANSPORT_MINOR_V1 + 1U);
  expect_error(malformed, "NIR002");

  malformed = valid;
  write_u32(malformed, NERI_IR_FLAGS_OFFSET_V1, 1U);
  expect_error(malformed, "NIR003");

  malformed = valid;
  malformed.back() ^= UINT8_C(1);
  expect_error(malformed, "NIR004");
}

void test_payload_rejections(const std::vector<std::uint8_t> &valid) {
  auto malformed = valid;
  write_u16(malformed, NERI_IR_HEADER_SIZE_V1, 2U);
  resign(malformed);
  expect_error(malformed, "NIR001");

  malformed = valid;
  write_u16(malformed, NERI_IR_HEADER_SIZE_V1 + 2U, 1U);
  resign(malformed);
  expect_error(malformed, "NIR002");

  malformed = valid;
  // The v1.0 vector starts with u16 versions, a u32 length, then module UTF-8.
  malformed[NERI_IR_HEADER_SIZE_V1 + 8U] = UINT8_C(0xff);
  resign(malformed);
  expect_error(malformed, "NIR010");

  malformed = valid;
  // The feature-vector count follows the 19-byte module name in this wire vector.
  write_u32(malformed, NERI_IR_HEADER_SIZE_V1 + 27U, UINT32_MAX);
  resign(malformed);
  expect_error(malformed, "NIR004");
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3) {
      throw std::runtime_error("expected minimal and primitive IR vector paths");
    }
    const auto bytes = read_hex(argv[1]);
    const auto legacy = neri::codegen::read_verified_module(bytes);
    require(legacy.value().functions.front().debug_scopes.empty(),
            "pre-1.6 transport must not synthesize debug scopes");
    test_envelope_rejections(bytes);
    test_payload_rejections(bytes);
    test_unsafe_call_boundary();
    test_debug_scope_validation();
    test_retained_module_lowering();
    test_primitive_codegen(read_hex(argv[2]));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Neri codegen test failed: " << error.what() << '\n';
    return 1;
  }
}
