static char *resolve_completion_item(const char *item, const char *id) {
  const char *end = skip_value(item, 0);
  size_t size = (size_t)(end - item) + strlen(id) + 96;
  char *message = malloc(size);
  require(message != NULL, "allocate completion resolve request");
  snprintf(message, size, "{\"jsonrpc\":\"2.0\",\"method\":\"completionItem/resolve\",\"id\":\"%s\",\"params\":%.*s}",
           id, (int)(end - item), item);
  send_message(message, 0);
  free(message);
  return receive();
}

static void documentation_contract(void) {
  open_document("file:///documentation.hk",
    "## Greets a person by name.\n"
    "## The generated signature remains authoritative.\n"
    "def greeting(name: String): String\n"
    "  return name\n"
    "end\n"
    "## This comment is detached.\n"
    "\n"
    "def orphan(): Int\n"
    "  return 1\n"
    "end\n"
    "def main(): Void\n"
    "  let message = greeting(\"Neri\")\n"
    "  let untouched = orphan()\n"
    "end\n", 0);
  empty_diagnostics(1);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"documented-hover\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation.hk\"},"
    "\"position\":{\"line\":11,\"character\":19}}}", 0);
  char *reply = receive();
  const char *contents = field(field(reply, "result"), "contents");
  require(string_is(field(contents, "kind"), "plaintext") &&
          string_is(field(contents, "value"),
            "def greeting(name: String): String\\u000a\\u000aGreets a person by name.\\u000aThe generated signature remains authoritative."),
          "hover combines a semantic signature with contiguous source documentation");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"parameter-doc-isolation\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation.hk\"},"
    "\"position\":{\"line\":2,\"character\":14}}}", 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"), "name: String"),
          "callable documentation is not attributed to a parameter on the declaration line");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"detached-hover\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation.hk\"},"
    "\"position\":{\"line\":12,\"character\":21}}}", 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"), "def orphan(): Int"),
          "a blank line prevents documentation from attaching to a declaration");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"documented-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation.hk\"},"
    "\"position\":{\"line\":11,\"character\":19}}}", 0);
  reply = receive();
  const char *item = completion_item(field(reply, "result"), "greeting");
  require(item != NULL && strstr(item, "\"documentation\"") == NULL,
          "completion documentation is resolved lazily");
  const char *data = field(item, "data");
  require(string_is(field(data, "uri"), "file:///documentation.hk") &&
          string_is(field(data, "identity"), "callable:greeting") &&
          strtol(field(data, "version"), NULL, 10) == 1 &&
          strtol(field(data, "revision"), NULL, 10) > 0,
          "completion data identifies the exact semantic analysis revision");
  char *resolved_reply = resolve_completion_item(item, "documented-resolve");
  const char *resolved = field(resolved_reply, "result");
  require(string_is(field(resolved, "detail"), "greeting(name: String): String") &&
          string_is(field(field(resolved, "documentation"), "value"),
                    "Greets a person by name.\\u000aThe generated signature remains authoritative."),
          "completion resolve preserves semantic detail and adds only documentation");
  free(resolved_reply);
  free(reply);

  open_document("file:///documentation-stdlib.hk",
    "use clock\n"
    "def main(): Void\n"
    "  let value = clock.milliseconds()\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"stdlib-docs\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation-stdlib.hk\"},"
    "\"position\":{\"line\":2,\"character\":24}}}", 0);
  reply = receive();
  require(string_is(field(field(field(reply, "result"), "contents"), "value"),
            "def clock.milliseconds(): Int?\\u000a\\u000aReturns an arbitrary monotonic epoch in milliseconds.\\u000aSuitable for elapsed time and deadlines, not wall-clock time."),
          "standard-library source documentation is available through the semantic model");
  free(reply);

  open_document("file:///documentation-stdlib-incomplete.hk",
    "use clock\n"
    "def main(): Void\n"
    "  clock.\n"
    "end\n", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete standard-library member reports its parse diagnostic");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"stdlib-incomplete-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation-stdlib-incomplete.hk\"},"
    "\"position\":{\"line\":2,\"character\":8}}}", 0);
  reply = receive();
  item = completion_item(field(reply, "result"), "milliseconds");
  require(item != NULL, "semantic repair completes an incomplete standard-library namespace");
  resolved_reply = resolve_completion_item(item, "stdlib-incomplete-resolve");
  require(string_is(field(field(field(resolved_reply, "result"), "documentation"), "value"),
                    "Returns an arbitrary monotonic epoch in milliseconds.\\u000aSuitable for elapsed time and deadlines, not wall-clock time."),
          "incomplete completion resolves standard-library prose from the matching sidecar snapshot");
  free(resolved_reply);
  free(reply);
}

