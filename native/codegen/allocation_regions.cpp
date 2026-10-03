#include "allocation_regions.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <vector>

namespace neri::codegen {
namespace {
auto key(const symbol_id &symbol) {
  return std::tuple(symbol.module, symbol.kind, symbol.semantic_name);
}

bool scalar_boundary(const type &value) {
  switch (value.tag) {
  case NERI_IR_TYPE_VOID_V1:
  case NERI_IR_TYPE_BOOL_V1:
  case NERI_IR_TYPE_BYTE_V1:
  case NERI_IR_TYPE_INT_V1:
  case NERI_IR_TYPE_FLOAT_V1:
  case NERI_IR_TYPE_INT32_V1:
  case NERI_IR_TYPE_UINT32_V1:
  case NERI_IR_TYPE_UINT64_V1:
  case NERI_IR_TYPE_FLOAT32_V1:
    return true;
  case NERI_IR_TYPE_OPTIONAL_V1:
    return scalar_boundary(value.arguments.front());
  default:
    return false;
  }
}

bool local_operation(neri_ir_opcode_v1 opcode) {
  // A whitelist makes future operations conservative by default. These
  // operations only see SSA inputs, fresh allocations and immutable literals.
  switch (opcode) {
  case NERI_IR_OPCODE_CONSTANT_V1:
  case NERI_IR_OPCODE_INT_NEG_CHECKED_V1:
  case NERI_IR_OPCODE_INT_ADD_CHECKED_V1:
  case NERI_IR_OPCODE_INT_SUB_CHECKED_V1:
  case NERI_IR_OPCODE_INT_MUL_CHECKED_V1:
  case NERI_IR_OPCODE_INT_DIV_CHECKED_V1:
  case NERI_IR_OPCODE_FLOAT_NEG_V1:
  case NERI_IR_OPCODE_FLOAT_ADD_V1:
  case NERI_IR_OPCODE_FLOAT_SUB_V1:
  case NERI_IR_OPCODE_FLOAT_MUL_V1:
  case NERI_IR_OPCODE_FLOAT_DIV_V1:
  case NERI_IR_OPCODE_BOOL_NOT_V1:
  case NERI_IR_OPCODE_BOOL_AND_V1:
  case NERI_IR_OPCODE_BOOL_OR_V1:
  case NERI_IR_OPCODE_COMPARE_V1:
  case NERI_IR_OPCODE_CAST_INT_TO_FLOAT_V1:
  case NERI_IR_OPCODE_CAST_FLOAT_TO_INT_CHECKED_V1:
  case NERI_IR_OPCODE_CAST_INT_TO_BYTE_CHECKED_V1:
  case NERI_IR_OPCODE_CAST_BYTE_TO_INT_V1:
  case NERI_IR_OPCODE_NUMERIC_CAST_CHECKED_V1:
  case NERI_IR_OPCODE_CLASS_UPCAST_V1:
  case NERI_IR_OPCODE_OPTIONAL_NONE_V1:
  case NERI_IR_OPCODE_OPTIONAL_SOME_V1:
  case NERI_IR_OPCODE_OPTIONAL_IS_SOME_V1:
  case NERI_IR_OPCODE_OPTIONAL_GET_CHECKED_V1:
  case NERI_IR_OPCODE_STRING_CONCAT_V1:
  case NERI_IR_OPCODE_STRING_EQUAL_V1:
  case NERI_IR_OPCODE_STRING_NOT_EQUAL_V1:
  case NERI_IR_OPCODE_STRING_FROM_INT_V1:
  case NERI_IR_OPCODE_STRING_FROM_BYTE_V1:
  case NERI_IR_OPCODE_STRING_FROM_FLOAT_V1:
  case NERI_IR_OPCODE_ARRAY_NEW_V1:
  case NERI_IR_OPCODE_ARRAY_LENGTH_V1:
  case NERI_IR_OPCODE_ARRAY_LOAD_CHECKED_V1:
  case NERI_IR_OPCODE_ARRAY_STORE_CHECKED_V1:
  case NERI_IR_OPCODE_OBJECT_ALLOC_V1:
  case NERI_IR_OPCODE_FIELD_LOAD_V1:
  case NERI_IR_OPCODE_FIELD_STORE_V1:
    return true;
  default:
    return false;
  }
}

bool has_loop(const function &function) {
  std::map<std::uint32_t, const block *> blocks;
  std::map<std::uint32_t, std::size_t> incoming;
  for (const auto &block : function.blocks) {
    blocks.emplace(block.id, &block);
    incoming.try_emplace(block.id, 0);
    for (const auto &edge : block.ending.edges) ++incoming[edge.target];
  }
  std::vector<std::uint32_t> ready;
  for (const auto &[id, count] : incoming)
    if (count == 0) ready.push_back(id);
  for (std::size_t index = 0; index < ready.size(); ++index)
    for (const auto &edge : blocks.at(ready[index])->ending.edges)
      if (--incoming.at(edge.target) == 0) ready.push_back(edge.target);
  return ready.size() != blocks.size();
}
} // namespace

std::set<const function *> allocation_regions(const ir_module &module) {
  std::set<const function *> compatible;
  std::set<const function *> bulk;
  std::map<decltype(key(symbol_id{})), const function *> definitions;
  for (const auto &function : module.functions)
    definitions.emplace(key(function.id), &function);

  // Start with no known callees and grow to a fixed point. Recursive cycles
  // stay unknown. Import effects alone are not a no-escape proof.
  bool changed;
  do {
    changed = false;
    for (const auto &function : module.functions) {
      if (compatible.contains(&function) || function.unsafe_call ||
          function.unsafe_root.has_value() || function.blocks.empty()) continue;
      bool local = true;
      bool repeated = has_loop(function);
      for (const auto &block : function.blocks) {
        for (const auto &instruction : block.instructions) {
          if (instruction.opcode == NERI_IR_OPCODE_CALL_V1 ||
              instruction.opcode == NERI_IR_OPCODE_CALL_DIRECT_V1) {
            const auto callee = definitions.find(key(*instruction.symbol));
            if (callee == definitions.end() || !compatible.contains(callee->second)) local = false;
            if (callee != definitions.end() && bulk.contains(callee->second)) repeated = true;
          } else if (!local_operation(instruction.opcode)) {
            local = false;
          }
        }
      }
      if (local) {
        compatible.insert(&function);
        if (repeated && (function.effects & NERI_IR_EFFECT_MANAGED_ALLOCATE_V1) != 0)
          bulk.insert(&function);
        changed = true;
      }
    }
  } while (changed);

  std::set<const function *> result;
  for (const auto *function : compatible) {
    // No incoming or outgoing object graph crosses the region boundary. All
    // intermediate managed values, including callees' results, stay inside.
    // Avoid creating an entire heap for a leaf that allocates one small object.
    if (bulk.contains(function) &&
        scalar_boundary(function->result_type) &&
        std::ranges::all_of(function->parameter_types, scalar_boundary)) {
      result.insert(function);
    }
  }
  return result;
}
} // namespace neri::codegen
