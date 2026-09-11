#ifndef NERI_CODEGEN_LLVM_LOWERING_H
#define NERI_CODEGEN_LLVM_LOWERING_H

#include "neri/codegen/reader.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class DataLayout;
class LLVMContext;
class Module;
class Triple;
} // namespace llvm

namespace neri::codegen {

[[nodiscard]] std::unique_ptr<llvm::Module>
lower_to_llvm(const verified_module &input, llvm::LLVMContext &context,
              const llvm::Triple &triple, const llvm::DataLayout &layout,
              bool emit_debug_information,
              const std::vector<std::pair<std::string, std::string>>
                  &debug_sources);

} // namespace neri::codegen

#endif
