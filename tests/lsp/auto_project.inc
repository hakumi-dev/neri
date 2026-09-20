static void initialize_project_workspace(const char *root) {
  char *uri = file_uri(root);
  char message[8192];
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1,"
    "\"params\":{\"rootUri\":\"%s\",\"capabilities\":{}}}", uri);
  free(uri);
  send_message(message, 0);
  free(receive());
}

static void automatic_project_contract(const char *compiler, const char *root) {
  char path[4096], message[16384];
  start_server(compiler, root);
  initialize_project_workspace(root);

  snprintf(path, sizeof(path), "%s/tests/lsp/auto-project/main.hk", root);
  char *main_uri = file_uri(path);
  open_document(main_uri,
    "def main(): Void\n"
    "  let value = dependencies::\n"
    "end\n", 0);
  char *reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete automatic project source reports a parse diagnostic");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"auto-project-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":28}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  const char *items = field(reply, "result");
  require(completion_item(items, "left") != NULL && completion_item(items, "right") != NULL &&
          completion_item(items, "library") != NULL,
          "nearest project completion includes transitive deduplicated references");
  free(reply);

  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
    "\"textDocument\":{\"uri\":\"%s\",\"version\":2},\"contentChanges\":[{\"text\":"
    "\"def main(): Void\\n  let value: Int = dependencies::library()\\nend\\n\"}]}}", main_uri);
  send_message(message, 0);
  empty_diagnostics(2);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\",\"id\":\"auto-project-definition\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":35}}}", main_uri);
  send_message(message, 0);
  reply = receive();
  snprintf(path, sizeof(path), "%s/tests/lsp/auto-library/src/library.hk", root);
  char *library_uri = file_uri(path);
  require(string_is(field(field(reply, "result"), "uri"), library_uri),
          "automatic project navigation resolves a diamond reference source exactly once");
  free(reply);

  open_document(library_uri,
    "namespace dependencies\n"
    "def library(): String\n"
    "  return \"changed\"\n"
    "end\n", 0);
  empty_diagnostics(1);
  reply = receive();
  require(string_is(field(field(reply, "params"), "uri"), main_uri) &&
          *spaces(diagnostics(reply) + 1) == '{',
          "an unsaved referenced-project edit invalidates its open consumer");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}", library_uri);
  send_message(message, 0);
  empty_diagnostics(1);
  empty_diagnostics(2);
  free(library_uri);
  free(main_uri);
  finish_server();

  start_server(compiler, root);
  initialize_project_workspace(root);
  snprintf(path, sizeof(path), "%s/tests/lsp/auto-left/src/left.hk", root);
  char *left_uri = file_uri(path);
  open_document(left_uri,
    "namespace dependencies\n"
    "def left(): Int\n"
    "  return dependencies::\n"
    "end\n", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete library project source reports its parse diagnostic");
  free(reply);
  snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"library-project-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":2,\"character\":23}}}", left_uri);
  send_message(message, 0);
  reply = receive();
  require(completion_item(field(reply, "result"), "library") != NULL,
          "completion repair preserves project mode without requiring a main entry point");
  free(reply);
  free(left_uri);
  finish_server();

  start_server(compiler, root);
  initialize_project_workspace(root);
  snprintf(path, sizeof(path), "%s/tests/lsp/auto-cycle-a/main.hk", root);
  char *cycle_uri = file_uri(path);
  open_document(cycle_uri, "def main(): Void\nend\n", 0);
  reply = receive();
  require(string_is(field(spaces(diagnostics(reply) + 1), "code"), "NR_PROJECT"),
          "project reference cycles are visible document diagnostics");
  free(reply);
  reply = receive();
  require(string_is(field(reply, "method"), "window/logMessage"), "project configuration error is also logged");
  free(reply);

  snprintf(path, sizeof(path), "%s/tests/lsp/auto-reference-main/main.hk", root);
  char *reference_uri = file_uri(path);
  open_document(reference_uri, "def main(): Void\nend\n", 0);
  reply = receive();
  require(string_is(field(spaces(diagnostics(reply) + 1), "code"), "NR_PROJECT_ENTRY"),
          "a referenced project main is rejected in the consumer project context");
  free(reply);
  free(reference_uri);
  free(cycle_uri);
  finish_server();
}
