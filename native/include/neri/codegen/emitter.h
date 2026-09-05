#ifndef NERI_CODEGEN_EMITTER_H
#define NERI_CODEGEN_EMITTER_H

#include "neri/codegen/reader.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neri::codegen {

enum class target_platform {
  macos_arm64,
  linux_x86_64,
};

enum class optimization_mode {
  debug,
  release,
};

enum class output_kind {
  llvm_ir,
  assembly,
  object,
};

struct artifact final {
  std::vector<std::uint8_t> bytes;
  bool text{};
};

struct emission_metrics final {
  std::uint64_t lowering_and_preverify_ns{};
  std::uint64_t llvm_optimization_and_postverify_ns{};
  std::uint64_t target_codegen_ns{};
};

class codegen_error final : public std::runtime_error {
public:
  codegen_error(std::string code, std::string message);

  [[nodiscard]] const std::string &code() const noexcept;

private:
  std::string code_;
};

[[nodiscard]] target_platform parse_target(std::string_view value);
[[nodiscard]] optimization_mode parse_optimization(std::string_view value);
[[nodiscard]] output_kind parse_output_kind(std::string_view value);
[[nodiscard]] std::string_view target_name(target_platform target) noexcept;
[[nodiscard]] std::string_view target_triple(target_platform target) noexcept;
[[nodiscard]] std::string_view
optimization_name(optimization_mode mode) noexcept;
[[nodiscard]] std::string_view output_kind_name(output_kind kind) noexcept;

[[nodiscard]] artifact emit_module(const verified_module &input,
                                   target_platform target,
                                   optimization_mode optimization,
                                   output_kind kind,
                                   emission_metrics *metrics = nullptr);

void write_artifact_atomically(const std::filesystem::path &path,
                               std::span<const std::uint8_t> bytes);

} // namespace neri::codegen

#endif
