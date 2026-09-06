#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* A real stdio client, independent of the server's JSON implementation.
 * No timing thresholds: deadlines only prevent a broken server hanging CI. */
static pid_t server = -1;
static int input = -1, output = -1;
static char dynamic_project_source[4096];
static char symlink_project_source[4096];
static char documented_stdlib[4096];
static char isolated_compiler[4096];
static int installed_launcher = 0;
static int use_documented_stdlib = 1;

static void cleanup(void) {
  if (dynamic_project_source[0]) unlink(dynamic_project_source);
  if (symlink_project_source[0]) unlink(symlink_project_source);
  if (documented_stdlib[0]) {
    char path[8192];
    const char *files[] = {"http.hk", "terminal.hk", "clock.hk", "documentation.json"};
    for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
      snprintf(path, sizeof(path), "%s/%s", documented_stdlib, files[index]);
      unlink(path);
    }
    rmdir(documented_stdlib);
  }
  if (server > 0) {
    kill(server, SIGKILL);
    while (waitpid(server, NULL, 0) < 0 && errno == EINTR) {}
  }
  if (input >= 0) close(input);
  if (output >= 0) close(output);
}

static void require(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "LSP contract failed: %s\n", message);
    exit(1);
  }
}

static void transfer(int fd, char *bytes, size_t size, int writing) {
  while (size) {
    struct pollfd ready = {fd, writing ? POLLOUT : POLLIN, 0};
    int result;
    do { result = poll(&ready, 1, 15000); } while (result < 0 && errno == EINTR);
    require(result > 0, "stdio deadline");
    ssize_t count = writing ? write(fd, bytes, size) : read(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    require(count > 0, "unexpected stdio EOF/error");
    bytes += count;
    size -= (size_t)count;
  }
}

static void send_message(const char *body, int fragmented) {
  char header[80];
  int size = snprintf(header, sizeof(header), "content-length: %zu\r\n\r\n", strlen(body));
  transfer(input, header, (size_t)size, 1);
  size_t length = strlen(body);
  for (size_t offset = 0; offset < length;) {
    size_t count = fragmented && length - offset > 7 ? 7 : length - offset;
    transfer(input, (char *)body + offset, count, 1);
    offset += count;
  }
}

static char *receive(void) {
  char header[8192] = {0};
  size_t used = 0;
  while (used < 4 || memcmp(header + used - 4, "\r\n\r\n", 4) != 0) {
    require(used + 1 < sizeof(header), "bounded response headers");
    transfer(output, header + used++, 1, 0);
  }
  size_t length = 0;
  require(sscanf(header, "Content-Length: %zu", &length) == 1 && length < 2097152,
          "byte-counted response framing, no stdout logs");
  char *body = calloc(length + 1, 1);
  require(body != NULL, "allocate response");
  transfer(output, body, length, 0);
  return body;
}

static const char *spaces(const char *text) {
  while (isspace((unsigned char)*text)) ++text;
  return text;
}

/* Traverse JSON values, without relying on property order or whitespace.
 * The asserted protocol keys/values are ASCII; escaped source is only sent. */
static const char *skip_value(const char *text, int depth) {
  require(depth < 128, "response nesting");
  text = spaces(text);
  if (*text == '"') {
    for (++text; *text && *text != '"'; ++text) {
      if (*text == '\\') { ++text; require(*text != 0, "JSON escape"); }
    }
    require(*text == '"', "JSON string terminator");
    return text + 1;
  }
  if (*text == '{' || *text == '[') {
    char end = *text == '{' ? '}' : ']';
    text = spaces(text + 1);
    while (*text != end) {
      require(*text != 0, "JSON container terminator");
      text = spaces(skip_value(text, depth + 1));
      if (*text == ':' || *text == ',') text = spaces(text + 1);
    }
    return text + 1;
  }
  const char *start = text;
  while (*text && !strchr(",]} \t\r\n", *text)) ++text;
  require(text > start, "JSON scalar");
  return text;
}

static const char *field(const char *object, const char *key) {
  object = spaces(object);
  require(*object == '{', "expected JSON object");
  const char *cursor = spaces(object + 1);
  while (*cursor != '}') {
    const char *end = skip_value(cursor, 0);
    require(*cursor == '"', "JSON property name");
    int match = (size_t)(end - cursor) == strlen(key) + 2 &&
                memcmp(cursor + 1, key, strlen(key)) == 0;
    cursor = spaces(end);
    require(*cursor == ':', "JSON property separator");
    cursor = spaces(cursor + 1);
    if (match) return cursor;
    cursor = spaces(skip_value(cursor, 0));
    if (*cursor == ',') cursor = spaces(cursor + 1);
  }
  require(0, key);
  return NULL;
}

static int string_is(const char *value, const char *expected) {
  value = spaces(value);
  return *value == '"' && strncmp(value + 1, expected, strlen(expected)) == 0 &&
         value[strlen(expected) + 1] == '"';
}

static void error_is(int code) {
  char *reply = receive();
  require(strtol(field(field(reply, "error"), "code"), NULL, 10) == code,
          "JSON-RPC error code");
  free(reply);
}

static char *quote(const char *text) {
  size_t length = strlen(text);
  char *result = malloc(length * 6 + 3);
  require(result != NULL, "allocate JSON string");
  char *next = result;
  *next++ = '"';
  for (size_t index = 0; index < length; ++index) {
    unsigned char byte = (unsigned char)text[index];
    if (byte < 32) {
      next += sprintf(next, "\\u%04x", byte);
    } else {
      if (byte == '"' || byte == '\\') *next++ = '\\';
      *next++ = (char)byte;
    }
  }
  *next++ = '"';
  *next = 0;
  return result;
}

static void open_document(const char *uri, const char *text, int fragmented) {
  char *encoded = quote(text);
  size_t size = strlen(encoded) + strlen(uri) + 256;
  char *message = malloc(size);
  require(message != NULL, "allocate open notification");
  snprintf(message, size, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
           "\"params\":{\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"neri\","
           "\"version\":1,\"text\":%s}}}", uri, encoded);
  send_message(message, fragmented);
  free(encoded);
  free(message);
}

static const char *diagnostics(const char *reply) {
  require(string_is(field(reply, "method"), "textDocument/publishDiagnostics"),
          "publish compiler diagnostics");
  return field(field(reply, "params"), "diagnostics");
}

static void empty_diagnostics(int version) {
  char *reply = receive();
  const char *list = diagnostics(reply);
  if (*list != '[' || *spaces(list + 1) != ']') fprintf(stderr, "Unexpected diagnostics: %s\n", reply);
  require(*list == '[' && *spaces(list + 1) == ']', "clear diagnostics");
  require(strtol(field(field(reply, "params"), "version"), NULL, 10) == version,
          "diagnostic document version");
  free(reply);
}

static char *fixture(const char *root) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/tests/contracts/callbacks.hk", root);
  FILE *file = fopen(path, "rb");
  require(file != NULL && fseek(file, 0, SEEK_END) == 0, "open existing callback fixture");
  long length = ftell(file);
  require(length >= 0 && length < 1048576 && fseek(file, 0, SEEK_SET) == 0, "fixture size");
  char *text = calloc((size_t)length + 1, 1);
  require(text != NULL && fread(text, 1, (size_t)length, file) == (size_t)length, "read fixture");
  fclose(file);
  open_document("file:///callbacks.hk", text, 0);
  empty_diagnostics(1);
  return text;
}

