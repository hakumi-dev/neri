#include "neri/codegen/emitter.h"
#include "neri/codegen/reader.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NERI_TOOLCHAIN_VERSION
#error "NERI_TOOLCHAIN_VERSION must be defined by the build."
#endif

namespace {

constexpr std::size_t max_ir_size = 256U * 1024U * 1024U;

struct arguments final {
  std::filesystem::path input;
  std::string input_format;
  neri::codegen::target_platform target{};
  neri::codegen::optimization_mode optimization{};
  neri::codegen::output_kind kind{};
  std::filesystem::path output;
  std::filesystem::path metrics;
};

[[noreturn]] void usage_error(std::string message) {
  throw std::invalid_argument(std::move(message));
}

void print_usage(std::ostream &stream) {
  stream << "Usage: neri-codegen --input <path> --input-format "
            "<binary|hex> --target <macos-arm64|linux-x86_64> "
            "--optimization <debug|release> "
            "--emit <llvm-ir|assembly|object> --output <path|-> "
            "[--metrics <path>]\n";
}

arguments parse_arguments(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    print_usage(std::cout);
    std::exit(0);
  }

  std::string input;
  std::string input_format;
  std::string target;
  std::string optimization;
  std::string emit;
  std::string output;
  std::string metrics;
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) {
      usage_error("Every option requires a value.");
    }
    const std::string_view option(argv[index]);
    auto &destination = [&]() -> std::string & {
      if (option == "--input") {
        return input;
      }
      if (option == "--input-format") {
        return input_format;
      }
      if (option == "--target") {
        return target;
      }
      if (option == "--optimization") {
        return optimization;
      }
      if (option == "--emit") {
        return emit;
      }
      if (option == "--output") {
        return output;
      }
      if (option == "--metrics") {
        return metrics;
      }
      usage_error("Unknown option '" + std::string(option) + "'.");
    }();
    if (!destination.empty()) {
      usage_error("Option '" + std::string(option) + "' was repeated.");
    }
    destination = argv[index + 1];
  }

  if (input.empty() || input_format.empty() || target.empty() ||
      optimization.empty() || emit.empty() || output.empty()) {
    usage_error("Input, input format, target, optimization, emit kind, and "
                "output are all required; host defaults are forbidden.");
  }
  if (input_format != "binary" && input_format != "hex") {
    usage_error("Input format must be binary or hex.");
  }
  const auto kind = neri::codegen::parse_output_kind(emit);
  if (output == "-" && kind == neri::codegen::output_kind::object) {
    usage_error("Object output requires a file path.");
  }
  return {input,
          input_format,
          neri::codegen::parse_target(target),
          neri::codegen::parse_optimization(optimization),
          kind,
          output,
          metrics};
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path,
                                    std::size_t size_limit) {
  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    throw std::runtime_error("Cannot inspect input '" + path.string() +
                             "': " + error.message());
  }
  if (file_size > size_limit) {
    throw std::runtime_error("Input exceeds the bounded reader limit.");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Cannot open input '" + path.string() + "'.");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
  stream.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream || stream.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("Cannot read input '" + path.string() +
                             "' exactly.");
  }
  return bytes;
}

std::uint8_t hex_digit(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>(value - 'A' + 10);
  }
  throw std::runtime_error("Hex input contains a non-hexadecimal character.");
}

std::vector<std::uint8_t> decode_hex(std::span<const std::uint8_t> input) {
  std::vector<std::uint8_t> output;
  output.reserve(input.size() / 2U);
  bool has_high_nibble = false;
  std::uint8_t high_nibble = 0U;
  for (const auto byte : input) {
    const auto character = static_cast<char>(byte);
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    const auto digit = hex_digit(character);
    if (!has_high_nibble) {
      high_nibble = digit;
      has_high_nibble = true;
      continue;
    }
    output.push_back(static_cast<std::uint8_t>((high_nibble << 4U) | digit));
    has_high_nibble = false;
    if (output.size() > max_ir_size) {
      throw std::runtime_error("Decoded IR exceeds the bounded reader limit.");
    }
  }
  if (has_high_nibble) {
    throw std::runtime_error("Hex input contains an incomplete byte.");
  }
  return output;
}

void write_output(const std::filesystem::path &path,
                  const neri::codegen::artifact &value) {
  if (path == "-") {
    if (!value.text) {
      usage_error("Binary artifacts cannot be written to standard output.");
    }
    std::cout.write(reinterpret_cast<const char *>(value.bytes.data()),
                    static_cast<std::streamsize>(value.bytes.size()));
    if (!std::cout) {
      throw std::runtime_error("Cannot write artifact to standard output.");
    }
    return;
  }
  neri::codegen::write_artifact_atomically(path, value.bytes);
}

void write_metrics(const std::filesystem::path &path, std::uint64_t input_ns,
                   std::uint64_t reader_verifier_ns,
                   const neri::codegen::emission_metrics &metrics) {
  if (path.empty()) {
    return;
  }
  std::string json =
      "{\n  \"schemaVersion\": 1,\n  \"inputNs\": " +
      std::to_string(input_ns) +
      ",\n  \"readerVerifierNs\": " + std::to_string(reader_verifier_ns) +
      ",\n  \"loweringAndPreverifyNs\": " +
      std::to_string(metrics.lowering_and_preverify_ns) +
      ",\n  \"llvmOptimizationAndPostverifyNs\": " +
      std::to_string(metrics.llvm_optimization_and_postverify_ns) +
      ",\n  \"targetCodegenNs\": " +
      std::to_string(metrics.target_codegen_ns) + "\n}\n";
  neri::codegen::write_artifact_atomically(
      path,
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t *>(json.data()), json.size()));
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::cout << NERI_TOOLCHAIN_VERSION << '\n';
      return 0;
    }
    const auto options = parse_arguments(argc, argv);
    const auto input_started = std::chrono::steady_clock::now();
    const auto physical_size_limit = options.input_format == "hex"
                                         ? max_ir_size * 2U
                                         : max_ir_size;
    auto bytes = read_file(options.input, physical_size_limit);
    if (options.input_format == "hex") {
      bytes = decode_hex(bytes);
    }
    const auto reader_started = std::chrono::steady_clock::now();
    const auto module = neri::codegen::read_verified_module(bytes);
    const auto emission_started = std::chrono::steady_clock::now();
    neri::codegen::emission_metrics metrics;
    write_output(options.output,
                 neri::codegen::emit_module(module, options.target,
                                              options.optimization,
                                              options.kind, &metrics));
    write_metrics(
        options.metrics,
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                reader_started - input_started)
                .count()),
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                emission_started - reader_started)
                .count()),
        metrics);
    return 0;
  } catch (const neri::codegen::reader_error &error) {
    std::cerr << error.code() << " at byte " << error.byte_offset() << ": "
              << error.what() << '\n';
    return 2;
  } catch (const neri::codegen::codegen_error &error) {
    std::cerr << error.code() << ": " << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "NCG001: " << error.what() << '\n';
    print_usage(std::cerr);
    return 2;
  }
}
