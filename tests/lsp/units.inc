/* Version-2 unit contracts. Included by protocol.c after completion helpers. */
static int unit_cli_status(const char *compiler, const char *manifest, const char *unit) {
  pid_t child = fork();
  require(child >= 0, "unit CLI process");
  if (child == 0) {
    if (unit)
      execl(compiler, compiler, "check", "--project", manifest, "--unit", unit, (char *)NULL);
    else
      execl(compiler, compiler, "check", "--project", manifest, (char *)NULL);
    _exit(127);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status), "unit CLI process exits");
  return WEXITSTATUS(status);
}

static void unit_cli_contract(const char *compiler, const char *root) {
  char manifest[4096];
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/units/neri.json", root);
  require(project_cli_status(compiler, "check", manifest) == 0,
          "the default executable unit resolves its library reference");
  require(unit_cli_status(compiler, manifest, "core") == 0,
          "--unit selects a library and excluded sources stay outside its ownership");

  const char *invalid[] = {"missing", "cycle", "overlap"};
  for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
    snprintf(manifest, sizeof(manifest), "%s/tests/lsp/units-invalid/%s/neri.json",
             root, invalid[index]);
    require(unit_cli_status(compiler, manifest, NULL) == 2,
            "invalid unit references and ownership are configuration errors");
  }
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/units-invalid/library-main/neri.json", root);
  require(unit_cli_status(compiler, manifest, NULL) == 1,
          "library units reject their own main entry point");
  snprintf(manifest, sizeof(manifest), "%s/tests/lsp/units-invalid/two-main/neri.json", root);
  require(unit_cli_status(compiler, manifest, NULL) == 1,
          "executable units reject multiple entry points");
}

static void unit_project_contract(const char *compiler, const char *root) {
  char path[4096], message[16384];
  start_server(compiler, root);
  snprintf(path, sizeof(path), "%s/tests/lsp/units", root);
  initialize_project_workspace(path);

  snprintf(path, sizeof(path), "%s/tests/lsp/units/core/api.hk", root);
  char *api_uri = file_uri(path);
  open_document(api_uri,
    "namespace shared\n\n"
    "def answer(): Int\n"
    "  return 42\n"
    "end\n", 0);
  empty_diagnostics(1);

  snprintf(path, sizeof(path), "%s/tests/lsp/units/web/main.hk", root);
  char *main_uri = file_uri(path);
  open_document(main_uri,
    "use shared\n\n"
    "def main(): Void\n"
    "  let model = new Model()\n"
    "  let value: Int = model.value + answer()\n"
    "end\n", 0);
  empty_diagnostics(1);

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"unit-definition\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":4,\"character\":35}}}", main_uri);
  send_message(message, 0);
  char *reply = receive();
  require(string_is(field(field(reply, "result"), "uri"), api_uri),
          "definition crosses an explicit unit reference and namespace");
  free(reply);

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":2},\"contentChanges\":[{\"text\":"
    "\"def main(): Void\\n  let value = shared::\\nend\\n\"}]}}", main_uri);
  send_message(message, 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete qualified unit source reports diagnostics");
  free(reply);
  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"unit-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":22}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  const char *items = field(reply, "result");
  require(completion_item(items, "answer") != NULL,
          "completion crosses an explicit unit reference without folder-derived namespaces");
  free(reply);

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":3},\"contentChanges\":[{\"text\":"
    "\"use shared\\n\\ndef main(): Void\\n  let model = new Model()\\n  let value: Int = model.value + answer()\\nend\\n\"}]}}", main_uri);
  send_message(message, 0);
  empty_diagnostics(3);

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":2},\"contentChanges\":[{\"text\":"
    "\"namespace shared\\n\\ndef answer(): String\\n  return \\\"changed\\\"\\nend\\n\"}]}}", api_uri);
  send_message(message, 0);
  int changed_api = 0, invalidated_main = 0;
  for (int publication = 0; publication < 2; ++publication) {
    reply = receive();
    const char *params = field(reply, "params");
    const char *published = diagnostics(reply);
    if (string_is(field(params, "uri"), api_uri)) {
      require(strtol(field(params, "version"), NULL, 10) == 2 &&
              *published == '[' && *spaces(published + 1) == ']',
              "the edited library unit remains valid");
      changed_api = 1;
    } else if (string_is(field(params, "uri"), main_uri)) {
      require(strtol(field(params, "version"), NULL, 10) == 3 &&
              *spaces(published + 1) == '{',
              "an unsaved library-unit edit invalidates its executable consumer");
      invalidated_main = 1;
    } else {
      require(0, "unit dependency diagnostic URI");
    }
    free(reply);
  }
  require(changed_api && invalidated_main, "dependency edit publishes both affected documents");

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}", api_uri);
  send_message(message, 0);
  int closed_api = 0, restored_main = 0;
  for (int publication = 0; publication < 2; ++publication) {
    reply = receive();
    const char *params = field(reply, "params");
    const char *published = diagnostics(reply);
    require(*published == '[' && *spaces(published + 1) == ']',
            "closing an unsaved unit dependency restores valid disk analysis");
    if (string_is(field(params, "uri"), api_uri)) {
      require(strtol(field(params, "version"), NULL, 10) == 2, "closed dependency version");
      closed_api = 1;
    } else if (string_is(field(params, "uri"), main_uri)) {
      require(strtol(field(params, "version"), NULL, 10) == 3, "restored consumer version");
      restored_main = 1;
    } else {
      require(0, "unit close diagnostic URI");
    }
    free(reply);
  }
  require(closed_api && restored_main, "dependency close publishes both affected documents");

  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":4},\"contentChanges\":[{\"text\":"
    "\"use shared\\n\\ndef main(): Void\\n  let model = new Model()\\n  let value: Int = model.value + answer() + later()\\nend\\n\"}]}}", main_uri);
  send_message(message, 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "new directory source is initially absent");
  free(reply);

  snprintf(dynamic_project_source, sizeof(dynamic_project_source),
           "%s/tests/lsp/units/web/dynamic/helper.hk", root);
  require(unlink(dynamic_project_source) == 0 || errno == ENOENT, "reset dynamic unit source");
  FILE *file = fopen(dynamic_project_source, "wb");
  require(file != NULL && fputs("namespace shared\n\ndef later(): Int\n  return 1\nend\n", file) >= 0 &&
          fclose(file) == 0, "create dynamic unit source");
  char *helper_uri = file_uri(dynamic_project_source);
  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
    "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":1}]}}", helper_uri);
  send_message(message, 0);
  empty_diagnostics(4);
  require(unlink(dynamic_project_source) == 0, "delete dynamic unit source");
  dynamic_project_source[0] = 0;
  snprintf(message, sizeof(message),
    "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
    "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":3}]}}", helper_uri);
  send_message(message, 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{',
          "recursive unit membership is reloaded after source deletion");
  free(reply);

  free(helper_uri);
  free(main_uri);
  free(api_uri);
  finish_server();
}
