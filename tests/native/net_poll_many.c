#define _POSIX_C_SOURCE 200809L
#include "neri/runtime_abi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#endif

/* OS descriptor masks, pipe readiness and signal interruption are native ABI
 * contracts; the coordinator policy remains in Neri. */
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "net_poll_many:%d: %s\n", __LINE__, #value); abort(); } } while (0)

static void socket_pair(int64_t pair[2]) {
  const int64_t listener = neri_rt_v1_net_open();

  CHECK(listener >= 0);
  CHECK(neri_rt_v1_net_configure(listener) == 0);
  CHECK(neri_rt_v1_net_bind(listener, 0) == 0);
  CHECK(neri_rt_v1_net_listen(listener) == 0);
  const int64_t port = neri_rt_v1_net_local_port(listener);
  pair[0] = neri_rt_v1_net_open();

  CHECK(port > 0 && pair[0] >= 0);
  CHECK(neri_rt_v1_net_connect(pair[0], port) == 0);
  CHECK(neri_rt_v1_net_poll(listener, 0, 1000) > 0);
  pair[1] = neri_rt_v1_net_accept(listener);

  CHECK(pair[1] >= 0);
  CHECK(neri_rt_v1_net_close_result(listener) == 0);
}

static void previous_error(int value) {
#if defined(_WIN32)
  WSASetLastError(value);
#else
  errno = value;
#endif
}

static int last_error(void) {
#if defined(_WIN32)
  return WSAGetLastError();
#else
  return errno;
#endif
}

#if !defined(_WIN32)
static volatile sig_atomic_t interrupted;
static void alarm_received(int signal) { (void)signal; interrupted = 1; }

static void pipe_events(void) {
  int descriptors[2];

  CHECK(pipe(descriptors) == 0);
  const int64_t fd = descriptors[0], interest = 1;
  int64_t event = -1;
  uint8_t byte = 42;

  CHECK(write(descriptors[1], &byte, 1) == 1);
  CHECK(neri_rt_v1_net_poll_many(&fd, &interest, &event, 1, 0) == 1);
  CHECK(event == 1);
  CHECK(read(descriptors[0], &byte, 1) == 1);

  struct sigaction action = {0}, previous;
  action.sa_handler = alarm_received;
  sigemptyset(&action.sa_mask);

  CHECK(sigaction(SIGALRM, &action, &previous) == 0);
  // Repetition also covers a signal delivered just before entering poll.
  const struct itimerval timer = {{0, 20000}, {0, 20000}};

  CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
  previous_error(EDOM);
  const int64_t result = neri_rt_v1_net_poll_many(&fd, &interest, &event, 1, -1);
  const int poll_error = last_error();
  const struct itimerval disabled = {{0, 0}, {0, 0}};

  CHECK(setitimer(ITIMER_REAL, &disabled, NULL) == 0);
  CHECK(result == 0 && interrupted && event == 0);
  CHECK(poll_error == EDOM);
  CHECK(sigaction(SIGALRM, &previous, NULL) == 0);
  CHECK(close(descriptors[1]) == 0);
  CHECK(neri_rt_v1_net_poll_many(&fd, &interest, &event, 1, 0) == 1);
  CHECK((event & 8) != 0);
  CHECK(close(descriptors[0]) == 0);
  CHECK(neri_rt_v1_net_poll_many(&fd, &interest, &event, 1, 0) == 1);
  CHECK(event == 16);
}
#endif

int main(void) {
  int64_t first[2], second[2];
  socket_pair(first);
  socket_pair(second);
  int64_t descriptors[2] = {first[1], second[1]};
  int64_t interests[2] = {1, 1}, events[2] = {-1, -1};

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, 0) == 0);
  CHECK(events[0] == 0 && events[1] == 0);
  uint8_t byte = 42;

  CHECK(neri_rt_v1_net_write(second[0], &byte, 1) == 1);
  previous_error(123);
  // The first descriptor stays idle: a per-descriptor blocking loop would hang.
  const int64_t ready = neri_rt_v1_net_poll_many(descriptors, interests, events, 2, -1);

  CHECK(ready == 1 && events[0] == 0 && events[1] == 1);
  CHECK(last_error() == 123);
  interests[0] = 2;

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, 0) == 2);
  CHECK(events[0] == 2 && events[1] == 1);
  CHECK(neri_rt_v1_net_read(second[1], &byte, 1) == 1);
  interests[0] = 1;

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, 10) == 0);
  CHECK(events[0] == 0 && events[1] == 0);
  interests[1] = 4;
  events[0] = events[1] = -1;

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, -1) == -1);
  CHECK(events[0] == 0 && events[1] == 0);
  interests[1] = 1;
  descriptors[1] = -1;
  events[0] = events[1] = -1;

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, 0) == -1);
  CHECK(events[0] == 0 && events[1] == 0);
  descriptors[1] = second[1];

  CHECK(neri_rt_v1_net_poll_many(descriptors, interests, events, 2, 60001) == -1);
  CHECK(neri_rt_v1_net_poll_many(NULL, NULL, NULL, 4097, 0) == -1);
  CHECK(neri_rt_v1_net_poll_many(NULL, NULL, NULL, -1, 0) == -1);
  CHECK(neri_rt_v1_net_poll_many(NULL, NULL, NULL, 0, 10) == 0);
  CHECK(neri_rt_v1_net_close_result(first[0]) == 0);
  CHECK(neri_rt_v1_net_close_result(first[1]) == 0);
  CHECK(neri_rt_v1_net_close_result(second[0]) == 0);
  CHECK(neri_rt_v1_net_close_result(second[1]) == 0);
#if !defined(_WIN32)
  pipe_events();
#endif
  return 0;
}
