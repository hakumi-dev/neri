#ifndef NERI_CODEGEN_OBJECT_CACHE_H
#define NERI_CODEGEN_OBJECT_CACHE_H
#include "neri/codegen/emitter.h"
#include <memory>
#include <string>
namespace neri::codegen {
// Returns an empty identity when the loaded native closure cannot be verified.
std::string cache_identity();
// Verifies absent paths against the active Darwin shared cache without loading them.
std::string shared_cache_identity(std::span<const std::string> paths);

// Bootstrap-only cache; failures always leave normal emission available.
class object_cache final {
public:
  object_cache(const std::filesystem::path &, std::span<const std::uint8_t>,
               target_platform, optimization_mode);
  ~object_cache();
  bool restore(artifact &);
  void publish(const artifact &);

private:
  struct state;
  std::unique_ptr<state> state_;
};
} // namespace neri::codegen
#endif
