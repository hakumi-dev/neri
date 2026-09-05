#ifndef NERI_CODEGEN_IR_VERIFIER_H
#define NERI_CODEGEN_IR_VERIFIER_H

#include "neri/codegen/ir.h"

namespace neri::codegen {

void verify_supported_module(const ir_module &value);

} // namespace neri::codegen

#endif
