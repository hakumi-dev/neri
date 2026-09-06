#ifndef NERI_CODEGEN_NATIVE_LAYOUT_H
#define NERI_CODEGEN_NATIVE_LAYOUT_H

#include "neri/codegen/ir.h"
#include "neri/codegen/reader.h"
#include "neri/ir_transport.h"
#include "numeric_types.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace neri::codegen {

struct native_layout final {
  std::uint64_t size{};
  std::uint64_t alignment{1U};
  std::vector<std::uint64_t> offsets;
};

class native_layouts final {
public:
  explicit native_layouts(const ir_module &module) : module_(module) {}

  [[nodiscard]] const native_record &declaration(const type &value) const {
    if (value.tag != NERI_IR_TYPE_NATIVE_RECORD_V1 || !value.symbol ||
        value.symbol->kind != NERI_IR_SYMBOL_NATIVE_RECORD_V1 ||
        !value.arguments.empty() || value.element_count != 0U)
      reject("Invalid native record type.");
    for (const auto &record : module_.native_records) {
      if (record.id.module == value.symbol->module &&
          record.id.semantic_name == value.symbol->semantic_name)
        return record;
    }
    reject("Native record type references a missing declaration.");
  }

  [[nodiscard]] native_layout layout(const type &value, unsigned depth = 0U) {
    if (depth >= 128U) reject("Native layout exceeds 128 nested types.");
    if (value.tag == NERI_IR_TYPE_FIXED_ARRAY_V1) {
      if (value.symbol || value.arguments.size() != 1U || value.element_count == 0U)
        reject("Invalid fixed native array.");
      auto element = layout(value.arguments.front(), depth + 1U);
      if (value.element_count > maximum_size / element.size)
        reject("Native array exceeds one GiB.");
      return {element.size * value.element_count, element.alignment, {}};
    }
    if (value.element_count != 0U) reject("Only fixed arrays carry an element count.");
    if (value.tag == NERI_IR_TYPE_NATIVE_RECORD_V1) {
      const auto &record = declaration(value);
      const auto &key = record.id.semantic_name;
      if (const auto found = completed_.find(key); found != completed_.end()) return found->second;
      if (!visiting_.insert(key).second) reject("Native layout is recursive by value.");
      if (record.fields.empty()) reject("Native records require at least one field.");
      native_layout result;
      for (const auto &field : record.fields) {
        const auto member = layout(field.value_type, depth + 1U);
        result.alignment = std::max(result.alignment, member.alignment);
        const auto offset = record.is_union ? 0U : align_up(result.size, member.alignment);
        result.offsets.push_back(offset);
        result.size = std::max(result.size, offset + member.size);
        if (result.size > maximum_size) reject("Native record exceeds one GiB.");
      }
      result.size = align_up(result.size, result.alignment);
      if (result.size > maximum_size) reject("Native record exceeds one GiB.");
      visiting_.erase(key);
      completed_.emplace(key, result);
      return result;
    }
    if (value.tag == NERI_IR_TYPE_POINTER_V1 || value.tag == NERI_IR_TYPE_OPTIONAL_V1) {
      const type *pointer = &value;
      if (value.tag == NERI_IR_TYPE_OPTIONAL_V1) {
        if (value.symbol || value.arguments.size() != 1U ||
            value.arguments.front().tag != NERI_IR_TYPE_POINTER_V1)
          reject("Only pointer optionals have a C-compatible layout.");
        pointer = &value.arguments.front();
      }
      validate_pointer(*pointer, depth + 1U);
      return {8U, 8U, {}};
    }
    if (value.symbol || !value.arguments.empty()) reject("Invalid native scalar shape.");
    if (value.tag == NERI_IR_TYPE_BOOL_V1 || value.tag == NERI_IR_TYPE_BYTE_V1)
      return {1U, 1U, {}};
    if (value.tag == NERI_IR_TYPE_INT32_V1 || value.tag == NERI_IR_TYPE_UINT32_V1 ||
        value.tag == NERI_IR_TYPE_FLOAT32_V1) return {4U, 4U, {}};
    if (value.tag == NERI_IR_TYPE_INT_V1 || value.tag == NERI_IR_TYPE_UINT64_V1 ||
        value.tag == NERI_IR_TYPE_FLOAT_V1) return {8U, 8U, {}};
    reject("Type has no C-compatible inline layout.");
  }

private:
  [[noreturn]] static void reject(const std::string &message) {
    throw reader_error("NIR006", message, 0U);
  }

  void validate_pointer(const type &value, unsigned depth) {
    if (depth >= 128U || value.symbol || value.arguments.size() != 1U || value.element_count != 0U)
      reject("Invalid native pointer shape.");
    const auto &element = value.arguments.front();
    if (element.tag == NERI_IR_TYPE_VOID_V1) {
      if (element.symbol || !element.arguments.empty() || element.element_count != 0U)
        reject("Invalid void pointer element.");
    } else if (element.tag == NERI_IR_TYPE_NATIVE_RECORD_V1) {
      (void)declaration(element);
    } else {
      (void)layout(element, depth + 1U);
    }
  }

  static std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
  }

  static constexpr std::uint64_t maximum_size = UINT64_C(1073741824);
  const ir_module &module_;
  std::map<std::string, native_layout> completed_;
  std::set<std::string> visiting_;
};

} // namespace neri::codegen
#endif
