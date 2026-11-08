#ifndef NERI_CODEGEN_IR_INTERFACES_H
#define NERI_CODEGEN_IR_INTERFACES_H

#include "neri/codegen/ir.h"

#include <set>
#include <vector>

namespace neri::codegen {

template <class Lookup>
[[nodiscard]] std::vector<const class_declaration *> transitive_interfaces(
    const class_declaration &declaration, Lookup lookup) {
  std::vector<const class_declaration *> result;
  std::vector<const class_declaration *> pending{&declaration};
  std::set<const class_declaration *> visited;
  while (!pending.empty()) {
    const auto *current = pending.back();
    pending.pop_back();
    if (current == nullptr || !visited.insert(current).second) continue;
    if (current != &declaration && current->is_interface) {
      result.push_back(current);
    }
    if (current->base) pending.push_back(lookup(*current->base));
    for (auto edge = current->interfaces.rbegin();
         edge != current->interfaces.rend(); ++edge) {
      pending.push_back(lookup(*edge));
    }
  }
  return result;
}

} // namespace neri::codegen

#endif
