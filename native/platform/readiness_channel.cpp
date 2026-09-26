#include "readiness_channel.h"
#include "neri/runtime_abi.h"

#include <cerrno>
#include <system_error>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include "posix_launch.h"
#include <fcntl.h>
#include <unistd.h>
#endif

namespace neri::platform {
namespace {
void close_descriptor(int64_t value) {
  if (value < 0) return;
#ifdef _WIN32
  closesocket(static_cast<SOCKET>(value));
#else
  close(static_cast<int>(value));
#endif
}
[[noreturn]] void channel_failure() {
  static constexpr uint8_t message[] = "Worker readiness channel failed";
  const neri_panic_v1 panic{NERI_PANIC_RUNTIME_CONTRACT_V1, 0, message, sizeof(message) - 1};
  neri_rt_v1_panic(&panic);
}
#ifdef _WIN32
[[noreturn]] void socket_error(const char *operation) {
  throw std::system_error(WSAGetLastError(), std::system_category(), operation);
}
int64_t open_socket() {
  const auto result = neri_rt_v1_net_open(); // Shared process Winsock startup.
  if (result < 0) socket_error("worker readiness socket");
  if (!SetHandleInformation(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(result)), HANDLE_FLAG_INHERIT, 0)) {
    const auto error = GetLastError();
    close_descriptor(result);
    throw std::system_error(static_cast<int>(error), std::system_category(), "worker readiness noninheritance");
  }
  return result;
}
struct temporary_socket {
  int64_t value = open_socket();
  ~temporary_socket() { close_descriptor(value); }
};
#endif
}
readiness_channel::~readiness_channel() {
  close_descriptor(reader_);
  close_descriptor(writer_);
}
void readiness_channel::open() {
#ifdef _WIN32
  temporary_socket listener;
  const int exclusive = 1;
  if (setsockopt(static_cast<SOCKET>(listener.value), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) != 0) socket_error("worker readiness exclusive bind");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(static_cast<SOCKET>(listener.value), reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
      listen(static_cast<SOCKET>(listener.value), 1) != 0) socket_error("worker readiness listen");
  int size = sizeof(address);
  if (getsockname(static_cast<SOCKET>(listener.value), reinterpret_cast<sockaddr *>(&address), &size) != 0) socket_error("worker readiness address");
  writer_ = open_socket();
  if (neri_rt_v1_net_connect_timeout(writer_, ntohs(address.sin_port), 1000) != 0) socket_error("worker readiness connect");
  const int immediate = 1;
  if (setsockopt(static_cast<SOCKET>(writer_), IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char *>(&immediate), sizeof(immediate)) != 0) socket_error("worker readiness no delay");
  sockaddr_in peer{};
  size = sizeof(peer);
  const auto accepted = accept(static_cast<SOCKET>(listener.value), reinterpret_cast<sockaddr *>(&peer), &size);
  if (accepted == INVALID_SOCKET) socket_error("worker readiness accept");
  reader_ = static_cast<int64_t>(accepted);
  sockaddr_in local{};
  size = sizeof(local);
  if (getsockname(static_cast<SOCKET>(writer_), reinterpret_cast<sockaddr *>(&local), &size) != 0) socket_error("worker readiness peer");
  if (peer.sin_family != AF_INET || peer.sin_port != local.sin_port || peer.sin_addr.s_addr != local.sin_addr.s_addr)
    throw std::system_error(WSAECONNABORTED, std::system_category(), "worker readiness peer mismatch");
  if (!SetHandleInformation(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(reader_)), HANDLE_FLAG_INHERIT, 0))
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "worker readiness noninheritance");
  u_long nonblocking = 1;
  if (ioctlsocket(static_cast<SOCKET>(reader_), FIONBIO, &nonblocking) != 0 ||
      ioctlsocket(static_cast<SOCKET>(writer_), FIONBIO, &nonblocking) != 0) socket_error("worker readiness nonblocking");
#else
  int descriptors[2] = {-1, -1};
  int error = 0;
  if (!posix_pipe_cloexec(descriptors, error)) throw std::system_error(error, std::generic_category(), "worker readiness pipe");
  reader_ = descriptors[0];
  writer_ = descriptors[1];
  for (const int descriptor : descriptors) {
    if (fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0)
      throw std::system_error(errno, std::generic_category(), "worker readiness configure");
  }
#endif
  opened_ = true;
}
void readiness_channel::set(bool ready) {
  if (!opened_ || ready == notified_) return;
  char byte = 1;
  for (;;) {
#ifdef _WIN32
    const int count = ready ? send(static_cast<SOCKET>(writer_), &byte, 1, 0)
                            : recv(static_cast<SOCKET>(reader_), &byte, 1, 0);
    if (count == 1) break;
    const int error = WSAGetLastError();
    if (count == SOCKET_ERROR && error == WSAEINTR) continue;
    if (count == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
      // A sent TCP byte may still be in transit. Keep the notification owned
      // until a later poll reconciles and drains it. New work reuses that byte.
      if (!ready) return;
    }
#else
    const auto count = ready ? write(static_cast<int>(writer_), &byte, 1)
                            : read(static_cast<int>(reader_), &byte, 1);
    if (count == 1) break;
    if (count < 0 && errno == EINTR) continue;
    if (!ready && count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
#endif
    channel_failure();
  }
  notified_ = ready;
}
} // namespace neri::platform
