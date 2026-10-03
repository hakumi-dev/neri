#include "managed_storage.h"
#include "../platform/host.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

struct managed_storage::slab final {
  slab *next;
  slab *available_next;
  void *free_slot;
  std::size_t category;
  std::size_t bump;
  std::size_t live;
};

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

std::size_t managed_storage::slot_stride(std::size_t index) {
#if defined(NERI_STORAGE_ASAN)
  // Preserve an inaccessible gap even between fully occupied adjacent slots.
  return slot_bytes[index] + alignof(std::max_align_t);
#else
  return slot_bytes[index];
#endif
}

std::size_t managed_storage::first_slot() {
  constexpr auto alignment = alignof(std::max_align_t);
  return (sizeof(slab) + alignment - 1) & ~(alignment - 1);
}

bool managed_storage::has_space(const slab &block) {
  return block.free_slot != nullptr ||
      block.bump <= block_bytes - slot_stride(block.category);
}

void *managed_storage::allocate(std::size_t bytes) {
  const auto category = size_class(bytes);
  if (category == slot_bytes.size()) return std::calloc(1, bytes);
  auto *block = available_[category];
  if (block == nullptr) {
    void *memory = platform::allocate(block_bytes, block_bytes, false);
    if (memory == nullptr) return nullptr;
    block = new (memory) slab{blocks_, nullptr, nullptr, category, first_slot(), 0};
    blocks_ = block;
    available_[category] = block;
    poison(static_cast<unsigned char *>(memory) + first_slot(), block_bytes - first_slot());
  }
  void *result;
  if (block->free_slot != nullptr) {
    result = block->free_slot;
    unpoison(result, sizeof(void *));
    block->free_slot = *static_cast<void **>(result);
  } else {
    result = reinterpret_cast<unsigned char *>(block) + block->bump;
    block->bump += slot_stride(category);
  }
  ++block->live;
  if (!has_space(*block)) {
    available_[category] = block->available_next;
    block->available_next = nullptr;
  }
  unpoison(result, bytes);
  std::memset(result, 0, bytes);
  return result;
}

void managed_storage::release(void *pointer, std::size_t bytes) {
  if (pointer == nullptr) return;
  const auto category = size_class(bytes);
  if (category == slot_bytes.size()) {
    std::free(pointer);
    return;
  }
  auto *block = reinterpret_cast<slab *>(
      reinterpret_cast<std::uintptr_t>(pointer) & ~(block_bytes - 1));
  assert(block->category == category && block->live != 0);
  *static_cast<void **>(pointer) = block->free_slot;
  block->free_slot = pointer;
  --block->live;
  poison(pointer, slot_stride(category));
}

void managed_storage::free_block(slab *block) {
  unpoison(block, block_bytes);
  platform::deallocate(block);
}

void managed_storage::collect_empty() {
  available_.fill(nullptr);
  for (auto **link = &blocks_; *link != nullptr;) {
    auto *block = *link;
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
}

void managed_storage::adopt(managed_storage &other) {
  assert(this != &other);
  if (other.blocks_ != nullptr) {
    auto *tail = other.blocks_;
    while (tail->next != nullptr) tail = tail->next;
    tail->next = blocks_;
    blocks_ = std::exchange(other.blocks_, nullptr);
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
  available_.fill(nullptr);
}

} // namespace neri::runtime