static double milliseconds(void) {
  struct timespec now;
  require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic benchmark clock");
  return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

/* Linux server RSS, not the client or compiler build. Unavailable elsewhere. */
static long server_rss(void) {
  char path[80], line[256];
  snprintf(path, sizeof(path), "/proc/%ld/status", (long)server);
  FILE *file = fopen(path, "r");
  if (!file) return -1;
  long rss = -1;
  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
  }
  fclose(file);
  return rss;
}

static int compare_samples(const void *left, const void *right) {
  double a = *(const double *)left, b = *(const double *)right;
  return (a > b) - (a < b);
}

static void benchmark(const char *source) {
  enum { samples = 256 };
  double latency[samples];
  size_t size = strlen(source) + 5;
  char *text = malloc(size);
  require(text != NULL, "allocate benchmark source");
  snprintf(text, size, "# 0\n%s", source);
  open_document("file:///benchmark.hk", text, 0);
  empty_diagnostics(1);
  long before = server_rss(), peak = before;
  for (int index = 0; index < samples; ++index) {
    char message[512];
    snprintf(message, sizeof(message),
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"file:///benchmark.hk\",\"version\":%d},\"contentChanges\":["
      "{\"range\":{\"start\":{\"line\":0,\"character\":2},\"end\":{\"line\":0,\"character\":3}},"
      "\"text\":\"%d\"}]}}", index + 2, (index + 1) % 2);
    double start = milliseconds();
    send_message(message, 0);
    empty_diagnostics(index + 2);
    latency[index] = milliseconds() - start;
    long rss = server_rss();
    if (rss > peak) peak = rss;
  }
  long after = server_rss();
  qsort(latency, samples, sizeof(latency[0]), compare_samples);
  printf("LSP benchmark: {\"fixture\":\"callbacks.hk\",\"source_bytes\":%zu,"
    "\"edits\":%d,\"p50_ms\":%.3f,\"p95_ms\":%.3f,\"p99_ms\":%.3f,"
    "\"max_ms\":%.3f,\"rss_before_kib\":%ld,\"rss_after_kib\":%ld,\"rss_sampled_peak_kib\":%ld}\n",
    strlen(text), samples, latency[(samples * 50 + 99) / 100 - 1],
    latency[(samples * 95 + 99) / 100 - 1], latency[(samples * 99 + 99) / 100 - 1],
    latency[samples - 1], before, after, peak);
  free(text);
}

