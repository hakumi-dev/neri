#ifndef NERI_CODEGEN_READER_H
#define NERI_CODEGEN_READER_H

#include "neri/codegen/ir.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace neri::codegen {

struct reader_options final {
  std::size_t max_input_bytes = 256U * 1024U * 1024U;
  std::size_t max_string_bytes = 16U * 1024U * 1024U;
  std::uint32_t max_collection_count = 1'000'000U;
  std::uint32_t max_nesting_depth = 64U;
};

class reader_error final : public std::runtime_error {
public:
  reader_error(std::string code, std::string message, std::size_t byte_offset);

  [[nodiscard]] const std::string &code() const noexcept;
  [[nodiscard]] std::size_t byte_offset() const noexcept;

private:
  std::string code_;
  std::size_t byte_offset_{};
};

class verified_module final {
public:
  verified_module(const verified_module &) = delete;
  verified_module &operator=(const verified_module &) = delete;
  verified_module(verified_module &&) noexcept = default;
  verified_module &operator=(verified_module &&) noexcept = default;

  [[nodiscard]] const ir_module &value() const noexcept;

private:
  explicit verified_module(ir_module value);

  ir_module value_;

  friend verified_module read_verified_module(std::span<const std::uint8_t>,
                                               const reader_options &);
};

[[nodiscard]] verified_module
read_verified_module(std::span<const std::uint8_t> input,
                     const reader_options &options = {});

} // namespace neri::codegen

#endif
