#include "neri/codegen/emitter.h"
#include "neri/codegen/reader.h"
#include "neri/ir_transport.h"

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
    require(error.code() == expected_code,
            "reader returned a non-canonical diagnostic code");
    return;
  }
  throw std::runtime_error("reader accepted invalid Neri IR");
}

void test_primitive_codegen(const std::vector<std::uint8_t> &bytes) {
  const auto input = neri::codegen::read_verified_module(bytes);
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

  try {
    static_cast<void>(neri::codegen::parse_target("invalid-triple"));
  } catch (const neri::codegen::codegen_error &error) {
    require(error.code() == "NCG001",
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
  write_u16(malformed, NERI_IR_VERSION_MINOR_OFFSET_V1, 2U);
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
    static_cast<void>(neri::codegen::read_verified_module(bytes));
    test_envelope_rejections(bytes);
    test_payload_rejections(bytes);
    test_primitive_codegen(read_hex(argv[2]));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Neri codegen test failed: " << error.what() << '\n';
    return 1;
  }
}
