#ifndef NERI_CODEGEN_LLVM_NUMERIC_H
#define NERI_CODEGEN_LLVM_NUMERIC_H
#include "numeric_types.h"
#include <llvm/IR/IRBuilder.h>
#include <cmath>
#include <limits>

namespace neri::codegen {
/* Emit guards before potentially poison-producing LLVM conversions. */
template <typename Guard>
llvm::Value *numeric_cast(llvm::IRBuilder<> &builder, llvm::Value *source,
                         unsigned source_tag, unsigned target_tag,
                         llvm::Type *target, Guard guard) {
  const bool from_float = floating_scalar(source_tag);
  const bool to_float = floating_scalar(target_tag);
  if (!from_float && !to_float) {
    auto *wide = builder.CreateIntCast(source, builder.getIntNTy(128), !unsigned_scalar(source_tag));
    const auto width = scalar_width(target_tag);
    const auto minimum = unsigned_scalar(target_tag) ? llvm::APInt(128, 0) : llvm::APInt::getSignedMinValue(width).sext(128);
    const auto maximum = (unsigned_scalar(target_tag) ? llvm::APInt::getMaxValue(width) : llvm::APInt::getSignedMaxValue(width)).zext(128);
    guard(builder.CreateAnd(
        builder.CreateICmpSGE(wide, llvm::ConstantInt::get(builder.getContext(), minimum)),
        builder.CreateICmpSLE(wide, llvm::ConstantInt::get(builder.getContext(), maximum))));
    return builder.CreateIntCast(source, target, !unsigned_scalar(source_tag));
  }
  if (from_float && !to_float) {
    const bool is_unsigned = unsigned_scalar(target_tag);
    const auto limit = std::ldexp(1.0, scalar_width(target_tag) - (is_unsigned ? 0 : 1));
    guard(builder.CreateAnd(
        builder.CreateFCmpOGE(source, llvm::ConstantFP::get(source->getType(), is_unsigned ? 0.0 : -limit)),
        builder.CreateFCmpOLT(source, llvm::ConstantFP::get(source->getType(), limit))));
    return is_unsigned ? builder.CreateFPToUI(source, target) : builder.CreateFPToSI(source, target);
  }
  if (!from_float && to_float) {
    return unsigned_scalar(source_tag) ? builder.CreateUIToFP(source, target) : builder.CreateSIToFP(source, target);
  }
  if (scalar_width(source_tag) > scalar_width(target_tag)) {
    const double limit = std::numeric_limits<float>::max();
    auto *in_range = builder.CreateAnd(
        builder.CreateFCmpOGE(source, llvm::ConstantFP::get(source->getType(), -limit)),
        builder.CreateFCmpOLE(source, llvm::ConstantFP::get(source->getType(), limit)));
    auto *special = builder.CreateOr(builder.CreateFCmpUNO(source, source),
        builder.CreateOr(
            builder.CreateFCmpOEQ(source, llvm::ConstantFP::get(source->getType(), std::numeric_limits<double>::infinity())),
            builder.CreateFCmpOEQ(source, llvm::ConstantFP::get(source->getType(), -std::numeric_limits<double>::infinity()))));
    guard(builder.CreateOr(in_range, special));
    return builder.CreateFPTrunc(source, target);
  }
  return builder.CreateFPCast(source, target);
}
}
#endif
