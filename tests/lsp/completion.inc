/* Included by protocol.c after its JSON and transport helpers. */
static const char *completion_item(const char *items, const char *label) {
  const char *item = spaces(items + 1);
  while (*item != ']') {
    require(*item == '{', "completion item object");
    if (string_is(field(item, "label"), label)) return item;
    item = spaces(skip_value(item, 0));
    if (*item == ',') item = spaces(item + 1);
    else require(*item == ']', "completion item separator");
  }
  return NULL;
}

static void completion_contract(void) {
  open_document("file:///completion.hk",
    "class Base\n"
    "  public inherited: Int = 1\n"
    "end\n"
    "class Widget : Base\n"
    "  public value: Int = 2\n"
    "  private secret: Int = 3\n"
    "  public def read(): Int\n"
    "    return this.value\n"
    "  end\n"
    "  public static def create(): Widget\n"
    "    return new Widget()\n"
    "  end\n"
    "end\n"
    "def helper(label: String, value: Widget): Void\nend\n"
    "def scope(param: Int): Void\n"
    "  let item = new Widget()\n"
    "  if true\n"
    "    let inner = param\n"
    "    let outer = item\n"
    "    helper(\"😀\", outer)\n"
    "  end\n"
    "  let result = item.value\n"
    "  let made = Widget.create()\n"
    "  helper(\"ok\", item)\n"
    "end\n"
    "def main(): Void\n"
    "  scope(1)\n"
    "end\n", 0);
  empty_diagnostics(1);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"locals\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":20,\"character\":19}}}", 0);
  char *reply = receive();
  const char *items = field(reply, "result");
  const char *item = completion_item(items, "outer");
  require(item != NULL && string_is(field(item, "detail"), "outer: Widget"),
          "completion uses the inferred local type from the active scope");
  const char *edit = field(item, "textEdit");
  const char *edit_range = field(edit, "range");
  require(strtol(field(field(edit_range, "start"), "character"), NULL, 10) == 17 &&
          strtol(field(field(edit_range, "end"), "character"), NULL, 10) == 22 &&
          string_is(field(edit, "newText"), "outer"),
          "completion replaces the whole identifier after non-BMP UTF-16 text");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"locals-all\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":20,\"character\":17}}}", 0);
  reply = receive();
  items = field(reply, "result");
  require(completion_item(items, "outer") != NULL && completion_item(items, "inner") != NULL &&
          completion_item(items, "param") != NULL,
          "completion includes block locals and callable parameters");
  item = completion_item(items, "helper");
  require(item != NULL && strtol(field(item, "kind"), NULL, 10) == 3 &&
          string_is(field(item, "detail"), "helper(label: String, value: Widget): Void"),
          "function completion presents its kind and signature without declaration syntax");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"scope\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":24,\"character\":15}}}", 0);
  reply = receive();
  items = field(reply, "result");
  require(completion_item(items, "inner") == NULL && completion_item(items, "outer") == NULL &&
          completion_item(items, "item") != NULL && completion_item(items, "param") != NULL,
          "completion excludes locals after their lexical scope ends");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"members\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":22,\"character\":20}}}", 0);
  reply = receive();
  items = field(reply, "result");
  require(completion_item(items, "value") != NULL && completion_item(items, "read") != NULL &&
          completion_item(items, "inherited") != NULL,
          "instance completion follows the bound receiver type and inheritance");
  require(completion_item(items, "secret") == NULL && completion_item(items, "create") == NULL &&
          completion_item(items, "init") == NULL,
          "instance completion excludes inaccessible, static and constructor members");
  item = completion_item(items, "read");
  require(item != NULL && strtol(field(item, "kind"), NULL, 10) == 2 &&
          string_is(field(item, "detail"), "Widget.read(): Int"),
          "method completion presents its kind and signature without declaration syntax");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"static\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":23,\"character\":22}}}", 0);
  reply = receive();
  items = field(reply, "result");
  require(completion_item(items, "create") != NULL && completion_item(items, "read") == NULL &&
          completion_item(items, "value") == NULL,
          "class completion exposes only static methods");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"type\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion.hk\"},"
    "\"position\":{\"line\":16,\"character\":20}}}", 0);
  reply = receive();
  require(completion_item(field(reply, "result"), "Widget") != NULL,
          "new-expression completion resolves constructible semantic classes");
  free(reply);

  open_document("file:///completion-callback.hk",
    "def main(): Void\n"
    "  let captured = \"outer\"\n"
    "  let callback: fn(): Void = fn()\n"
    "    let nested = 1\n"
    "    let copy = captured\n"
    "  end\n"
    "  callback()\n"
    "end\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"callback-scope\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion-callback.hk\"},"
    "\"position\":{\"line\":4,\"character\":15}}}", 0);
  reply = receive();
  items = field(reply, "result");
  item = completion_item(items, "captured");
  require(item != NULL && string_is(field(item, "detail"), "captured: String") &&
          completion_item(items, "nested") != NULL && completion_item(items, "callback") == NULL,
          "callback completion includes capturable outer state and excludes a not-yet-declared local");
  free(reply);

  open_document("file:///completion-incomplete.hk",
    "class Tiny\n"
    "  public count: Int = 1\n"
    "  public def read(): Int\n"
    "    return this.count\n"
    "  end\n"
    "end\n"
    "def main(): Void\n"
    "  let item = new Tiny()\n"
    "  item.\n"
    "end\n", 0);
  reply = receive();
  require(*spaces(diagnostics(reply) + 1) == '{', "bare member access has parser diagnostics");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"incomplete-member\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion-incomplete.hk\"},"
    "\"position\":{\"line\":8,\"character\":7}}}", 0);
  reply = receive();
  items = field(reply, "result");
  require(completion_item(items, "count") != NULL && completion_item(items, "read") != NULL,
          "completion repairs and binds current bare-dot text without a stale model");
  free(reply);

  open_document("file:///completion-builtins.hk",
    "# Qualified library call\ndef main(): Void\n  console::println(\"ok\")\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/completion\",\"id\":\"builtins\","
    "\"params\":{\"textDocument\":{\"uri\":\"file:///completion-builtins.hk\"},"
    "\"position\":{\"line\":2,\"character\":11}}}", 0);
  reply = receive();
  items = field(reply, "result");
  item = completion_item(items, "read");
  require(completion_item(items, "print") != NULL && completion_item(items, "println") != NULL &&
          item != NULL && string_is(field(item, "detail"), "console::read(): String"),
          "library completion is rendered from source callable signatures");
  free(reply);
}
