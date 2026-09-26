#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
// POSIX layouts and constants only. Ownership, retries and deadlines live in Neri.
#include "neri/runtime_abi.h"
#include "error_text.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
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
  if (port < 0 || port > 65535) { errno = EINVAL; return -1; }
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return bind((int)fd, (struct sockaddr *)&address, sizeof(address));
}
int64_t neri_rt_v1_net_local_port(int64_t fd) {
  if (fd < 0 || fd > INT_MAX) { errno = EINVAL; return -1; }
  struct sockaddr_in address = {0};
  socklen_t length = sizeof(address);
  if (getsockname((int)fd, (struct sockaddr *)&address, &length) < 0 ||
      length < sizeof(address) || address.sin_family != AF_INET) return -1;
  return ntohs(address.sin_port);
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
int64_t neri_rt_v1_net_connect_timeout(int64_t fd, int64_t port, int64_t milliseconds) {
  if (fd < 0 || fd > INT_MAX || port < 1 || port > 65535 ||
      milliseconds < 0 || milliseconds > INT_MAX) { errno = EINVAL; return -1; }
  if (fcntl((int)fd, F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl((int)fd, F_SETFL, O_NONBLOCK) < 0) return -1;
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect((int)fd, (struct sockaddr *)&address, sizeof(address)) == 0) return 0;
  if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR) return -1;
  struct pollfd item = {(int)fd, POLLOUT, 0};
  struct timespec started = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &started) < 0) return -1;
  const int64_t deadline = started.tv_sec * INT64_C(1000) + started.tv_nsec / 1000000 + milliseconds;
  int ready;
  do {
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    const int64_t remaining = deadline - (now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000);
    if (remaining <= 0) { errno = ETIMEDOUT; return -2; }
    ready = poll(&item, 1, (int)remaining);
  } while (ready < 0 && errno == EINTR);
  if (ready <= 0) { if (ready == 0) { errno = ETIMEDOUT; return -2; } return -1; }
  int error = 0;
  socklen_t length = sizeof(error);
  if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) return -1;
  if (error != 0) { errno = error; return -1; }
  return 0;
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
int64_t neri_rt_v1_net_poll_many(const int64_t *descriptors, const int64_t *interests,
                               int64_t *events, int64_t count, int64_t timeout_ms) {
  const int previous_error = errno;
  if (count < 0 || count > 4096 || (count != 0 && events == NULL)) {
    errno = EINVAL; return -1;
  }
  if (count != 0) memset(events, 0, (size_t)count * sizeof(*events));
  if (timeout_ms < -1 || timeout_ms > 60000 ||
      (count != 0 && (descriptors == NULL || interests == NULL))) {
    errno = EINVAL; return -1;
  }
  for (int64_t index = 0; index < count; ++index) {
    if (descriptors[index] < 0 || descriptors[index] > INT_MAX ||
        interests[index] < 0 || interests[index] > 3) {
      errno = EINVAL; return -1;
    }
  }
  struct pollfd *items = count == 0 ? NULL : malloc((size_t)count * sizeof(*items));
  if (count != 0 && items == NULL) { errno = ENOMEM; return -1; }
  for (int64_t index = 0; index < count; ++index) {
    items[index].fd = (int)descriptors[index];
    items[index].events = (short)(((interests[index] & 1) ? POLLIN : 0) |
                                  ((interests[index] & 2) ? POLLOUT : 0));
    items[index].revents = 0;
  }
  const int result = poll(items, (nfds_t)count, (int)timeout_ms);
  const int poll_error = errno;
  int64_t ready = 0;
  if (result > 0) {
    for (int64_t index = 0; index < count; ++index) {
      const short flags = items[index].revents;
      events[index] = ((flags & POLLIN) ? 1 : 0) | ((flags & POLLOUT) ? 2 : 0) |
          ((flags & POLLERR) ? 4 : 0) | ((flags & POLLHUP) ? 8 : 0) |
          ((flags & POLLNVAL) ? 16 : 0);
      if (events[index] != 0) ++ready;
    }
  }
  free(items);
  if (result < 0 && poll_error != EINTR) { errno = poll_error; return -1; }
  // An interrupt returns control to the Neri lifecycle without restarting its wait.
  errno = previous_error;
  return ready;
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
int64_t neri_rt_v1_net_close_result(int64_t fd) { return close((int)fd); }
int64_t neri_rt_v1_net_milliseconds(void) {
  struct timespec now = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
  return now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000;
}
int64_t neri_rt_v1_net_error(uint8_t *bytes, int64_t capacity) {
  if (bytes == NULL || capacity <= 0) return 0;
  const int error = errno;
  char message[256];
  const size_t length = neri_platform_error_text(error, message, sizeof(message));
  const size_t copied = length < (size_t)capacity ? length : (size_t)capacity;
  memcpy(bytes, message, copied);
  errno = error;
  return (int64_t)copied;
}