static void documentation_incomplete_and_no_sidecar_contract(const char *compiler, const char *root) {
  start_server(compiler, root);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1,\"params\":{"
    "\"capabilities\":{\"textDocument\":{\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},"
    "\"completion\":{\"completionItem\":{\"documentationFormat\":[\"markdown\",\"plaintext\"]}}}}}}", 0);
  char *reply = receive();
  free(reply);
  open_document("file:///documentation-incomplete.hk",
    "class Reader\n"
    "  ## Reads **one** value.\n"
    "  public def read(): Int\n"
    "    return 1\n"
    "  end\n"
    "end\n"
    "def main(): Void\n"
    "  let item = new Reader()\n"
    "  item.\n"
    "end\n", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "incomplete member source reports its parse diagnostic");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"incomplete-doc-completion\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation-incomplete.hk\"},"
    "\"position\":{\"line\":8,\"character\":7}}}", 0);
  reply = receive();
  const char *item = completion_item(field(reply, "result"), "read");
  require(item != NULL, "semantic repair completes a member in incomplete current text");
  char *resolved_reply = resolve_completion_item(item, "incomplete-doc-resolve");
  const char *documentation = field(field(resolved_reply, "result"), "documentation");
  require(string_is(field(documentation, "kind"), "markdown") &&
          string_is(field(documentation, "value"), "Reads \\\\*\\\\*one\\\\*\\\\* value."),
          "completion resolve repairs current text and escapes source prose for markdown");
  free(resolved_reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":[]}}", 0);
  char *diagnostic_reply = receive();
  require(*spaces(diagnostics(diagnostic_reply) + 1) == '{', "watched change reanalyzes the incomplete document");
  free(diagnostic_reply);
  resolved_reply = resolve_completion_item(item, "stale-doc-resolve");
  require(strstr(field(resolved_reply, "result"), "\"documentation\"") == NULL,
          "completion resolve rejects an item from an older analysis revision");
  free(resolved_reply);
  free(reply);

  open_document("file:///documentation-no-sidecar.hk",
    "use clock\n"
    "def main(): Void\n"
    "  let value = clock.milliseconds()\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"no-sidecar-hover\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation-no-sidecar.hk\"},"
    "\"position\":{\"line\":2,\"character\":24}}}", 0);
  reply = receive();
  const char *value = field(field(field(reply, "result"), "contents"), "value");
  require(string_is(value, "```neri\\u000adef clock.milliseconds(): Int?\\u000a```") &&
          strstr(value, "monotonic") == NULL,
          "a no-doc toolchain preserves the standard-library signature without prose");
  free(reply);
  finish_server();
}

static void documentation_disabled_contract(const char *compiler, const char *root) {
  start_server(compiler, root);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1,\"params\":{"
    "\"capabilities\":{\"textDocument\":{\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},"
    "\"completion\":{\"completionItem\":{\"documentationFormat\":[\"markdown\",\"plaintext\"]}}}},"
    "\"initializationOptions\":{\"documentation\":false}}}", 0);
  char *reply = receive();
  require(strncmp(field(field(field(reply, "result"), "capabilities"), "completionProvider"), "{", 1) == 0,
          "documentation setting preserves completion capability");
  free(reply);
  open_document("file:///documentation-disabled.hk",
    "## Hidden prose.\n"
    "def answer(): Int\n"
    "  return 42\n"
    "end\n"
    "def main(): Void\n"
    "  let value = answer()\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/hover\",\"id\":\"disabled-hover\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///documentation-disabled.hk\"},"
    "\"position\":{\"line\":5,\"character\":17}}}", 0);
  reply = receive();
  const char *contents = field(field(reply, "result"), "contents");
  require(string_is(field(contents, "kind"), "markdown") &&
          string_is(field(contents, "value"), "```neri\\u000adef answer(): Int\\u000a```") &&
          strstr(contents, "Hidden prose") == NULL,
          "documentation=false hides prose but preserves negotiated semantic signatures");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"completionItem/resolve\",\"id\":\"disabled-resolve\","
    "\"params\":{\"label\":\"answer\",\"detail\":\"answer(): Int\","
    "\"documentation\":{\"kind\":\"plaintext\",\"value\":\"Cached prose.\"},"
    "\"data\":{\"uri\":\"file:///documentation-disabled.hk\",\"identity\":\"callable:answer\",\"version\":1}}}", 0);
  reply = receive();
  require(strstr(field(reply, "result"), "\"documentation\"") == NULL &&
          string_is(field(field(reply, "result"), "detail"), "answer(): Int"),
          "documentation=false keeps completion types and signatures without prose");
  free(reply);
  finish_server();
}
