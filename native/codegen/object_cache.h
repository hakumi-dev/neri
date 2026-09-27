#ifndef NERI_CODEGEN_OBJECT_CACHE_H
#define NERI_CODEGEN_OBJECT_CACHE_H
#include "neri/codegen/emitter.h"
#include <memory>
namespace neri::codegen {
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
