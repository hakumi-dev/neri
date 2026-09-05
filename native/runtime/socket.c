#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
// Platform layouts and constants only. Ownership, retries and deadlines live in Neri.
#include "neri/runtime_abi.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t io_result(int64_t result) {
  if (result >= 0) return result;
  return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
                 errno == ECONNABORTED ? -2 : -1;
}

int64_t neri_rt_v1_net_open(void) { return socket(AF_INET, SOCK_STREAM, 0); }
int64_t neri_rt_v1_net_configure(int64_t fd) {
  if (fcntl((int)fd, F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl((int)fd, F_SETFL, O_NONBLOCK) < 0) return -1;
  const int enabled = 1;
#if defined(__APPLE__)
  if (setsockopt((int)fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) return -1;
#endif
  return setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
}
int64_t neri_rt_v1_net_bind(int64_t fd, int64_t port) {
  if (port < 1 || port > 65535) { errno = EINVAL; return -1; }
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return bind((int)fd, (struct sockaddr *)&address, sizeof(address));
}
int64_t neri_rt_v1_net_listen(int64_t fd) { return listen((int)fd, 16); }
int64_t neri_rt_v1_net_connect(int64_t fd, int64_t port) {
  if (port < 1 || port > 65535) { errno = EINVAL; return -1; }
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return connect((int)fd, (struct sockaddr *)&address, sizeof(address));
}
int64_t neri_rt_v1_net_accept(int64_t fd) {
  return io_result(accept((int)fd, NULL, NULL));
}
int64_t neri_rt_v1_net_poll(int64_t fd, int64_t writing, int64_t milliseconds) {
  if (fd < 0 || fd > INT_MAX || milliseconds < -1 || milliseconds > INT_MAX) {
    errno = EINVAL; return -1;
  }
  struct pollfd item = {(int)fd, (short)(writing ? POLLOUT : POLLIN), 0};
  const int result = poll(&item, 1, (int)milliseconds);
  if (result > 0 && (item.revents & POLLNVAL)) { errno = EBADF; return -1; }
  return io_result(result);
}
int64_t neri_rt_v1_net_read(int64_t fd, uint8_t *bytes, int64_t length) {
  if (length < 0) { errno = EINVAL; return -1; }
  return io_result(recv((int)fd, bytes, (size_t)length, 0));
}
int64_t neri_rt_v1_net_write(int64_t fd, uint8_t *bytes, int64_t length) {
  if (length < 0) { errno = EINVAL; return -1; }
#if defined(__APPLE__)
  const int flags = 0;
#else
  const int flags = MSG_NOSIGNAL;
#endif
  return io_result(send((int)fd, bytes, (size_t)length, flags));
}
void neri_rt_v1_net_close(int64_t fd) { close((int)fd); }
int64_t neri_rt_v1_net_milliseconds(void) {
  struct timespec now = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
  return now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000;
}
int64_t neri_rt_v1_net_error(uint8_t *bytes, int64_t capacity) {
  const char *message = strerror(errno);
  if (capacity < 0) return 0;
  const size_t length = strlen(message) < (size_t)capacity ? strlen(message) : (size_t)capacity;
  memcpy(bytes, message, length);
  return (int64_t)length;
}
