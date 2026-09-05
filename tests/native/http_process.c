#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
/* Process supervision unavailable through the synchronous host.run contract.
 * Protocol traffic and assertions are implemented by tests/http/client.hk.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

static pid_t start(char **arguments) {
  const pid_t child = fork();
  if (child == 0) {
    setpgid(0, 0);
    execv(arguments[0], arguments);
    _exit(127);
  }
  if (child > 0) setpgid(child, child);
  return child;
}

static void stop(pid_t child) {
  if (child <= 0) return;
  kill(-child, SIGKILL);
  while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fputs("usage: neri-http-process <client> <server> [server args...]\n", stderr);
    return 2;
  }
  const int probe = socket(AF_INET, SOCK_STREAM, 0);
  const int enabled = 1;
  setsockopt(probe, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons(8080);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (probe < 0 || bind(probe, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("HTTP test requires an available 127.0.0.1:8080");
    if (probe >= 0) close(probe);
    return 2;
  }
  close(probe);
  const pid_t server = start(&argv[2]);
  if (server < 0) return 2;
  char *client_arguments[] = {argv[1], NULL};
  const pid_t client = start(client_arguments);
  if (client < 0) { stop(server); return 2; }
  struct timespec started;
  clock_gettime(CLOCK_MONOTONIC, &started);
  int code = 124;
  int reaped = 0;
  for (;;) {
    int status;
    const pid_t result = waitpid(client, &status, WNOHANG);
    if (result == client) {
      reaped = 1;
      code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      break;
    }
    if (result < 0 && errno != EINTR) break;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - started.tv_sec >= 60) break;
    const struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
  }
  if (!reaped) stop(client);
  stop(server);
  return code;
}
