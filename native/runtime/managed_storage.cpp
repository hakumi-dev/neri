#include "managed_storage.h"
#include "../platform/host.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NERI_STORAGE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(NERI_STORAGE_ASAN)
#define NERI_STORAGE_ASAN 1
#endif
#if defined(NERI_STORAGE_ASAN)
#include <sanitizer/asan_interface.h>
#endif

namespace neri::runtime {
namespace {
constexpr std::uint16_t no_slot = UINT16_MAX;
constexpr std::size_t align_up(std::size_t value) {
  static_assert(alignof(std::max_align_t) <= 16);
  return (value + 15) & ~std::size_t(15);
}

void poison(void *pointer, std::size_t bytes) {
#if defined(NERI_STORAGE_ASAN)
  ASAN_POISON_MEMORY_REGION(pointer, bytes);
#else
  static_cast<void>(pointer);
  static_cast<void>(bytes);
#endif
}

void unpoison(void *pointer, std::size_t bytes) {
#if defined(NERI_STORAGE_ASAN)
  ASAN_UNPOISON_MEMORY_REGION(pointer, bytes);
#else
  static_cast<void>(pointer);
  static_cast<void>(bytes);
#endif
}
} // namespace

managed_storage::~managed_storage() { clear(); }

managed_storage::managed_storage(managed_storage &&other) noexcept { adopt(other); }

managed_storage &managed_storage::operator=(managed_storage &&other) noexcept {
  if (this != &other) {
    clear();
    adopt(other);
  }
  return *this;
}

std::size_t managed_storage::size_class(std::size_t bytes) {
  for (std::size_t index = 0; index < slot_bytes.size(); ++index)
    if (bytes <= slot_bytes[index]) return index;
  return slot_bytes.size();
}

std::size_t managed_storage::slot_stride(std::size_t category) {
#if defined(NERI_STORAGE_ASAN)
  return slot_bytes[category] + 16;
#else
  return slot_bytes[category];
#endif
}

bool managed_storage::has_space(const slab &block) {
  return block.free_slot != no_slot || block.bump < block.capacity;
}

managed_storage::entry *managed_storage::entries(slab *block) {
  return reinterpret_cast<entry *>(block + 1);
}

neri_ref_v1 managed_storage::object_at(slab *block, std::size_t index) {
  return reinterpret_cast<neri_ref_v1>(reinterpret_cast<unsigned char *>(block) +
      block->object_offset + index * slot_stride(block->category));
}

neri_ref_v1 managed_storage::allocate(std::size_t bytes, const void *owner, bool zeroed) {
  assert(bytes >= sizeof(neri_object_header_v1) && owner != nullptr);
  const auto category = size_class(bytes);
  if (category == slot_bytes.size()) {
    if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(large)) return nullptr;
    void *memory = zeroed ? std::calloc(1, sizeof(large) + bytes)
                          : std::malloc(sizeof(large) + bytes);
    if (memory == nullptr) return nullptr;
    auto *metadata = new (memory) large{large_, owner, bytes, false};
    large_ = metadata;
    auto *object = metadata->object();
    object->runtime_word = reinterpret_cast<std::uintptr_t>(metadata);
    return object;
  }
  auto *block = available_[category];
  if (block == nullptr) {
    void *memory = platform::allocate(block_bytes, block_bytes, false);
    if (memory == nullptr) return nullptr;
    auto capacity = (block_bytes - sizeof(slab)) / (sizeof(entry) + slot_stride(category));
    while (align_up(sizeof(slab) + capacity * sizeof(entry)) +
               capacity * slot_stride(category) > block_bytes) --capacity;
    block = new (memory) slab{blocks_, nullptr, owner, category,
        static_cast<std::uint16_t>(capacity), 0, 0, no_slot,
        align_up(sizeof(slab) + capacity * sizeof(entry))};
    blocks_ = block;
    available_[category] = block;
    poison(reinterpret_cast<unsigned char *>(block) + block->object_offset,
           block_bytes - block->object_offset);
  }
  assert(block->owner == owner);
  const auto index = block->free_slot != no_slot ? block->free_slot : block->bump++;
  auto *metadata = &entries(block)[index];
  if (block->free_slot != no_slot) block->free_slot = metadata->bytes;
  *metadata = {static_cast<std::uint16_t>(bytes), static_cast<std::uint16_t>(
      allocated_bit | ((block->object_offset + index * slot_stride(category)) / 16) << 2)};
  ++block->live;
  if (!has_space(*block)) {
    available_[category] = block->available_next;
    block->available_next = nullptr;
  }
  auto *object = object_at(block, index);
  unpoison(object, bytes);
  if (zeroed) std::memset(object, 0, bytes);
  object->runtime_word = reinterpret_cast<std::uintptr_t>(metadata) | 1;
  return object;
}

