#pragma once

#include "neri/runtime_abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace neri::runtime {

// Nonmoving storage. Small objects are contiguous; their size and mark bits
// live in a separate compact table, with ownership shared by the whole slab.
class managed_storage final {
public:
  using visitor = void (*)(neri_ref_v1, std::size_t, void *);
  struct reclaimed final {
    std::uint64_t objects = 0;
    std::uint64_t bytes = 0;
  };

  managed_storage() = default;
  ~managed_storage();
  managed_storage(const managed_storage &) = delete;
  managed_storage &operator=(const managed_storage &) = delete;
  managed_storage(managed_storage &&other) noexcept;
  managed_storage &operator=(managed_storage &&other) noexcept;

  [[nodiscard]] neri_ref_v1 allocate(std::size_t bytes, const void *owner,
                                    bool zeroed = true);
  [[nodiscard]] static const void *owner(neri_ref_v1 object);
  [[nodiscard]] static std::size_t size(const neri_object_header_v1 *object);
  // Return true only on the first mark in a collection.
  [[nodiscard]] static bool mark(neri_ref_v1 object);
  [[nodiscard]] static bool marked(neri_ref_v1 object);
  reclaimed sweep(visitor on_reclaim = nullptr, void *context = nullptr);
  void visit(visitor callback, void *context) const;
  void adopt(managed_storage &other, const void *new_owner = nullptr);
  void clear();

private:
  static constexpr std::uint16_t allocated_bit = 1;
  static constexpr std::uint16_t marked_bit = 2;
  struct entry final {
    std::uint16_t bytes; // The next free index when flags are zero.
    std::uint16_t flags; // Allocation, mark, then object offset / 16.
  };
  struct slab final {
    slab *next;
    slab *available_next;
    const void *owner;
    std::size_t category;
    std::uint16_t capacity;
    std::uint16_t bump;
    std::uint16_t live;
    std::uint16_t free_slot;
    std::size_t object_offset;
  };
  struct alignas(std::max_align_t) large final {
    large *next;
    const void *owner;
    std::size_t bytes;
    bool marked;

    neri_ref_v1 object() { return reinterpret_cast<neri_ref_v1>(this + 1); }
  };
  static constexpr std::size_t block_bytes = 16 * 1024;
  static constexpr std::array<std::size_t, 6> slot_bytes{16, 32, 48, 64, 128, 256};
  static std::size_t size_class(std::size_t bytes);
  static std::size_t slot_stride(std::size_t category);
  static bool has_space(const slab &block);
  static entry *entries(slab *block);
  static neri_ref_v1 object_at(slab *block, std::size_t index);
  static slab *block_of(const entry *metadata);
  static entry *small_metadata(const neri_object_header_v1 *object);
  static large *large_metadata(const neri_object_header_v1 *object);
  static void free_block(slab *block);

  slab *blocks_ = nullptr;
  large *large_ = nullptr;
  std::array<slab *, slot_bytes.size()> available_{};
};

inline managed_storage::slab *managed_storage::block_of(const entry *metadata) {
  return reinterpret_cast<slab *>(reinterpret_cast<std::uintptr_t>(metadata) &
                                  ~(block_bytes - 1));
}

inline managed_storage::entry *managed_storage::small_metadata(const neri_object_header_v1 *object) {
  return reinterpret_cast<entry *>(object->runtime_word & ~std::uintptr_t(1));
}

inline managed_storage::large *managed_storage::large_metadata(const neri_object_header_v1 *object) {
  return reinterpret_cast<large *>(object->runtime_word);
}

inline const void *managed_storage::owner(neri_ref_v1 object) {
  if (object == nullptr || object->runtime_word == 0) return nullptr;
  if ((object->runtime_word & 1) != 0) {
    const auto *metadata = small_metadata(object);
    const auto *block = block_of(metadata);
    const auto address = reinterpret_cast<std::uintptr_t>(block) +
        static_cast<std::uintptr_t>(metadata->flags >> 2) * 16;
    return (metadata->flags & allocated_bit) != 0 && address == reinterpret_cast<std::uintptr_t>(object)
        ? block->owner : nullptr;
  }
  auto *metadata = large_metadata(object);
  return metadata->object() == object ? metadata->owner : nullptr;
}

inline std::size_t managed_storage::size(const neri_object_header_v1 *object) {
  return (object->runtime_word & 1) != 0 ? small_metadata(object)->bytes
                                       : large_metadata(object)->bytes;
}

inline bool managed_storage::mark(neri_ref_v1 object) {
  if ((object->runtime_word & 1) != 0) {
    auto *metadata = small_metadata(object);
    const auto first = (metadata->flags & marked_bit) == 0;
    metadata->flags |= marked_bit;
    return first;
  }
  return !std::exchange(large_metadata(object)->marked, true);
}

inline bool managed_storage::marked(neri_ref_v1 object) {
  return (object->runtime_word & 1) != 0
      ? (small_metadata(object)->flags & marked_bit) != 0
      : large_metadata(object)->marked;
}

} // namespace neri::runtime
