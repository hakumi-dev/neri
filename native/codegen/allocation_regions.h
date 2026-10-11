#pragma once

#include "neri/codegen/ir.h"

#include <set>

namespace neri::codegen {

// Select scalar boundaries whose entire allocation/call graph is local.
// Unknown calls, recursive call cycles and unsafe operations retain normal GC.
[[nodiscard]] std::set<const function *> allocation_regions(const ir_module &module);

} // namespace neri::codegen
