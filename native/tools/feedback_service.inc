// Local IPC is an OS capability; feedback policy and analysis live in Neri.
#if !defined(_WIN32)
#include <poll.h>
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

namespace {
constexpr std::size_t feedback_transport_bytes = 2097152;
constexpr int feedback_transport_timeout_ms = 600000;

bool feedback_ready(int descriptor, short events, int timeout) {
  pollfd value{descriptor, events, 0};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  while (true) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return false;
    const int result = poll(&value, 1, static_cast<int>(remaining));
    if (result < 0 && errno == EINTR) continue;
    return result > 0 && (value.revents & events) != 0;
  }
}

using feedback_deadline = std::chrono::steady_clock::time_point;

int feedback_remaining(feedback_deadline deadline) {
  return static_cast<int>(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()));
}

void feedback_nonblocking(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFL);
  if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0)
    throw std::runtime_error("cannot configure feedback transport");
}

bool feedback_send(int descriptor, const std::string &text, feedback_deadline deadline) {
  std::size_t offset = 0;
  while (offset < text.size()) {
    if (!feedback_ready(descriptor, POLLOUT, feedback_remaining(deadline))) return false;
    const auto count = ::write(descriptor, text.data() + offset, text.size() - offset);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) return false;
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

bool feedback_receive(int descriptor, std::string &text, feedback_deadline deadline, int cancel = -1) {
  text.clear();
  while (text.size() <= feedback_transport_bytes) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return false;
    if (cancel >= 0) {
      pollfd pending[2]{{descriptor, POLLIN, 0}, {cancel, POLLIN, 0}};
      const int result = poll(pending, 2, static_cast<int>(remaining));
      if (result < 0 && errno == EINTR) continue;
      if (result <= 0 || (pending[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) return false;
      if ((pending[0].revents & POLLIN) == 0) return false;
    } else if (!feedback_ready(descriptor, POLLIN, static_cast<int>(remaining))) return false;
    char buffer[4096];
    const auto count = ::read(descriptor, buffer, sizeof(buffer));
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) return false;
    text.append(buffer, static_cast<std::size_t>(count));
    const auto newline = text.find('\n');
    if (newline != std::string::npos) {
      if (newline + 1 != text.size()) return false;
      text.resize(newline);
      return text.size() <= feedback_transport_bytes;
    }
  }
  return false;
}

sockaddr_un feedback_address(const std::string &path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) throw std::runtime_error("feedback endpoint path is too long");
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return address;
}

int feedback_connect(const std::string &path, feedback_deadline deadline) {
  const auto address = feedback_address(path);
  const int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
  if (descriptor < 0) return -1;
  fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  feedback_nonblocking(descriptor);
  if (connect(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0) return descriptor;
  if (errno == EINPROGRESS && feedback_ready(descriptor, POLLOUT, feedback_remaining(deadline))) {
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) return descriptor;
  }
  close(descriptor);
  return -1;
}

