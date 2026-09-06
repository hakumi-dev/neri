static void signature_help_contract(void) {
  open_document("file:///signature.hk",
    "def render(text: String, times: Int): Void\nend\n"
    "def apply(value: Int, operation: fn(Int): Int): Int\n  return operation(value)\nend\n"
    "def twice(value: Int): Int\n  return value * 2\nend\n"
    "def main(): Void\n"
    "  render(\"value, still first\", 2)\n"
    "  apply(21, twice)\n"
    "end\n", 0);
  empty_diagnostics(1);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-second\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature.hk\"},\"position\":{\"line\":9,\"character\":31}}}", 0);
  char *reply = receive();
  const char *result = field(reply, "result");
  const char *signature = spaces(field(result, "signatures") + 1);
  require(string_is(field(signature, "label"), "def render(text: String, times: Int): Void"),
          "signature help uses the resolved callable declaration");
  require(string_is(field(spaces(field(signature, "parameters") + 1), "label"), "text: String"),
          "signature parameter labels are client-safe label substrings");
  require(strtol(field(result, "activeParameter"), NULL, 10) == 1,
          "signature help ignores a comma inside a string literal");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-callback\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature.hk\"},\"position\":{\"line\":10,\"character\":12}}}", 0);
  reply = receive();
  result = field(reply, "result");
  signature = spaces(field(result, "signatures") + 1);
  require(string_is(field(signature, "label"), "def apply(value: Int, operation: fn(Int):(Int)): Int"),
          "signature help preserves callback parameter types");
  require(strtol(field(result, "activeParameter"), NULL, 10) == 1,
          "signature help selects a callback argument");
  free(reply);

  open_document("file:///signature-library.hk",
    "use console\n"
    "def main(): Void\n"
    "  console.println(\"value\")\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-library\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature-library.hk\"},\"position\":{\"line\":2,\"character\":18}}}", 0);
  reply = receive();
  result = field(reply, "result");
  signature = spaces(field(result, "signatures") + 1);
  require(string_is(field(signature, "label"), "console.println(Any): Void"),
          "signature help uses the compiler intrinsic contract");
  free(reply);

  open_document("file:///signature-nested.hk",
    "def inner(text: String, times: Int): String\n  return text\nend\n"
    "def outer(value: String, fallback: Int): Void\nend\n"
    "def main(): Void\n"
    "  outer(inner(\"value\", 2), 3)\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-nested\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature-nested.hk\"},\"position\":{\"line\":6,\"character\":23}}}", 0);
  reply = receive();
  result = field(reply, "result");
  signature = spaces(field(result, "signatures") + 1);
  require(string_is(field(signature, "label"), "def inner(text: String, times: Int): String"),
          "signature help selects the innermost resolved call");
  free(reply);

  open_document("file:///signature-unknown-nested.hk",
    "def outer(value: String, fallback: Int): Void\nend\n"
    "def main(): Void\n"
    "  outer(unknown(\n"
    "end\n", 0);
  reply = receive();
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-unknown-nested\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature-unknown-nested.hk\"},\"position\":{\"line\":3,\"character\":16}}}", 0);
  reply = receive();
  require(strncmp(field(reply, "result"), "null", 4) == 0,
          "an unresolved inner call does not fall through to its outer signature");
  free(reply);

  open_document("file:///signature-incomplete.hk",
    "def render(text: String, times: Int): Void\nend\n"
    "def main(): Void\n"
    "  render(\n"
    "end\n", 0);
  reply = receive();
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/signatureHelp\",\"id\":\"signature-incomplete\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///signature-incomplete.hk\"},\"position\":{\"line\":3,\"character\":9}}}", 0);
  reply = receive();
  result = field(reply, "result");
  signature = spaces(field(result, "signatures") + 1);
  require(string_is(field(signature, "label"), "def render(text: String, times: Int): Void"),
          "signature help repairs the current incomplete call without stale semantics");
  require(strtol(field(result, "activeParameter"), NULL, 10) == 0,
          "signature help selects the first incomplete argument");
  free(reply);
}
