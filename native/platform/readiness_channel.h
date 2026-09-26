#ifndef NERI_PLATFORM_READINESS_CHANNEL_H
#define NERI_PLATFORM_READINESS_CHANNEL_H

#include <cstdint>

namespace neri::platform {
/* A coalesced advisory notification. Its owner serializes set() with the state
 * represented by the level and reconciles on polling. A pending TCP byte can
 * arrive after a reset attempt, producing an empty wakeup. set() never waits
 * for socket readiness. The descriptor is borrowed for polling only. */
class readiness_channel final {
public:
  readiness_channel() = default;
  readiness_channel(const readiness_channel &) = delete;
  readiness_channel &operator=(const readiness_channel &) = delete;
  ~readiness_channel();
  void open(); // Throws an OS error before publication on failure.
  void set(bool ready);
  [[nodiscard]] int64_t descriptor() const { return reader_; }
private:
  int64_t reader_ = -1;
  int64_t writer_ = -1;
  bool opened_ = false;
  bool notified_ = false;
};
}
#endif
