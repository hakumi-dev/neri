#include "neri/runtime_abi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/resource.h>

namespace {
constexpr std::size_t payload_bytes = 64 * 1024;
constexpr std::size_t allocations = 1024;
const neri_type_descriptor_v1 block_type = {
    sizeof(neri_type_descriptor_v1), NERI_RUNTIME_ABI_MAJOR,
    NERI_RUNTIME_ABI_MINOR, NERI_TYPE_KIND_CLASS_V1, 0,
    payload_bytes, 8, 0, 0, nullptr, nullptr, "gc-pressure-block", 0,
    nullptr, nullptr, NERI_SCALAR_KIND_NONE_V1, 0};

neri_ref_v1 allocate() {
  return neri_rt_v1_gc_alloc(&block_type, payload_bytes, 8);
}

unsigned char *payload(neri_ref_v1 value) {
  return reinterpret_cast<unsigned char *>(value) + sizeof(neri_object_header_v1);
}

neri_gc_stats_v1 stats() {
  neri_gc_stats_v1 result{sizeof(neri_gc_stats_v1), 0, 0, 0, 0, 0};
  neri_rt_v1_gc_get_stats(&result);
  return result;
}
}

int main() {
  const neri_runtime_abi_requirements_v1 requirements = {
      sizeof(neri_runtime_abi_requirements_v1), NERI_RUNTIME_ABI_MAJOR,
      NERI_RUNTIME_ABI_MINOR, NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_ROOT_FRAMES};
  if (neri_rt_v1_initialize(&requirements) != NERI_ABI_STATUS_OK_V1) return 1;
  neri_ref_v1 root = nullptr;
  neri_gc_root_frame_v1 frame{nullptr, &root, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&frame);
  root = allocate();
  payload(root)[0] = 42;
  payload(root)[payload_bytes - 1] = 73;
  std::vector<double> samples;
  samples.reserve(allocations);
  uint64_t peak_heap = 0;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < allocations; ++index) {
    const auto before = std::chrono::steady_clock::now();
    const auto value = allocate();
    // Touch every page: RSS must reflect the workload, not lazy zero pages.
    for (std::size_t offset = 0; offset < payload_bytes; offset += 4096)
      payload(value)[offset] = static_cast<unsigned char>(index);
    samples.push_back(std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - before).count());
    peak_heap = std::max(peak_heap, stats().managed_byte_count);
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  const auto automatic_collections = stats().collection_count;
  const bool retained = payload(root)[0] == 42 && payload(root)[payload_bytes - 1] == 73;
  neri_rt_v1_gc_collect();
  const bool exact_live_set = stats().managed_object_count == 1;
  root = nullptr;
  neri_rt_v1_gc_collect();
  const bool reclaimed = stats().managed_byte_count == 0;
  neri_rt_v1_gc_root_frame_leave(&frame);
  neri_rt_v1_shutdown();
  std::sort(samples.begin(), samples.end());
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 1;
#if defined(__APPLE__)
  const auto rss_bytes = usage.ru_maxrss;
#else
  const auto rss_bytes = usage.ru_maxrss * 1024;
#endif
  std::printf("{\"workload\":\"gc-pressure\",\"allocated_payload_bytes\":%zu,"
      "\"peak_managed_bytes\":%llu,\"automatic_collections\":%llu,\"peak_rss_bytes\":%ld,"
      "\"seconds\":%.6f,\"allocation_us_p50\":%.3f,\"allocation_us_p95\":%.3f,"
      "\"allocation_us_p99\":%.3f,\"allocation_us_max\":%.3f}\n",
      allocations * payload_bytes, static_cast<unsigned long long>(peak_heap),
      static_cast<unsigned long long>(automatic_collections), rss_bytes, elapsed,
      samples[allocations / 2], samples[allocations * 95 / 100],
      samples[allocations * 99 / 100], samples.back());
  // A long-lived process must reclaim garbage before allocation failure. This
  // budget allows policy changes while ruling out retention of the whole workload.
  return retained && exact_live_set && reclaimed && automatic_collections > 0 &&
      peak_heap < 16 * 1024 * 1024 ? 0 : 1;
}
