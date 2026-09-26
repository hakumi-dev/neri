#include "neri/codegen/emitter.h"
#include "neri/host_path.h"
#if defined(_WIN32)
#include "../platform/windows_support.h"
#endif
#include "neri/codegen/reader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <set>
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

enum class input_format { binary, hex };

struct arguments final {
  std::filesystem::path input;
  input_format format{};
  neri::codegen::target_platform target{};
  neri::codegen::optimization_mode optimization{};
  neri::codegen::output_kind kind{};
  std::filesystem::path output;
  std::filesystem::path metrics;
  std::filesystem::path progress;
  neri::codegen::debug_source_paths debug_sources;
};

[[noreturn]] void usage_error(std::string message) {
  throw std::invalid_argument(std::move(message));
}

void print_usage(std::ostream &stream) {
  stream << "Usage: neri-codegen --input <path> --input-format "
            "<binary|hex> --target <macos-arm64|linux-x86_64|windows-x86_64> "
            "--optimization <debug|release> "
            "--emit <llvm-ir|assembly|object> --output <path|-> "
            "[--metrics <path>] [--progress <path>] [--debug-source <id> <path>]\n";
}

arguments parse_arguments(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    print_usage(std::cout);
    std::exit(0);
  }

  std::string input;
  std::string format_name;
  std::string target;
  std::string optimization;
  std::string emit;
  std::string output;
  std::string metrics;
  std::string progress;
  neri::codegen::debug_source_paths debug_sources;
  for (int index = 1; index < argc; index += 2) {
    const std::string_view option(argv[index]);
    if (option == "--debug-source") {
      if (index + 2 >= argc) {
        usage_error("Debug source requires an ID and a path.");
      }
      if (argv[index + 1][0] == '\0' || argv[index + 2][0] == '\0') {
        usage_error("Debug source ID and path must not be empty.");
      }
      debug_sources.emplace_back(argv[index + 1], argv[index + 2]);
      ++index;
      continue;
    }
    if (index + 1 >= argc) {
      usage_error("Every option requires a value.");
    }
    auto &destination = [&]() -> std::string & {
      if (option == "--input") {
        return input;
      }
      if (option == "--input-format") {
        return format_name;
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
      if (option == "--progress") {
        return progress;
      }
      usage_error("Unknown option '" + std::string(option) + "'.");
    }();
    if (!destination.empty()) {
      usage_error("Option '" + std::string(option) + "' was repeated.");
    }
    destination = argv[index + 1];
  }

  if (input.empty() || format_name.empty() || target.empty() ||
      optimization.empty() || emit.empty() || output.empty()) {
    usage_error("Input, input format, target, optimization, emit kind, and "
                "output are all required; host defaults are forbidden.");
  }
  if (format_name != "binary" && format_name != "hex") {
    usage_error("Input format must be binary or hex.");
  }
  const auto kind = neri::codegen::parse_output_kind(emit);
  if (output == "-" && kind == neri::codegen::output_kind::object) {
    usage_error("Object output requires a file path.");
  }
  std::set<std::string> debug_source_ids;
  for (const auto &[id, ignored] : debug_sources) {
    static_cast<void>(ignored);
    if (!debug_source_ids.insert(id).second) {
      usage_error("Debug source ID was repeated.");
    }
  }
  return {neri::host_path(input),
          format_name == "hex" ? input_format::hex : input_format::binary,
          neri::codegen::parse_target(target),
          neri::codegen::parse_optimization(optimization),
          kind,
          neri::host_path(output),
          neri::host_path(metrics),
          neri::host_path(progress),
          std::move(debug_sources)};
}

std::string progress_json_string(std::string_view value) {
  std::string result = "\"";
  auto limit = std::min<std::size_t>(value.size(), 160U);
  if (limit < value.size()) {
    while (limit > 0 && (static_cast<unsigned char>(value[limit]) & 0xC0U) == 0x80U)
      --limit;
  }
  for (const auto character : value.substr(0, limit)) {
    const auto byte = static_cast<unsigned char>(character);
    if (character == '"' || character == '\\') {
      result += '\\';
      result += character;
    } else if (byte < 32U || byte == 127U) {
      result += '?';
    } else {
      result += character;
    }
  }
  result += '"';
  return result;
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
#if defined(_WIN32)
    neri::windows::command_arguments arguments;
    argc = static_cast<int>(arguments.storage.size());
    argv = arguments.pointers.data();
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::cout << NERI_TOOLCHAIN_VERSION << '\n';
      return 0;
    }
    const auto options = parse_arguments(argc, argv);
    const auto input_started = std::chrono::steady_clock::now();
    const auto physical_size_limit = options.format == input_format::hex
                                         ? max_ir_size * 2U
                                         : max_ir_size;
    auto bytes = read_file(options.input, physical_size_limit);
    if (options.format == input_format::hex) {
      bytes = decode_hex(bytes);
    }
    const auto reader_started = std::chrono::steady_clock::now();
    const auto module = neri::codegen::read_verified_module(bytes);
    const auto emission_started = std::chrono::steady_clock::now();
    neri::codegen::emission_metrics metrics;
    std::ofstream progress_file;
    std::size_t progress_bytes = 0;
    if (!options.progress.empty())
      progress_file.open(options.progress, std::ios::binary | std::ios::trunc);
    const neri::codegen::emission_progress progress =
        [&](std::string_view phase, std::size_t completed, std::size_t total,
            std::string_view detail, std::size_t source_offset) {
          if (!progress_file || progress_bytes >= 1048576U) return;
          const auto line = "{\"schemaVersion\":1,\"phase\":" +
                            progress_json_string(phase) +
                            ",\"completed\":" + std::to_string(completed) +
                            ",\"total\":" + std::to_string(total) +
                            ",\"detail\":" + progress_json_string(detail) +
                            ",\"sourceOffset\":" + std::to_string(source_offset) + "}\n";
          if (progress_bytes + line.size() > 1048576U) return;
          progress_file << line << std::flush;
          progress_bytes += line.size();
        };
    write_output(options.output,
                 neri::codegen::emit_module(module, options.target,
                                              options.optimization,
                                              options.kind, &metrics,
                                              options.debug_sources,
                                              options.progress.empty() ? neri::codegen::emission_progress{} : progress));
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
    std::cerr << diagnostic_code(error.code()) << " at byte " << error.byte_offset() << ": "
              << error.what() << '\n';
    return 2;
  } catch (const neri::codegen::codegen_error &error) {
    std::cerr << diagnostic_code(error.code()) << ": " << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << diagnostic_code(neri::codegen::codegen_error_kind::driver) << ": " << error.what() << '\n';
    print_usage(std::cerr);
    return 2;
  }
}