static void start_server(const char *compiler, const char *root) {
  int to_server[2], from_server[2];
  require(pipe(to_server) == 0 && pipe(from_server) == 0, "create protocol pipes");
  server = fork();
  require(server >= 0, "start server");
  if (server == 0) {
    if (dup2(to_server[0], STDIN_FILENO) < 0 || dup2(from_server[1], STDOUT_FILENO) < 0) _exit(2);
    close(to_server[0]); close(to_server[1]); close(from_server[0]); close(from_server[1]);
    char library[4096];
    if (use_documented_stdlib && documented_stdlib[0])
      snprintf(library, sizeof(library), "%s", documented_stdlib);
    else
      snprintf(library, sizeof(library), "%s/stdlib", root);
    if (setenv("NERI_STDLIB", library, 1) != 0) _exit(2);
    const char *executable = !use_documented_stdlib && installed_launcher ? isolated_compiler : compiler;
    execl(executable, executable, "lsp", (char *)NULL);
    _exit(2);
  }
  close(to_server[0]); close(from_server[1]);
  input = to_server[1]; output = from_server[0];
}

static void identify_isolated_compiler(const char *compiler) {
  char resolved[4096];
  require(realpath(compiler, resolved) != NULL, "resolve tested compiler path");
  snprintf(isolated_compiler, sizeof(isolated_compiler), "%s", resolved);
  const char suffix[] = "/bin/neri";
  size_t length = strlen(resolved), suffix_length = sizeof(suffix) - 1;
  if (length <= suffix_length || strcmp(resolved + length - suffix_length, suffix) != 0) return;

  char candidate[4096], manifest[4096];
  int root_length = (int)(length - suffix_length);
  require(root_length + (int)sizeof("/libexec/neri") < (int)sizeof(candidate) &&
          root_length + (int)sizeof("/ARTIFACTS.sha256") < (int)sizeof(manifest),
          "bounded installed toolchain path");
  snprintf(candidate, sizeof(candidate), "%.*s/libexec/neri", root_length, resolved);
  snprintf(manifest, sizeof(manifest), "%.*s/ARTIFACTS.sha256", root_length, resolved);
  char raw[4096];
  if (access(candidate, X_OK) != 0 || access(manifest, R_OK) != 0 || realpath(candidate, raw) == NULL) return;
  snprintf(isolated_compiler, sizeof(isolated_compiler), "%s", raw);
  installed_launcher = 1;
}

static void prepare_documented_stdlib(const char *root) {
  snprintf(documented_stdlib, sizeof(documented_stdlib), "%s/build/lsp-documentation-XXXXXX", root);
  require(mkdtemp(documented_stdlib) != NULL, "create isolated documented stdlib");
  char source[8192], destination[8192], output_path[8192];
  const char *files[] = {"http.hk", "terminal.hk", "clock.hk"};
  for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
    snprintf(source, sizeof(source), "%s/stdlib/%s", root, files[index]);
    snprintf(destination, sizeof(destination), "%s/%s", documented_stdlib, files[index]);
    require(symlink(source, destination) == 0, "link documented standard-library source");
  }
  snprintf(output_path, sizeof(output_path), "%s/documentation.json", documented_stdlib);
  char http_path[8192], terminal_path[8192], clock_path[8192];
  snprintf(http_path, sizeof(http_path), "%s/http.hk", documented_stdlib);
  snprintf(terminal_path, sizeof(terminal_path), "%s/terminal.hk", documented_stdlib);
  snprintf(clock_path, sizeof(clock_path), "%s/clock.hk", documented_stdlib);
  pid_t child = fork();
  require(child >= 0, "start documentation index generator");
  if (child == 0) {
    execl(isolated_compiler, isolated_compiler, "documentation-index", "--output", output_path,
          http_path, terminal_path, clock_path, (char *)NULL);
    _exit(2);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "generate documented standard-library sidecar");
}