void feedback_broker(const std::string &path, const char *compiler, const char *root) {
  const int lock = open((path + ".lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  if (lock < 0) _exit(1);
  struct flock record{};
  record.l_type = F_WRLCK;
  record.l_whence = SEEK_SET;
  if (fcntl(lock, F_SETLK, &record) < 0) _exit(0);
  fcntl(lock, F_SETFD, FD_CLOEXEC);
  const auto address = feedback_address(path);
  const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) _exit(1);
  fcntl(listener, F_SETFD, FD_CLOEXEC);
  feedback_nonblocking(listener);
  unlink(path.c_str());
  if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0 ||
      chmod(path.c_str(), 0600) < 0 || listen(listener, 16) < 0) _exit(1);
  int input[2], output[2];
  if (pipe(input) < 0 || pipe(output) < 0) _exit(1);
  const auto child = fork();
  if (child < 0) _exit(1);
  if (child == 0) {
    dup2(input[0], STDIN_FILENO);
    dup2(output[1], STDOUT_FILENO);
    const int errors = open("/dev/null", O_WRONLY);
    if (errors >= 0) dup2(errors, STDERR_FILENO);
    close(input[0]); close(input[1]); close(output[0]); close(output[1]);
    execl(compiler, compiler, "feedback-worker", "--project", root, static_cast<char *>(nullptr));
    _exit(127);
  }
  close(input[0]); close(output[1]);
  feedback_nonblocking(input[1]);
  feedback_nonblocking(output[0]);
  fcntl(input[1], F_SETFD, FD_CLOEXEC);
  fcntl(output[0], F_SETFD, FD_CLOEXEC);
  signal(SIGPIPE, SIG_IGN);
  // An idle broker closes stdin; the worker exits through its normal EOF path.
  while (feedback_ready(listener, POLLIN, 120000)) {
    const int client = accept(listener, nullptr, nullptr);
    if (client < 0) continue;
    fcntl(client, F_SETFD, FD_CLOEXEC);
    feedback_nonblocking(client);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(feedback_transport_timeout_ms);
    std::string request, response;
    const bool healthy = feedback_receive(client, request, deadline) &&
        feedback_send(input[1], request + "\n", deadline) && feedback_receive(output[0], response, deadline, client);
    if (healthy) feedback_send(client, response + "\n", deadline);
    close(client);
    if (!healthy) break;
  }
  close(input[1]); close(output[0]); close(listener);
  unlink(path.c_str());
  // A stuck worker cannot retain a project indefinitely after transport failure.
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (waitpid(child, nullptr, WNOHANG) == child) { close(lock); _exit(0); }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  kill(child, SIGTERM);
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (waitpid(child, nullptr, WNOHANG) == child) { close(lock); _exit(0); }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  kill(child, SIGKILL);
  waitpid(child, nullptr, 0);
  close(lock);
  _exit(0);
}

int feedback_service(const char *endpoint, const char *compiler, const char *root, const char *request_path, const char *output_path) {
  const std::string key(endpoint);
  if (key.size() != 64 || key.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("invalid feedback endpoint identity");
  const std::string path = "/tmp/neri-feedback-" + std::to_string(getuid()) + "/" + endpoint + ".sock";
  const auto directory = neri::host_path(path).parent_path();
  if (mkdir(directory.c_str(), 0700) < 0 && errno != EEXIST)
    throw std::runtime_error("cannot create feedback endpoint directory");
  struct stat attributes{};
  if (lstat(directory.c_str(), &attributes) < 0 || !S_ISDIR(attributes.st_mode) ||
      attributes.st_uid != getuid() || (attributes.st_mode & 0077) != 0)
    throw std::runtime_error("feedback endpoint directory is not private");
  const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int client = feedback_connect(path, startup_deadline);
  if (client < 0) {
    const auto child = fork();
    if (child < 0) throw std::runtime_error("cannot start feedback broker");
    if (child == 0) {
      setsid();
      const auto daemon = fork();
      if (daemon < 0) _exit(1);
      if (daemon > 0) _exit(0);
      const int empty = open("/dev/null", O_RDWR);
      if (empty >= 0) { dup2(empty, 0); dup2(empty, 1); dup2(empty, 2); }
      const long maximum = sysconf(_SC_OPEN_MAX);
      if (maximum < 0) _exit(1);
      for (int descriptor = 3; descriptor < maximum; ++descriptor) close(descriptor);
      feedback_broker(path, compiler, root);
    }
    waitpid(child, nullptr, 0);
    while (client < 0 && feedback_remaining(startup_deadline) > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      client = feedback_connect(path, startup_deadline);
    }
  }
  if (client < 0) throw std::runtime_error("feedback service is unavailable");
  std::ifstream stream(neri::host_path(request_path), std::ios::binary);
  std::string request;
  request.resize(feedback_transport_bytes + 1);
  stream.read(request.data(), static_cast<std::streamsize>(request.size()));
  request.resize(static_cast<std::size_t>(stream.gcount()));
  std::string response;
  signal(SIGPIPE, SIG_IGN);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(feedback_transport_timeout_ms);
  const bool healthy = !stream.bad() && request.size() <= feedback_transport_bytes && request.find('\n') == std::string::npos &&
      feedback_send(client, request + "\n", deadline) && feedback_receive(client, response, deadline);
  close(client);
  if (!healthy) throw std::runtime_error("feedback service transport failed or exceeded its deadline");
  write(output_path, response);
  return 0;
}
}
#endif
