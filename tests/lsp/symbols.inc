/* Included by protocol.c after its JSON and transport helpers. */
static const char *document_symbol_at(const char *symbols, int index) {
  const char *symbol = spaces(symbols + 1);
  for (int current = 0; current < index; ++current) {
    require(*symbol == '{', "document symbol entry");
    symbol = spaces(skip_value(symbol, 0));
    require(*symbol == ',', "document symbol separator");
    symbol = spaces(symbol + 1);
  }
  require(*symbol == '{', "document symbol at index");
  return symbol;
}

static void require_document_symbol_for(const char *symbols, int index, const char *uri,
                                        const char *name, long kind, long line, long character) {
  const char *symbol = document_symbol_at(symbols, index);
  const char *location = field(symbol, "location");
  const char *start = field(field(location, "range"), "start");
  require(string_is(field(symbol, "name"), name), "document symbol name");
  require(strtol(field(symbol, "kind"), NULL, 10) == kind, "document symbol kind");
  require(string_is(field(location, "uri"), uri), "document symbol URI");
  require(strtol(field(start, "line"), NULL, 10) == line &&
          strtol(field(start, "character"), NULL, 10) == character,
          "document symbol UTF-16 selection range");
}

static void require_document_symbol(const char *symbols, int index, const char *name,
                                    long kind, long line, long character) {
  require_document_symbol_for(symbols, index, "file:///symbols.hk", name, kind, line, character);
}

static void document_symbols_contract(void) {
  open_document("file:///symbols.hk", "# 😀\nclass Sample\n  public value: Int = 1\n"
      "  public def add(amount: Int): Int\n    let total = this.value + amount\n"
      "    return total\n  end\nend\ndef helper(text: String): Void\n"
      "  let copy = \"😀\" + text\nend\ndef main(): Void\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentSymbol\",\"id\":\"symbols\","
      "\"params\":{\"textDocument\":{\"uri\":\"file:///symbols.hk\"}}}", 0);
  char *reply = receive();
  const char *symbols = field(reply, "result");
  require(*symbols == '[', "document symbols result array");
  require_document_symbol(symbols, 0, "Sample", 5, 1, 6);
  require_document_symbol(symbols, 1, "value", 8, 2, 9);
  require_document_symbol(symbols, 2, "add", 6, 3, 13);
  require_document_symbol(symbols, 3, "amount", 13, 3, 17);
  require_document_symbol(symbols, 4, "total", 13, 4, 8);
  require_document_symbol(symbols, 5, "helper", 12, 8, 4);
  require_document_symbol(symbols, 6, "text", 13, 8, 11);
  require_document_symbol(symbols, 7, "copy", 13, 9, 6);
  require_document_symbol(symbols, 8, "main", 12, 11, 4);
  const char *after = spaces(skip_value(document_symbol_at(symbols, 8), 0));
  require(*after == ']', "document symbols have no generated or duplicate declarations");
  free(reply);

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentSymbol\",\"id\":\"closed-symbols\","
      "\"params\":{\"textDocument\":{\"uri\":\"file:///closed-symbols.hk\"}}}", 0);
  reply = receive();
  require(*field(reply, "result") == '[' && *spaces(field(reply, "result") + 1) == ']',
          "document symbols do not synthesize closed documents");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
      "\"params\":{\"textDocument\":{\"uri\":\"file:///symbols.hk\"}}}", 0);
  empty_diagnostics(1);

  open_document("file:///native-generic-symbols.hk", "struct Packet\n  tag: Int\nend\n"
      "union Selection\n  code: Int\nend\nclass Box<T>\n  public value: T\nend\n"
      "def identity<T>(input: T): T\n  return input\nend\ndef main(): Void\n"
      "  let box = new Box<Int>()\n  let number = identity<Int>(box.value)\nend\n", 0);
  empty_diagnostics(1);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/documentSymbol\",\"id\":\"native-generic-symbols\","
      "\"params\":{\"textDocument\":{\"uri\":\"file:///native-generic-symbols.hk\"}}}", 0);
  reply = receive();
  symbols = field(reply, "result");
  require_document_symbol_for(symbols, 0, "file:///native-generic-symbols.hk", "Packet", 23, 0, 7);
  require_document_symbol_for(symbols, 1, "file:///native-generic-symbols.hk", "tag", 8, 1, 2);
  require_document_symbol_for(symbols, 2, "file:///native-generic-symbols.hk", "Selection", 23, 3, 6);
  require_document_symbol_for(symbols, 3, "file:///native-generic-symbols.hk", "code", 8, 4, 2);
  require_document_symbol_for(symbols, 4, "file:///native-generic-symbols.hk", "Box", 5, 6, 6);
  require_document_symbol_for(symbols, 5, "file:///native-generic-symbols.hk", "value", 8, 7, 9);
  require_document_symbol_for(symbols, 6, "file:///native-generic-symbols.hk", "identity", 12, 9, 4);
  require_document_symbol_for(symbols, 7, "file:///native-generic-symbols.hk", "input", 13, 9, 16);
  require_document_symbol_for(symbols, 8, "file:///native-generic-symbols.hk", "main", 12, 12, 4);
  require_document_symbol_for(symbols, 9, "file:///native-generic-symbols.hk", "box", 13, 13, 6);
  require_document_symbol_for(symbols, 10, "file:///native-generic-symbols.hk", "number", 13, 14, 6);
  after = spaces(skip_value(document_symbol_at(symbols, 10), 0));
  require(*after == ']', "generic specializations do not duplicate source symbols");
  free(reply);
  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
      "\"params\":{\"textDocument\":{\"uri\":\"file:///native-generic-symbols.hk\"}}}", 0);
  empty_diagnostics(1);
}