static void finish_server(void) {
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"shutdown\",\"id\":3}", 0);
  char *reply = receive();
  require(strncmp(field(reply, "result"), "null", 4) == 0, "shutdown response");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}", 0);
  int status = 0, finished = 0;
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (waitpid(server, &status, WNOHANG) == server) { finished = 1; break; }
    poll(NULL, 0, 10);
  }
  if (finished) server = -1;
  require(finished && WIFEXITED(status) && WEXITSTATUS(status) == 0, "clean LSP shutdown");
  close(input); close(output);
  input = output = -1;
}

static char *file_uri(const char *path) {
  char *result = malloc(strlen(path) * 3 + 8);
  require(result != NULL, "allocate source URI");
  strcpy(result, "file://");
  char *next = result + 7;
  for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
    if (isalnum(*p) || strchr("/._-~", *p)) *next++ = (char)*p;
    else next += sprintf(next, "%%%02X", *p);
  }
  *next = 0;
  return result;
}

static void project_contract(const char *compiler, const char *root) {
  start_server(compiler, root);
  char path[4096], message[16384];
  snprintf(path, sizeof(path), "%s/tests/lsp/project", root);
  char *uri = file_uri(path);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1,"
    "\"params\":{\"rootUri\":\"%s\",\"capabilities\":{}}}", uri);
  free(uri);
  send_message(message, 0);
  free(receive());
  snprintf(path, sizeof(path), "%s/tests/lsp/project/main.hk", root);
  char *main_uri = file_uri(path);
  open_document(main_uri, "use sample\ndef main(): Void\n  let app = new App()\n  let value: Int = app.value\n  let result = answer()\nend\n", 0);
  empty_diagnostics(1); /* Resolves the closed source; ignores unrelated.hk. */
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentSymbol\",\"id\":\"project-symbols\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}", main_uri);
  send_message(message, 0);
  char *symbols_reply = receive();
  const char *symbol = spaces(field(symbols_reply, "result") + 1);
  int symbol_count = 0;
  while (*symbol == '{') {
    require(string_is(field(field(symbol, "location"), "uri"), main_uri) &&
            !string_is(field(symbol, "name"), "App") && !string_is(field(symbol, "name"), "answer"),
            "document symbols exclude declarations from compilation dependencies");
    symbol_count++;
    symbol = spaces(skip_value(symbol, 0));
    if (*symbol == ',') symbol = spaces(symbol + 1);
    else break;
  }
  require(symbol_count == 4 && *symbol == ']', "document symbols include only main and its three locals");
  free(symbols_reply);
  snprintf(path, sizeof(path), "%s/tests/lsp/project/source files/app.hk", root);
  char *app_uri = file_uri(path);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"class\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":2,\"character\":17}}}", main_uri);
  send_message(message, 0);
  char *reply = receive();
  const char *location = field(reply, "result");
  require(string_is(field(location, "uri"), app_uri) &&
    strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 2,
    "constructor type definition maps to the closed source, including an encoded space");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"function\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":4,\"character\":16}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  location = field(reply, "result");
  require(string_is(field(location, "uri"), app_uri) &&
    strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 6,
    "resolved function call navigates across sources");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"signature\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":4,\"character\":16}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"), "def sample.answer(): Int"),
          "call hover presents the resolved callable signature, not a variable type");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentHighlight\",\"id\":\"local-highlights\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":2,\"character\":17}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  location = spaces(field(reply, "result") + 1);
  require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 2 &&
          *spaces(skip_value(location, 0)) == ']', "highlights do not leak declarations from closed dependencies");
  free(reply);
  open_document(app_uri, "namespace sample\npublic class App\n  public value: Int = 1\nend\ndef answer(): Int\n  return 42\nend\n", 0);
  empty_diagnostics(1);
  empty_diagnostics(1);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":2},\"contentChanges\":[{\"text\":"
    "\"namespace sample\\npublic class App\\n  public value: String = \\\"changed\\\"\\nend\\ndef answer(): Int\\n  return 42\\nend\\n\"}]}}", app_uri);
  send_message(message, 0);
  empty_diagnostics(2);
  reply = receive();
  require(string_is(field(field(reply, "params"), "uri"), main_uri) &&
          *spaces(diagnostics(reply) + 1) == '{', "unsaved dependency edit invalidates the consumer");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}", app_uri);
  send_message(message, 0);
  empty_diagnostics(2);
  empty_diagnostics(1); /* Closing the unsaved dependency restores its disk type. */
  free(main_uri); free(app_uri);
  finish_server();
}

