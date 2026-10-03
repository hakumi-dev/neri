#pragma once

#include <array>
#include <cstddef>

namespace neri::runtime {

// Heap-local storage for nonmoving managed objects. Tracing and ownership stay
// in the runtime; this layer only groups small reservations and recycles slots.
class managed_storage final {
public:
  managed_storage() = default;
  ~managed_storage();
  managed_storage(const managed_storage &) = delete;
  managed_storage &operator=(const managed_storage &) = delete;
  managed_storage(managed_storage &&other) noexcept;
  managed_storage &operator=(managed_storage &&other) noexcept;

  [[nodiscard]] void *allocate(std::size_t bytes);
  void release(void *pointer, std::size_t bytes);
  // Release is used during sweep/shutdown. Rebuild allocation lists after sweep.
  void collect_empty();
  void adopt(managed_storage &other);
  void clear();

private:
  struct slab;
  static constexpr std::size_t block_bytes = 16 * 1024;
  static constexpr std::array<std::size_t, 4> slot_bytes{64, 96, 128, 256};
  static std::size_t size_class(std::size_t bytes);
  static std::size_t slot_stride(std::size_t index);
  static std::size_t first_slot();
  static bool has_space(const slab &block);
  static void free_block(slab *block);

  slab *blocks_ = nullptr;
  std::array<slab *, slot_bytes.size()> available_{};
};

} // namespace neri::runtime
