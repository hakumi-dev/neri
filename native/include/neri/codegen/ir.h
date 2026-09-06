#ifndef NERI_CODEGEN_IR_H
#define NERI_CODEGEN_IR_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neri::codegen {

struct version final {
  std::uint16_t major{};
  std::uint16_t minor{};
};

struct symbol_id final {
  std::string module;
  std::uint8_t kind{};
  std::string semantic_name;
};

struct source_location final {
  std::string source;
  std::uint32_t utf8_start{};
  std::uint32_t utf8_length{};
};

struct type final {
  std::uint8_t tag{};
  std::optional<symbol_id> symbol;
  std::vector<type> arguments;
  std::uint32_t element_count{};
};

struct constant final {
  std::uint8_t tag{};
  std::uint64_t bits{};
  std::vector<type> types;
  std::vector<constant> nested;
  std::optional<symbol_id> symbol;
  std::string text;
};

struct source final {
  std::string id;
  std::vector<std::uint8_t> utf8;
};

struct field final {
  symbol_id id;
  type value_type;
  std::uint8_t access{};
  std::optional<source_location> location;
};

struct method final {
  symbol_id function_id;
  std::uint8_t dispatch{};
  std::optional<symbol_id> dispatch_slot;
  std::optional<source_location> location;
};

struct class_declaration final {
  symbol_id id;
  std::optional<symbol_id> base;
  std::uint8_t access{};
  std::vector<field> fields;
  std::vector<method> methods;
  std::optional<source_location> location;
};

struct native_record final {
  symbol_id id;
  bool is_union{};
  std::vector<field> fields;
};

struct global_declaration final {
  symbol_id id;
  type value_type;
  constant initializer;
  std::uint8_t linkage{};
  std::optional<source_location> location;
};

struct runtime_requirement final {
  std::uint16_t abi_major{};
  std::uint16_t abi_minor{};
  std::uint64_t feature_bits{};
};

struct import_declaration final {
  symbol_id id;
  std::vector<type> parameter_types;
  type result_type;
  std::uint8_t kind{};
  std::string link_name;
  std::string native_library;
  std::uint32_t effects{};
  std::optional<runtime_requirement> minimum_runtime;
  std::optional<source_location> location;
};

struct value_definition final {
  std::uint32_t id{};
  type value_type;
};

struct block_parameter final {
  std::uint32_t id{};
  type value_type;
  std::optional<source_location> location;
};

struct edge final {
  std::uint32_t target{};
  std::vector<std::uint32_t> arguments;
};

struct instruction final {
  std::uint16_t opcode{};
  std::vector<value_definition> results;
  std::vector<std::uint32_t> operands;
  std::vector<type> type_arguments;
  std::optional<symbol_id> symbol;
  std::optional<constant> constant_value;
  std::optional<std::uint8_t> predicate;
  bool flag{};
  std::optional<source_location> location;
};

struct terminator final {
  std::uint8_t tag{};
  std::optional<std::uint32_t> condition;
  std::vector<edge> edges;
  std::optional<std::uint32_t> return_value;
  std::string panic_code;
  std::string panic_message;
  std::optional<source_location> location;
};

struct block final {
  std::uint32_t id{};
  std::vector<block_parameter> parameters;
  std::vector<instruction> instructions;
  terminator ending;
};

struct debug_local final {
  std::string name;
  std::uint32_t value{};
  source_location location;
};

struct function final {
  symbol_id id;
  std::vector<type> parameter_types;
  type result_type;
  std::uint8_t kind{};
  std::uint32_t effects{};
  bool unsafe_call{};
  std::uint32_t entry_block{};
  std::optional<value_definition> unsafe_root;
  std::optional<symbol_id> declaring_class;
  std::optional<symbol_id> dispatch_slot;
  std::optional<source_location> location;
  std::vector<block> blocks;
  std::vector<debug_local> debug_locals;
};

struct ir_module final {
  version semantic_version;
  std::string id;
  std::vector<std::string> required_features;
  std::vector<source> sources;
  std::vector<class_declaration> classes;
  std::vector<global_declaration> globals;
  std::vector<import_declaration> imports;
  std::vector<function> functions;
  std::vector<native_record> native_records;
};

} // namespace neri::codegen

#endif