void managed_storage::free_block(slab *block) {
  unpoison(block, block_bytes);
  platform::deallocate(block);
}

managed_storage::reclaimed managed_storage::sweep(visitor on_reclaim, void *context) {
  reclaimed result;
  available_.fill(nullptr);
  for (auto **link = &blocks_; *link != nullptr;) {
    auto *block = *link;
    auto *table = entries(block);
    for (std::size_t index = 0; index < block->bump; ++index) {
      auto &metadata = table[index];
      if ((metadata.flags & allocated_bit) == 0) continue;
      if ((metadata.flags & marked_bit) != 0) {
        metadata.flags &= ~marked_bit;
        continue;
      }
      auto *object = object_at(block, index);
      ++result.objects;
      result.bytes += metadata.bytes;
      if (on_reclaim != nullptr) on_reclaim(object, metadata.bytes, context);
      metadata = {block->free_slot, 0};
      block->free_slot = static_cast<std::uint16_t>(index);
      --block->live;
      poison(object, slot_stride(block->category));
    }
    if (block->live == 0) {
      *link = block->next;
      free_block(block);
      continue;
    }
    block->available_next = nullptr;
    if (has_space(*block)) {
      block->available_next = available_[block->category];
      available_[block->category] = block;
    }
    link = &block->next;
  }
  for (auto **link = &large_; *link != nullptr;) {
    auto *metadata = *link;
    if (metadata->marked) {
      metadata->marked = false;
      link = &metadata->next;
      continue;
    }
    ++result.objects;
    result.bytes += metadata->bytes;
    if (on_reclaim != nullptr) on_reclaim(metadata->object(), metadata->bytes, context);
    *link = metadata->next;
    std::free(metadata);
  }
  return result;
}

void managed_storage::visit(visitor callback, void *context) const {
  for (auto *block = blocks_; block != nullptr; block = block->next)
    for (std::size_t index = 0; index < block->bump; ++index)
      if ((entries(block)[index].flags & allocated_bit) != 0)
        callback(object_at(block, index), entries(block)[index].bytes, context);
  for (auto *metadata = large_; metadata != nullptr; metadata = metadata->next)
    callback(metadata->object(), metadata->bytes, context);
}

void managed_storage::adopt(managed_storage &other, const void *new_owner) {
  assert(this != &other);
  if (other.blocks_ != nullptr) {
    auto *tail = other.blocks_;
    while (true) {
      if (new_owner != nullptr) tail->owner = new_owner;
      if (tail->next == nullptr) break;
      tail = tail->next;
    }
    tail->next = blocks_;
    blocks_ = std::exchange(other.blocks_, nullptr);
  }
  if (other.large_ != nullptr) {
    auto *tail = other.large_;
    while (true) {
      if (new_owner != nullptr) tail->owner = new_owner;
      if (tail->next == nullptr) break;
      tail = tail->next;
    }
    tail->next = large_;
    large_ = std::exchange(other.large_, nullptr);
  }
  for (std::size_t index = 0; index < available_.size(); ++index) {
    if (other.available_[index] == nullptr) continue;
    auto *tail = other.available_[index];
    while (tail->available_next != nullptr) tail = tail->available_next;
    tail->available_next = available_[index];
    available_[index] = std::exchange(other.available_[index], nullptr);
  }
}

void managed_storage::clear() {
  while (blocks_ != nullptr) {
    auto *block = blocks_;
    blocks_ = block->next;
    free_block(block);
  }
  while (large_ != nullptr) {
    auto *metadata = large_;
    large_ = metadata->next;
    std::free(metadata);
  }
  available_.fill(nullptr);
}

} // namespace neri::runtime