static void glob_project_cli_contract(const char *compiler, const char *root) {
  char manifest[4096];
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/glob-project/neri.json", root);
  pid_t child = fork();
  require(child >= 0, "glob CLI process");
  if (child == 0) {
    execl(compiler, compiler, "check", "--project", manifest, (char *)NULL);
    _exit(127);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "CLI expands recursive source globs and excludes generated or unrelated mains");
  char target[4096];
  snprintf(symlink_project_source, sizeof(symlink_project_source), "%s/tests/lsp/glob-project/src/linked", root);
  snprintf(target, sizeof(target), "%s/tests/lsp/project", root);
  require(unlink(symlink_project_source) == 0 || errno == ENOENT, "reset project glob symlink");
  require(symlink(target, symlink_project_source) == 0, "create escaping project glob symlink");
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/glob-project/invalid-symlink.json", root);
  child = fork();
  require(child >= 0, "symlink glob CLI process");
  if (child == 0) {
    execl(compiler, compiler, "check", "--project", manifest, (char *)NULL);
    _exit(127);
  }
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 2,
          "CLI rejects a source glob whose literal prefix is a symlink");
  require(unlink(symlink_project_source) == 0, "remove project glob symlink");
  symlink_project_source[0] = 0;
}

static int project_cli_status(const char *compiler, const char *command, const char *manifest) {
  pid_t child = fork();
  require(child >= 0, "project CLI process");
  if (child == 0) {
    execl(compiler, compiler, command, "--project", manifest, (char *)NULL);
    _exit(127);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status), "project CLI process exits");
  return WEXITSTATUS(status);
}

static void automatic_project_cli_contract(const char *compiler, const char *root) {
  char manifest[8192];
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/auto-library/neri.json", root);
  require(project_cli_status(compiler, "check", manifest) == 0,
          "a project without main is a library for check");
  require(project_cli_status(compiler, "build", manifest) == 1,
          "building a library as an executable requires an entry point");

  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/auto-project/neri.json", root);
  require(project_cli_status(compiler, "check", manifest) == 0,
          "automatic discovery excludes nested projects and deduplicates diamond references");
  char target[4096];
  snprintf(symlink_project_source, sizeof(symlink_project_source), "%s/tests/lsp/auto-project-alias", root);
  snprintf(target, sizeof(target), "%s/tests/lsp/auto-project", root);
  require(unlink(symlink_project_source) == 0 || errno == ENOENT, "reset project manifest alias");
  require(symlink(target, symlink_project_source) == 0, "create project manifest alias");
  snprintf(manifest, sizeof(manifest), "%s/neri.json", symlink_project_source);
  require(project_cli_status(compiler, "check", manifest) == 0,
          "project manifests resolve symlink aliases before reference traversal");
  require(unlink(symlink_project_source) == 0, "remove project manifest alias");
  symlink_project_source[0] = 0;

  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/auto-reference-main/neri.json", root);
  require(project_cli_status(compiler, "check", manifest) == 1,
          "referenced projects cannot declare main");

  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/auto-cycle-a/neri.json", root);
  require(project_cli_status(compiler, "check", manifest) == 2,
          "project reference cycles are configuration errors");
}

static void glob_project_contract(const char *compiler, const char *root) {
  start_server(compiler, root);
  char path[4096], message[16384];
  snprintf(path, sizeof(path), "%s/tests/lsp/glob-project", root);
  char *uri = file_uri(path);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1,"
    "\"params\":{\"rootUri\":\"%s\",\"capabilities\":{}}}", uri);
  free(uri);
  send_message(message, 0);
  free(receive());
  snprintf(path, sizeof(path), "%s/tests/lsp/glob-project/cli/main.hk", root);
  char *main_uri = file_uri(path);
  snprintf(dynamic_project_source, sizeof(dynamic_project_source), "%s/tests/lsp/glob-project/src/extensions/later.hk", root);
  require(unlink(dynamic_project_source) == 0 || errno == ENOENT, "reset dynamic glob source");
  open_document(main_uri, "use sample\ndef main(): Void\n  let value = later()\nend\n", 0);
  char *reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "glob source is absent before its file is created");
  free(reply);
  FILE *file = fopen(dynamic_project_source, "wb");
  require(file != NULL, "create dynamic glob source");
  require(fputs("namespace sample\n\ndef later(): Int\n  return 7\nend\n", file) >= 0 && fclose(file) == 0,
          "write dynamic glob source");
  char *later_uri = file_uri(dynamic_project_source);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
    "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":1}]}}", later_uri);
  send_message(message, 0);
  empty_diagnostics(1);
  require(unlink(dynamic_project_source) == 0, "remove dynamic glob source");
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
    "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":3}]}}", later_uri);
  free(later_uri);
  send_message(message, 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "glob membership is reread after file deletion");
  free(reply);
  free(main_uri);
  finish_server();
}

#include "symbols.inc"
#include "signature.inc"
#include "completion.inc"
#include "documentation.inc"
#include "auto_project.inc"

