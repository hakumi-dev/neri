#include "neri/runtime_abi.h"
#include <winsock2.h>
#include <windows.h>
#include <limits.h>
#include <string.h>

static INIT_ONCE startup = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK initialize(PINIT_ONCE once, PVOID parameter, PVOID *context) {
  (void)once; (void)parameter; (void)context;
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
static int64_t io_result(int result) {
  if (result != SOCKET_ERROR) return result;
  const int error = WSAGetLastError();
  return error == WSAEINTR || error == WSAEWOULDBLOCK ||
                 error == WSAEINPROGRESS || error == WSAEALREADY ||
                 error == WSAECONNABORTED ? -2 : -1;
}
int64_t neri_rt_v1_net_open(void) {
  if (!InitOnceExecuteOnce(&startup, initialize, NULL, NULL)) return -1;
  SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  return fd == INVALID_SOCKET ? -1 : (int64_t)fd;
}
int64_t neri_rt_v1_net_configure(int64_t fd) {
  u_long nonblocking = 1;
  const int enabled = 1;
  if (!SetHandleInformation((HANDLE)(uintptr_t)fd, HANDLE_FLAG_INHERIT, 0) ||
      ioctlsocket((SOCKET)fd, FIONBIO, &nonblocking) != 0) return -1;
  /* SO_REUSEADDR permits duplicate listeners on Windows. The HTTP contract
   * requires an occupied loopback port to fail deterministically. */
  return setsockopt((SOCKET)fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                    (const char *)&enabled, sizeof(enabled));
}
static int64_t address_operation(int64_t fd, int64_t port, int connecting) {
  if (port < 1 || port > 65535) { WSASetLastError(WSAEINVAL); return -1; }
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return connecting ? connect((SOCKET)fd, (struct sockaddr *)&address, sizeof(address))
                    : bind((SOCKET)fd, (struct sockaddr *)&address, sizeof(address));
}
int64_t neri_rt_v1_net_bind(int64_t fd, int64_t port) { return address_operation(fd, port, 0); }
int64_t neri_rt_v1_net_connect(int64_t fd, int64_t port) {
  return io_result((int)address_operation(fd, port, 1));
}
int64_t neri_rt_v1_net_listen(int64_t fd) { return listen((SOCKET)fd, 16); }
int64_t neri_rt_v1_net_accept(int64_t fd) {
  SOCKET result = accept((SOCKET)fd, NULL, NULL);
  return result == INVALID_SOCKET ? io_result(SOCKET_ERROR) : (int64_t)result;
}
int64_t neri_rt_v1_net_poll(int64_t fd, int64_t writing, int64_t milliseconds) {
  if (fd < 0 || milliseconds < -1 || milliseconds > INT_MAX) { WSASetLastError(WSAEINVAL); return -1; }
  WSAPOLLFD item = {(SOCKET)fd, (short)(writing ? POLLWRNORM : POLLRDNORM), 0};
  int result = WSAPoll(&item, 1, (int)milliseconds);
  if (result > 0 && (item.revents & POLLNVAL)) { WSASetLastError(WSAENOTSOCK); return -1; }
  return io_result(result);
}
int64_t neri_rt_v1_net_read(int64_t fd, uint8_t *bytes, int64_t length) {
  if (length < 0) { WSASetLastError(WSAEINVAL); return -1; }
  return io_result(recv((SOCKET)fd, (char *)bytes, (int)(length > INT_MAX ? INT_MAX : length), 0));
}
int64_t neri_rt_v1_net_write(int64_t fd, uint8_t *bytes, int64_t length) {
  if (length < 0) { WSASetLastError(WSAEINVAL); return -1; }
  return io_result(send((SOCKET)fd, (const char *)bytes, (int)(length > INT_MAX ? INT_MAX : length), 0));
}
void neri_rt_v1_net_close(int64_t fd) { closesocket((SOCKET)fd); }
int64_t neri_rt_v1_net_milliseconds(void) { return (int64_t)GetTickCount64(); }
int64_t neri_rt_v1_net_error(uint8_t *bytes, int64_t capacity) {
  const DWORD error = (DWORD)WSAGetLastError();
  if (capacity <= 0) return 0;
  if (error == WSAEADDRINUSE) {
    static const char message[] = "Address already in use";
    const DWORD length = (DWORD)(sizeof(message) - 1 < (size_t)capacity ? sizeof(message) - 1 : (size_t)capacity);
    memcpy(bytes, message, length);
    return length;
  }
  char message[512];
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), message, sizeof(message), NULL);
  if (!length)
    length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, message, sizeof(message), NULL);
  if (!length) return 0;
  if ((int64_t)length > capacity) length = (DWORD)capacity;
  memcpy(bytes, message, length);
  return length;
}