static void source_name_cli_contract(const char *compiler, const char *root) {
  char manifest[4096];
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/project/invalid-name.json", root);
  for (int project = 0; project < 2; project++) {
    int channel[2];
    require(pipe(channel) == 0, "CLI diagnostic pipe");
    pid_t child = fork();
    require(child >= 0, "CLI diagnostic process");
    if (child == 0) {
      close(channel[0]);
      dup2(channel[1], STDOUT_FILENO);
      close(channel[1]);
      if (project) execl(compiler, compiler, "check", "--project", manifest, (char *)NULL);
      else execl(compiler, compiler, "check", "bad name.hk", (char *)NULL);
      _exit(127);
    }
    close(channel[1]);
    FILE *stream = fdopen(channel[0], "r");
    char message[8192];
    require(stream != NULL && fgets(message, sizeof(message), stream) != NULL,
            "CLI source name diagnostic");
    fclose(stream);
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 2 && strstr(message, ".hk file names must not contain whitespace"),
            "CLI and project reject whitespace basenames before reading source");
  }
}

int main(int argc, char **argv) {
  if (argc != 3 && !(argc == 4 && strcmp(argv[3], "--benchmark") == 0)) {
    fprintf(stderr, "usage: neri-lsp-test <compiler> <source-root> [--benchmark]\n"); return 2;
  }
  atexit(cleanup);
  signal(SIGPIPE, SIG_IGN);
  identify_isolated_compiler(argv[1]);
  prepare_documented_stdlib(argv[2]);
  source_name_cli_contract(argv[1], argv[2]);
  glob_project_cli_contract(argv[1], argv[2]);
  automatic_project_cli_contract(argv[1], argv[2]);
  start_server(argv[1], argv[2]);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"unknown\",\"id\":0}", 0);
  error_is(-32002);
  send_message("{\"jsonrpc\":\"2.0\",}", 0);
  error_is(-32700);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":\"init\",\"params\":{\"capabilities\":{}}}", 0);
  char *reply = receive();
  const char *capabilities = field(field(reply, "result"), "capabilities");
  require(string_is(field(capabilities, "positionEncoding"), "utf-16"), "UTF-16 encoding");
  require(*field(capabilities, "completionProvider") == '{' &&
          *field(capabilities, "signatureHelpProvider") == '{' &&
          strncmp(field(capabilities, "documentHighlightProvider"), "true", 4) == 0 &&
          strncmp(field(capabilities, "documentSymbolProvider"), "true", 4) == 0,
          "semantic query capabilities are discoverable");
  require(strtol(field(field(capabilities, "textDocumentSync"), "change"), NULL, 10) == 2,
          "incremental document synchronization");
  free(reply);
  const char *initialized = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
  const char *unknown = "{\"jsonrpc\":\"2.0\",\"method\":\"unknown\",\"id\":2}";
  char coalesced[256];
  int coalesced_size = snprintf(coalesced, sizeof(coalesced),
      "Content-Length: %zu\r\n\r\n%sContent-Length: %zu\r\n\r\n%s",
      strlen(initialized), initialized, strlen(unknown), unknown);
  require(write(input, coalesced, (size_t)coalesced_size) == coalesced_size,
          "send consecutive frames in one write");
  error_is(-32601);

  document_symbols_contract();
  signature_help_contract();
  completion_contract();
  documentation_contract();

  const char *invalid_names[] = {"file:///bad%20name.hk", "file:///bad%C2%A0name.hk"};
  for (size_t i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]); i++) {
    open_document(invalid_names[i], "def main(): Void\nend\n", 1);
    reply = receive();
    require(string_is(field(spaces(diagnostics(reply) + 1), "code"), "NR_FILE_NAME"),
            "whitespace in source basename is rejected");
    free(reply);
  }

  open_document("file:///folder%20name/unsaved.hk",
      "use console\r\ndef main(): Void\r\n\tconsole.println(\"😀\" + missing)\r\nend\r\n", 1);
  reply = receive();
  const char *list = diagnostics(reply);
  const char *diagnostic = spaces(list + 1);
  require(*list == '[' && *diagnostic == '{', "real semantic diagnostic");
  require(string_is(field(diagnostic, "source"), "neri"), "compiler diagnostic authority");
  const char *start = field(field(diagnostic, "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 2 &&
          strtol(field(start, "character"), NULL, 10) == 24,
          "non-BMP/tab/CRLF source range");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///folder%20name/unsaved.hk\",\"version\":2},"
    "\"contentChanges\":[{\"text\":\"use console\\ndef main(): Void\\n  console.println(\\\"\\ud83d\\ude00\\\")\\nend\\n\"}]}}", 0);
  empty_diagnostics(2);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///folder%20name/unsaved.hk\",\"version\":1},"
    "\"contentChanges\":[{\"text\":\"broken\"}]}}", 0);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///folder%20name/unsaved.hk\"}}}", 0);
  empty_diagnostics(2);

  open_document("file:///incremental.hk", "use console\r\ndef main(): Void\r\n\tconsole.println(\"😀\")\r\nend\r\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///incremental.hk\",\"version\":2},\"contentChanges\":["
    "{\"range\":{\"start\":{\"line\":2,\"character\":18},\"end\":{\"line\":2,\"character\":20}},\"text\":\"ok\"},"
    "{\"range\":{\"start\":{\"line\":2,\"character\":21},\"end\":{\"line\":2,\"character\":21}},\"text\":\" + missing\"}]}}", 0);
  reply = receive();
  start = field(field(spaces(diagnostics(reply) + 1), "range"), "start");
  require(strtol(field(start, "character"), NULL, 10) == 24, "sequential UTF-16 incremental edits");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///incremental.hk\",\"version\":3},\"contentChanges\":["
    "{\"range\":{\"start\":{\"line\":2,\"character\":21},\"end\":{\"line\":2,\"character\":31}},\"text\":\"\"},"
    "{\"range\":{\"start\":{\"line\":99,\"character\":0},\"end\":{\"line\":99,\"character\":0}},\"text\":\"bad\"}]}}", 0);
  reply = receive();
  require(string_is(field(reply, "method"), "window/logMessage"), "invalid batch rejected");
  free(reply);
  /* Same version and range still succeed: neither the first edit nor the
   * version from the rejected batch may have been committed. */
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///incremental.hk\",\"version\":3},\"contentChanges\":["
    "{\"range\":{\"start\":{\"line\":2,\"character\":21},\"end\":{\"line\":2,\"character\":31}},\"text\":\"\"}]}}", 0);
  empty_diagnostics(3);

  char *callback_source = fixture(argv[2]);
  open_document("file:///type-error.hk", "def main(): Void\n  let value: Int = \"wrong\"\nend\n", 0);
  reply = receive();
  diagnostic = spaces(diagnostics(reply) + 1);
  require(strncmp(field(diagnostic, "code"), "\"NR1", 4) == 0, "binder type error");
  free(reply);
  open_document("file:///incomplete.hk", "def main(): Void\n  missing() do |value|\n", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete do block diagnostics");
  free(reply);
  open_document("file:///clock.hk", "use clock\ndef main(): Void\nend\n", 0);
  empty_diagnostics(1);

  const char *unit = "abc😀é";
  char *large = calloc(4096 * strlen(unit) + 128, 1);
  require(large != NULL, "allocate large source");
  strcpy(large, "# ");
  char *next = large + 2;
  for (int index = 0; index < 4096; ++index) { memcpy(next, unit, strlen(unit)); next += strlen(unit); }
  strcpy(next, "\ndef main(): Void\nend\n");
  open_document("file:///large.hk", large, 1);
  empty_diagnostics(1);
  free(large);

  open_document("file:///hover.hk", "def one(value: Int): Int\n  return value\nend\n"
    "def main(): Void\n  let value = \"text\"\n  let copy = \"😀\" + value\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"parameter\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":1,\"character\":10}}}", 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"), "value: Int"),
          "hover uses the parameter binding");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"local\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":5,\"character\":21}}}", 0);
  reply = receive();
  const char *hover = field(reply, "result");
  require(string_is(field(field(hover, "contents"), "value"), "value: String"),
          "same-spelling local has its own inferred type");
  require(strtol(field(field(field(hover, "range"), "start"), "character"), NULL, 10) == 20,
          "hover range after non-BMP character uses UTF-16");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"parameter-definition\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":1,\"character\":10}}}", 0);
  reply = receive();
  const char *location = field(reply, "result");
  require(string_is(field(location, "uri"), "file:///hover.hk"), "definition source URI");
  start = field(field(location, "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 0 && strtol(field(start, "character"), NULL, 10) == 8,
          "parameter definition uses its declaration identity");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"local-definition\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":5,\"character\":21}}}", 0);
  reply = receive();
  start = field(field(field(reply, "result"), "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 4 && strtol(field(start, "character"), NULL, 10) == 6,
          "same-spelling local definition targets the identifier, not the let keyword");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/references\",\"id\":\"references\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":4,\"character\":7},"
    "\"context\":{\"includeDeclaration\":true}}}", 0);
  reply = receive();
  const char *references = field(reply, "result");
  require(*references == '[', "reference locations");
  location = spaces(references + 1);
  require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 4,
          "reference query on declaration includes declaration when requested");
  const char *next_location = spaces(skip_value(location, 0));
  require(*next_location == ',', "declaration has a reference");
  location = spaces(next_location + 1);
  require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 5 &&
          *spaces(skip_value(location, 0)) == ']', "references exclude another binding with the same spelling");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/references\",\"id\":\"uses-only\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":5,\"character\":21},"
    "\"context\":{\"includeDeclaration\":false}}}", 0);
  reply = receive();
  location = spaces(field(reply, "result") + 1);
  require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 5 &&
          *spaces(skip_value(location, 0)) == ']', "references honor includeDeclaration=false");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentHighlight\",\"id\":\"highlights\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":5,\"character\":21}}}", 0);
  reply = receive();
  location = spaces(field(reply, "result") + 1);
  require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 4,
          "highlight includes the resolved declaration");
  next_location = spaces(skip_value(location, 0));
  require(*next_location == ',', "highlight includes the use");
  location = spaces(next_location + 1);
  require(strtol(field(field(field(location, "range"), "start"), "character"), NULL, 10) == 20 &&
          *spaces(skip_value(location, 0)) == ']', "UTF-16 highlights exclude same-spelling bindings");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"declaration-hover\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":4,\"character\":7}}}", 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"), "value: String"),
          "declaration hover exposes the binder's inferred type");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"file:///hover.hk\",\"version\":2},\"contentChanges\":[{\"text\":\"def main(\"}]}}", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "invalid edit diagnostic");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"invalidated\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///hover.hk\"},\"position\":{\"line\":0,\"character\":5}}}", 0);
  reply = receive();
  require(strncmp(field(reply, "result"), "null", 4) == 0, "invalid edit cannot expose an old semantic model");
  free(reply);

  if (argc == 4) benchmark(callback_source);
  free(callback_source);

  open_document("file:///inherited.hk", "class Base\n  public value: Int = 1\n"
    "  public def read(): Int\n    return this.value\n  end\nend\n"
    "class Derived : Base\nend\ndef main(): Void\n  let item = new Derived()\n"
    "  let field = item.value\n  let result = item.read()\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"inherited-field\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///inherited.hk\"},\"position\":{\"line\":10,\"character\":20}}}", 0);
  reply = receive();
  start = field(field(field(reply, "result"), "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 1 && strtol(field(start, "character"), NULL, 10) == 9,
          "inherited field resolves to its declaring class's identifier");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"inherited-method\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///inherited.hk\"},\"position\":{\"line\":11,\"character\":21}}}", 0);
  reply = receive();
  start = field(field(field(reply, "result"), "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 2 && strtol(field(start, "character"), NULL, 10) == 13,
    "inherited call resolves to the actual method declaration");
  free(reply);

  open_document("file:///capture.hk", "def main(): Void\n  let value = 1\n"
    "  let callback: fn(Int): Int = fn(arg)\n    let local = arg\n"
    "    return value + value + local\n  end\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/references\",\"id\":\"captures\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///capture.hk\"},\"position\":{\"line\":1,\"character\":7},"
    "\"context\":{\"includeDeclaration\":false}}}", 0);
  reply = receive();
  references = field(reply, "result");
  int capture_count = 0;
  location = spaces(references + 1);
  while (*location != ']') {
    require(strtol(field(field(field(location, "range"), "start"), "line"), NULL, 10) == 4,
            "captured references target user source, not generated closure fields");
    ++capture_count;
    location = spaces(skip_value(location, 0));
    if (*location == ',') location = spaces(location + 1);
  }
  require(capture_count == 2, "initial and repeated captures are indexed without duplicates");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"callback-parameter\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///capture.hk\"},\"position\":{\"line\":3,\"character\":17}}}", 0);
  reply = receive();
  start = field(field(field(reply, "result"), "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 2 && strtol(field(start, "character"), NULL, 10) == 34,
          "inferred callback parameter has a source declaration");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"callback-local\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///capture.hk\"},\"position\":{\"line\":4,\"character\":28}}}", 0);
  reply = receive();
  start = field(field(field(reply, "result"), "range"), "start");
  require(strtol(field(start, "line"), NULL, 10) == 3 && strtol(field(start, "character"), NULL, 10) == 8,
          "nested callback binder retains local source symbols");
  free(reply);

  finish_server();
  use_documented_stdlib = 0;
  documentation_incomplete_and_no_sidecar_contract(argv[1], argv[2]);
  use_documented_stdlib = 1;
  documentation_disabled_contract(argv[1], argv[2]);
  automatic_project_contract(argv[1], argv[2]);
  project_contract(argv[1], argv[2]);
  glob_project_contract(argv[1], argv[2]);
  puts("LSP lifecycle, synchronization, diagnostics, navigation, highlights, symbols, completion and signature help passed.");
  return 0;
}
