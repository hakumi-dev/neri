#include "llvm_lowering.h"

#include "neri/codegen/emitter.h"
#include "neri/ir_transport.h"
#include "neri/runtime_abi.h"

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace neri::codegen {
namespace {

constexpr std::string_view lowering_error = "NCG002";
constexpr std::uint16_t source_location_runtime_minor = 1U;
constexpr std::uint16_t native_string_runtime_minor = 2U;
constexpr std::uint16_t native_array_runtime_minor = 4U;
constexpr std::uint16_t native_class_runtime_minor = 4U;

[[nodiscard]] auto symbol_key(const symbol_id &value) {
  return std::tuple(value.module, value.kind, value.semantic_name);
}

[[nodiscard]] bool is_void(const type &value) {
  return value.tag == NERI_IR_TYPE_VOID_V1;
}

[[nodiscard]] bool is_optional(const type &value) {
  return value.tag == NERI_IR_TYPE_OPTIONAL_V1;
}

[[nodiscard]] bool is_string(const type &value) {
  return value.tag == NERI_IR_TYPE_STRING_V1;
}

[[nodiscard]] bool is_array(const type &value) {
  return value.tag == NERI_IR_TYPE_ARRAY_V1;
}

[[nodiscard]] bool is_class(const type &value) {
  return value.tag == NERI_IR_TYPE_CLASS_V1;
}

[[nodiscard]] bool is_managed_reference(const type &value) {
  return is_string(value) || is_array(value) || is_class(value) ||
         (is_optional(value) && is_managed_reference(value.arguments.front()));
}

[[nodiscard]] bool is_nullable_pointer(const type &value) {
  return is_optional(value) && value.arguments.size() == 1U &&
         value.arguments.front().tag == NERI_IR_TYPE_POINTER_V1;
}

[[nodiscard]] bool uses_null_representation(const type &value) {
  return is_managed_reference(value) || is_nullable_pointer(value);
}

[[nodiscard]] bool is_indirect_optional(const type &value) {
  return is_optional(value) && !uses_null_representation(value);
}

[[nodiscard]] bool contains_array(const type &value) {
  return is_array(value) || std::ranges::any_of(value.arguments, contains_array);
}

void append_hex(std::string &output, std::string_view value) {
  constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    output.push_back(digits[byte >> 4U]);
    output.push_back(digits[byte & 0x0fU]);
  }
}

void append_qualified_parts(std::vector<std::string_view> &parts,
                            std::string_view value) {
  std::size_t start = 0U;
  while (true) {
    const auto separator = value.find('.', start);
    parts.push_back(value.substr(start, separator - start));
    if (separator == std::string_view::npos) {
      return;
    }
    start = separator + 1U;
  }
}

[[nodiscard]] std::string qualified_name(const symbol_id &value) {
  std::vector<std::string_view> parts;
  append_qualified_parts(parts, value.semantic_name);

  auto result = std::string("q") + std::to_string(parts.size());
  for (const auto part : parts) {
    result += "_n" + std::to_string(part.size()) + "_";
    append_hex(result, part);
  }
  return result;
}

[[nodiscard]] std::string type_code(const type &value) {
  switch (value.tag) {
  case NERI_IR_TYPE_VOID_V1:
    return "v";
  case NERI_IR_TYPE_BOOL_V1:
    return "b";
  case NERI_IR_TYPE_BYTE_V1:
    return "u8";
  case NERI_IR_TYPE_INT_V1:
    return "i64";
  case NERI_IR_TYPE_FLOAT_V1:
    return "f64";
  case NERI_IR_TYPE_STRING_V1:
    return "s";
  case NERI_IR_TYPE_ARRAY_V1:
    return "a" + type_code(value.arguments.front()) + "e";
  case NERI_IR_TYPE_CLASS_V1:
    return "c" + qualified_name(*value.symbol) + "e";
  case NERI_IR_TYPE_OPTIONAL_V1:
    return "o" + type_code(value.arguments.front()) + "e";
  case NERI_IR_TYPE_POINTER_V1:
    return "p" + type_code(value.arguments.front()) + "e";
  case NERI_IR_TYPE_UNSAFE_CAPABILITY_V1:
    return "cu";
  case NERI_IR_TYPE_BORROW_CAPABILITY_V1:
    return "cb" + type_code(value.arguments.front()) + "e";
  default:
    throw codegen_error(std::string(lowering_error),
                        "Cannot mangle an unsupported native type.");
  }
}

[[nodiscard]] std::string mangle_function(const function &value) {
  auto result = std::string("hk1_f_") + qualified_name(value.id) + "__p" +
                std::to_string(value.parameter_types.size()) + "_";
  for (const auto &parameter : value.parameter_types) {
    result += type_code(parameter) + "_";
  }
  result += "r" + type_code(value.result_type);
  return result;
}

[[nodiscard]] std::string mangle_global(const global_declaration &value) {
  return "hk1_g_" + qualified_name(value.id) + "__" +
         type_code(value.value_type);
}

class module_lowerer final {
public:
  module_lowerer(const ir_module &input, llvm::LLVMContext &context,
                 const llvm::Triple &triple, const llvm::DataLayout &layout,
                 bool emit_debug_information)
      : input_(input), context_(context),
        output_(std::make_unique<llvm::Module>(input.id, context)),
        emit_debug_information_(emit_debug_information) {
    output_->setTargetTriple(triple);
    output_->setDataLayout(layout);
  }

  [[nodiscard]] std::unique_ptr<llvm::Module> lower() {
    initialize_debug_information();
    build_class_layouts();
    emit_program_requirements();
    declare_globals();
    declare_imports();
    declare_functions();
    declare_class_metadata();
    lower_functions();
    if (debug_builder_ != nullptr) {
      debug_builder_->finalize();
    }
    return std::move(output_);
  }

private:
  void initialize_debug_information() {
    if (!emit_debug_information_ || input_.sources.empty()) {
      return;
    }
    debug_builder_ = std::make_unique<llvm::DIBuilder>(*output_);
    auto *file = debug_file(input_.sources.front().id);
    static_cast<void>(debug_builder_->createCompileUnit(
        llvm::dwarf::DW_LANG_C, file, "Neri", false, "", 0U));
    output_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                           llvm::DEBUG_METADATA_VERSION);
    output_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5U);
  }

  [[nodiscard]] const source &find_source(std::string_view id) const {
    const auto found = std::ranges::find_if(
        input_.sources,
        [id](const auto &candidate) { return candidate.id == id; });
    if (found == input_.sources.end()) {
      throw codegen_error(std::string(lowering_error),
                          "Verified debug source disappeared.");
    }
    return *found;
  }

  [[nodiscard]] static std::string debug_filename(std::string_view id) {
    const auto separator = id.find_last_of("/\\");
    auto name = std::string(id.substr(
        separator == std::string_view::npos ? 0U : separator + 1U));
    return name.empty() ? "main.hk" : name;
  }

  [[nodiscard]] llvm::DIFile *debug_file(std::string_view source_id) {
    if (const auto found = debug_files_.find(std::string(source_id));
        found != debug_files_.end()) {
      return found->second;
    }
    const auto &source = find_source(source_id);
    const auto text = std::string(
        reinterpret_cast<const char *>(source.utf8.data()), source.utf8.size());
    auto *file = debug_builder_->createFile(debug_filename(source_id), ".",
                                            std::nullopt, text);
    debug_files_.emplace(source.id, file);
    return file;
  }

  [[nodiscard]] std::pair<unsigned, unsigned>
  source_coordinates(const source_location &location) const {
    const auto &source = find_source(location.source);
    unsigned line = 1U;
    unsigned column = 1U;
    const auto limit = std::min<std::size_t>(location.utf8_start,
                                             source.utf8.size());
    for (std::size_t index = 0; index < limit; ++index) {
      if (source.utf8[index] == static_cast<std::uint8_t>('\n')) {
        ++line;
        column = 1U;
      } else {
        ++column;
      }
    }
    return {line, column};
  }

  [[nodiscard]] llvm::DebugLoc
  debug_location(const std::optional<source_location> &location,
                 llvm::DIScope *scope) const {
    if (debug_builder_ == nullptr || !location.has_value() || scope == nullptr) {
      return {};
    }
    const auto [line, column] = source_coordinates(*location);
    return llvm::DILocation::get(context_, line, column, scope);
  }

  [[nodiscard]] llvm::DIType *debug_type(const type &value) {
    if (is_void(value)) {
      return nullptr;
    }
    const auto key = type_code(value);
    if (const auto found = debug_types_.find(key); found != debug_types_.end()) {
      return found->second;
    }
    llvm::DIType *result = nullptr;
    switch (value.tag) {
    case NERI_IR_TYPE_BOOL_V1:
      result = debug_builder_->createBasicType("Bool", 8U,
                                                llvm::dwarf::DW_ATE_boolean);
      break;
    case NERI_IR_TYPE_BYTE_V1:
      result = debug_builder_->createBasicType("Byte", 8U,
                                                llvm::dwarf::DW_ATE_unsigned);
      break;
    case NERI_IR_TYPE_INT_V1:
      result = debug_builder_->createBasicType("Int", 64U,
                                                llvm::dwarf::DW_ATE_signed);
      break;
    case NERI_IR_TYPE_FLOAT_V1:
      result = debug_builder_->createBasicType("Float", 64U,
                                                llvm::dwarf::DW_ATE_float);
      break;
    case NERI_IR_TYPE_POINTER_V1:
      result = debug_builder_->createPointerType(
          debug_type(value.arguments.front()), 64U, 64U, std::nullopt,
          "Pointer");
      break;
    case NERI_IR_TYPE_STRING_V1:
    case NERI_IR_TYPE_ARRAY_V1:
    case NERI_IR_TYPE_CLASS_V1:
      result = debug_builder_->createPointerType(
          debug_builder_->createUnspecifiedType(key), 64U, 64U);
      break;
    default:
      result = debug_builder_->createUnspecifiedType(key);
      break;
    }
    debug_types_.emplace(key, result);
    return result;
  }

  [[nodiscard]] llvm::DISubprogram *
  debug_subprogram(const function &value, llvm::Function &output) {
    if (debug_builder_ == nullptr || !value.location.has_value()) {
      return nullptr;
    }
    auto *file = debug_file(value.location->source);
    const auto [line, ignored_column] = source_coordinates(*value.location);
    static_cast<void>(ignored_column);
    std::vector<llvm::Metadata *> types;
    types.push_back(debug_type(value.result_type));
    for (const auto &parameter : value.parameter_types) {
      types.push_back(debug_type(parameter));
    }
    auto *signature = debug_builder_->createSubroutineType(
        debug_builder_->getOrCreateTypeArray(types));
    auto *subprogram = debug_builder_->createFunction(
        file, value.id.semantic_name, output.getName(), file, line, signature,
        line, llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition);
    output.setSubprogram(subprogram);
    return subprogram;
  }

  class function_lowerer final {
  public:
    function_lowerer(module_lowerer &module, const function &input,
                     llvm::Function &output)
        : module_(module), input_(input), output_(output),
          builder_(module.context_) {}

    void lower() {
      create_blocks_and_parameters();
      for (const auto &block : input_.blocks) {
        builder_.SetInsertPoint(blocks_.at(block.id));
        for (const auto &instruction : block.instructions) {
          builder_.SetCurrentDebugLocation(
              module_.debug_location(instruction.location, subprogram_));
          lower_instruction(instruction);
        }
        builder_.SetCurrentDebugLocation(
            module_.debug_location(block.ending.location, subprogram_));
        lower_terminator(block.ending);
      }
    }

  private:
    [[nodiscard]] llvm::Value *value(std::uint32_t id) {
      if (const auto rooted = root_slots_.find(id); rooted != root_slots_.end()) {
        return builder_.CreateLoad(module_.semantic_type(
                                       module_.value_type(input_, id)),
                                   rooted->second,
                                   "v" + std::to_string(id) + ".rooted");
      }
      return values_.at(id);
    }

    [[nodiscard]] llvm::AllocaInst *create_entry_alloca(llvm::Type *value_type,
                                                        std::string_view name) {
      auto &entry = output_.getEntryBlock();
      llvm::IRBuilder<> entry_builder(&entry, entry.begin());
      return entry_builder.CreateAlloca(value_type, nullptr,
                                        llvm::StringRef(name));
    }

    [[nodiscard]] llvm::Value *to_physical_scalar(llvm::Value *input,
                                                   const type &semantic) {
      if (semantic.tag == NERI_IR_TYPE_BOOL_V1) {
        return builder_.CreateZExt(input, builder_.getInt8Ty(), "bool.abi");
      }
      return input;
    }

    [[nodiscard]] llvm::Value *from_physical_scalar(llvm::Value *input,
                                                     const type &semantic) {
      if (semantic.tag == NERI_IR_TYPE_BOOL_V1) {
        return builder_.CreateICmpNE(input, builder_.getInt8(0), "bool.ssa");
      }
      return input;
    }

    void create_root_frame_storage() {
      std::vector<std::uint32_t> root_ids;
      for (const auto &block : input_.blocks) {
        for (const auto &parameter : block.parameters) {
          if (is_managed_reference(parameter.value_type)) {
            root_ids.push_back(parameter.id);
          }
        }
        for (const auto &instruction : block.instructions) {
          for (const auto &result : instruction.results) {
            if (is_managed_reference(result.value_type)) {
              root_ids.push_back(result.id);
            }
          }
        }
      }
      if (root_ids.empty()) {
        return;
      }

      auto *pointer_type = llvm::PointerType::getUnqual(module_.context_);
      auto *array_type = llvm::ArrayType::get(pointer_type, root_ids.size());
      root_array_ = create_entry_alloca(array_type, "gc.roots");
      builder_.CreateStore(llvm::ConstantAggregateZero::get(array_type),
                           root_array_);
      for (std::size_t index = 0; index < root_ids.size(); ++index) {
        auto *slot = builder_.CreateInBoundsGEP(
            array_type, root_array_,
            {builder_.getInt32(0), builder_.getInt32(index)},
            "gc.root." + std::to_string(root_ids[index]));
        root_slots_.emplace(root_ids[index], slot);
      }

      root_frame_type_ = llvm::StructType::get(
          module_.context_,
          {pointer_type, pointer_type, builder_.getInt64Ty(),
           builder_.getInt64Ty()});
      root_frame_ = create_entry_alloca(root_frame_type_, "gc.frame");
      builder_.CreateStore(
          llvm::ConstantAggregateZero::get(root_frame_type_), root_frame_);
      auto *slots = builder_.CreateStructGEP(root_frame_type_, root_frame_, 1U);
      auto *first = builder_.CreateInBoundsGEP(
          array_type, root_array_,
          {builder_.getInt32(0), builder_.getInt32(0)});
      builder_.CreateStore(first, slots);
      auto *count = builder_.CreateStructGEP(root_frame_type_, root_frame_, 2U);
      builder_.CreateStore(builder_.getInt64(root_ids.size()), count);
    }

    void root_value(std::uint32_t id, llvm::Value *value) {
      if (const auto slot = root_slots_.find(id); slot != root_slots_.end()) {
        builder_.CreateStore(value, slot->second);
      }
    }

    void enter_root_frame() {
      if (root_frame_ != nullptr) {
        auto *call = builder_.CreateCall(module_.root_frame_enter_function(),
                                         {root_frame_});
        call->setCallingConv(llvm::CallingConv::C);
      }
    }

    void leave_root_frame() {
      if (root_frame_ != nullptr) {
        auto *call = builder_.CreateCall(module_.root_frame_leave_function(),
                                         {root_frame_});
        call->setCallingConv(llvm::CallingConv::C);
      }
    }

    void create_blocks_and_parameters() {
      subprogram_ = module_.debug_subprogram(input_, output_);
      for (const auto &block : input_.blocks) {
        blocks_.emplace(block.id,
                        llvm::BasicBlock::Create(module_.context_,
                                                 "b" + std::to_string(block.id),
                                                 &output_));
      }

      auto argument = output_.arg_begin();
      if (is_indirect_optional(input_.result_type)) {
        result_slot_ = &*argument++;
        result_slot_->setName("result.slot");
      }

      builder_.SetInsertPoint(blocks_.at(input_.entry_block));
      create_root_frame_storage();
      const auto &entry = input_.blocks.front();
      for (std::size_t index = 0; index < entry.parameters.size(); ++index) {
        auto *physical = &*argument++;
        physical->setName("p" + std::to_string(index));
        const auto &semantic = entry.parameters[index].value_type;
        llvm::Value *lowered = physical;
        if (is_indirect_optional(semantic)) {
          lowered = builder_.CreateLoad(module_.semantic_type(semantic),
                                        physical, "p" + std::to_string(index) +
                                                      ".value");
        } else {
          lowered = from_physical_scalar(physical, semantic);
        }
        values_.emplace(entry.parameters[index].id, lowered);
        root_value(entry.parameters[index].id, lowered);
        emit_debug_local(entry.parameters[index].id, lowered);
      }
      enter_root_frame();

      if (input_.unsafe_root.has_value()) {
        auto *token = llvm::ConstantExpr::getIntToPtr(
            builder_.getInt64(1), llvm::PointerType::getUnqual(module_.context_));
        values_.emplace(input_.unsafe_root->id, token);
      }

      for (std::size_t block_index = 1U;
           block_index < input_.blocks.size(); ++block_index) {
        const auto &block = input_.blocks[block_index];
        builder_.SetInsertPoint(blocks_.at(block.id));
        std::vector<std::pair<std::uint32_t, llvm::PHINode *>> block_phis;
        for (const auto &parameter : block.parameters) {
          auto *phi = builder_.CreatePHI(module_.semantic_type(parameter.value_type),
                                         0U,
                                         "v" + std::to_string(parameter.id));
          values_.emplace(parameter.id, phi);
          phis_.emplace(parameter.id, phi);
          block_phis.emplace_back(parameter.id, phi);
        }
        for (const auto &[id, phi] : block_phis) {
          root_value(id, phi);
        }
      }
    }

    void emit_debug_local(std::uint32_t id, llvm::Value *value) {
      if (module_.debug_builder_ == nullptr || subprogram_ == nullptr ||
          value == nullptr) {
        return;
      }
      for (const auto &local : input_.debug_locals) {
        if (local.value != id) {
          continue;
        }
        const auto coordinates = module_.source_coordinates(local.location);
        auto *variable = module_.debug_builder_->createAutoVariable(
            subprogram_, local.name, module_.debug_file(local.location.source),
            coordinates.first,
            module_.debug_type(module_.value_type(input_, local.value)), true);
        module_.debug_builder_->insertDbgValueIntrinsic(
            value, variable, module_.debug_builder_->createExpression(),
            llvm::DILocation::get(module_.context_, coordinates.first,
                                  coordinates.second, subprogram_),
            builder_.GetInsertBlock());
      }
    }

    void emit_guard(llvm::Value *condition, std::uint32_t code,
                    std::string_view message,
                    const std::optional<source_location> &location) {
      auto *success = llvm::BasicBlock::Create(module_.context_, "check.ok",
                                               &output_);
      auto *failure = llvm::BasicBlock::Create(module_.context_, "check.fail",
                                               &output_);
      builder_.CreateCondBr(condition, success, failure);
      builder_.SetInsertPoint(failure);
      module_.emit_panic(builder_, code, message, location);
      builder_.SetInsertPoint(success);
    }

    [[nodiscard]] llvm::Value *checked_overflow(
        llvm::Intrinsic::ID intrinsic, std::span<llvm::Value *const> operands,
        std::string_view message,
        const std::optional<source_location> &location) {
      auto *declaration = llvm::Intrinsic::getOrInsertDeclaration(
          module_.output_.get(), intrinsic, {builder_.getInt64Ty()});
      auto *pair = builder_.CreateCall(declaration,
                                       llvm::ArrayRef(operands.data(),
                                                      operands.size()),
                                       "checked");
      auto *result = builder_.CreateExtractValue(pair, 0U, "checked.value");
      auto *overflow =
          builder_.CreateExtractValue(pair, 1U, "checked.overflow");
      emit_guard(builder_.CreateNot(overflow), NERI_PANIC_ARITHMETIC_V1,
                 message, location);
      return result;
    }

    [[nodiscard]] llvm::Value *compare_scalar(llvm::Value *left,
                                              llvm::Value *right,
                                              const type &operand_type,
                                              std::uint8_t predicate) {
      if (operand_type.tag == NERI_IR_TYPE_FLOAT_V1) {
        if (predicate == NERI_IR_COMPARISON_EQUAL_V1 ||
            predicate == NERI_IR_COMPARISON_NOT_EQUAL_V1) {
          auto *ordered_equal = builder_.CreateFCmpOEQ(left, right, "float.eq");
          auto *left_nan = builder_.CreateFCmpUNO(left, left, "float.left.nan");
          auto *right_nan =
              builder_.CreateFCmpUNO(right, right, "float.right.nan");
          auto *equal = builder_.CreateOr(
              ordered_equal, builder_.CreateAnd(left_nan, right_nan),
              "float.equals");
          return predicate == NERI_IR_COMPARISON_EQUAL_V1
                     ? equal
                     : builder_.CreateNot(equal, "float.not.equals");
        }
        const auto llvm_predicate = [&] {
          switch (predicate) {
          case NERI_IR_COMPARISON_LESS_V1:
            return llvm::CmpInst::FCMP_OLT;
          case NERI_IR_COMPARISON_LESS_OR_EQUAL_V1:
            return llvm::CmpInst::FCMP_OLE;
          case NERI_IR_COMPARISON_GREATER_V1:
            return llvm::CmpInst::FCMP_OGT;
          case NERI_IR_COMPARISON_GREATER_OR_EQUAL_V1:
            return llvm::CmpInst::FCMP_OGE;
          default:
            throw codegen_error(std::string(lowering_error),
                                "Invalid floating comparison predicate.");
          }
        }();
        return builder_.CreateFCmp(llvm_predicate, left, right, "float.cmp");
      }

      const auto llvm_predicate = [&] {
        switch (predicate) {
        case NERI_IR_COMPARISON_EQUAL_V1:
          return llvm::CmpInst::ICMP_EQ;
        case NERI_IR_COMPARISON_NOT_EQUAL_V1:
          return llvm::CmpInst::ICMP_NE;
        case NERI_IR_COMPARISON_LESS_V1:
          return operand_type.tag == NERI_IR_TYPE_BYTE_V1
                     ? llvm::CmpInst::ICMP_ULT
                     : llvm::CmpInst::ICMP_SLT;
        case NERI_IR_COMPARISON_LESS_OR_EQUAL_V1:
          return operand_type.tag == NERI_IR_TYPE_BYTE_V1
                     ? llvm::CmpInst::ICMP_ULE
                     : llvm::CmpInst::ICMP_SLE;
        case NERI_IR_COMPARISON_GREATER_V1:
          return operand_type.tag == NERI_IR_TYPE_BYTE_V1
                     ? llvm::CmpInst::ICMP_UGT
                     : llvm::CmpInst::ICMP_SGT;
        case NERI_IR_COMPARISON_GREATER_OR_EQUAL_V1:
          return operand_type.tag == NERI_IR_TYPE_BYTE_V1
                     ? llvm::CmpInst::ICMP_UGE
                     : llvm::CmpInst::ICMP_SGE;
        default:
          throw codegen_error(std::string(lowering_error),
                              "Invalid integer comparison predicate.");
        }
      }();
      return builder_.CreateICmp(llvm_predicate, left, right, "cmp");
    }

    [[nodiscard]] llvm::Value *compare_optional(llvm::Value *left,
                                                llvm::Value *right,
                                                const type &operand_type,
                                                std::uint8_t predicate) {
      if (uses_null_representation(operand_type)) {
        llvm::Value *equal = nullptr;
        if (is_string(operand_type.arguments.front())) {
          auto *call = builder_.CreateCall(module_.string_equal_function(),
                                           {left, right}, "optional.string.eq");
          call->setCallingConv(llvm::CallingConv::C);
          equal = builder_.CreateICmpNE(call, builder_.getInt8(0),
                                        "optional.string.equals");
        } else {
          equal = builder_.CreateICmpEQ(left, right, "optional.ref.equals");
        }
        return predicate == NERI_IR_COMPARISON_EQUAL_V1
                   ? equal
                   : builder_.CreateNot(equal, "optional.string.not.equals");
      }

      auto *left_tag = builder_.CreateExtractValue(left, 0U, "left.tag");
      auto *right_tag = builder_.CreateExtractValue(right, 0U, "right.tag");
      auto *tags_equal = builder_.CreateICmpEQ(left_tag, right_tag, "tags.eq");
      auto *both_none = builder_.CreateICmpEQ(left_tag, builder_.getInt8(0),
                                              "both.none");
      const auto payload_index = operand_type.arguments.front().tag ==
                                         NERI_IR_TYPE_BOOL_V1 ||
                                     operand_type.arguments.front().tag ==
                                         NERI_IR_TYPE_BYTE_V1
                                 ? 1U
                                 : 2U;
      auto *left_value =
          builder_.CreateExtractValue(left, payload_index, "left.value");
      auto *right_value =
          builder_.CreateExtractValue(right, payload_index, "right.value");
      left_value = from_physical_scalar(left_value,
                                        operand_type.arguments.front());
      right_value = from_physical_scalar(right_value,
                                         operand_type.arguments.front());
      auto *payload_equal = compare_scalar(
          left_value, right_value, operand_type.arguments.front(),
          NERI_IR_COMPARISON_EQUAL_V1);
      auto *equal = builder_.CreateAnd(
          tags_equal, builder_.CreateOr(both_none, payload_equal),
          "optional.equals");
      return predicate == NERI_IR_COMPARISON_EQUAL_V1
                 ? equal
                 : builder_.CreateNot(equal, "optional.not.equals");
    }

    [[nodiscard]] llvm::Value *lower_call_target(
        const instruction &instruction, const std::vector<type> &parameters,
        const type &result, llvm::Value *callee,
        llvm::FunctionType *function_type, std::size_t operand_offset = 0U) {
      std::vector<llvm::Value *> arguments;
      llvm::AllocaInst *result_slot = nullptr;
      if (is_indirect_optional(result)) {
        result_slot = create_entry_alloca(module_.semantic_type(result),
                                          "call.result");
        arguments.push_back(result_slot);
      }
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        auto *operand = value(instruction.operands[index + operand_offset]);
        if (is_indirect_optional(parameters[index])) {
          auto *slot = create_entry_alloca(
              module_.semantic_type(parameters[index]), "call.argument");
          builder_.CreateStore(operand, slot);
          arguments.push_back(slot);
        } else {
          arguments.push_back(to_physical_scalar(operand, parameters[index]));
        }
      }

      auto *call = builder_.CreateCall(function_type, callee, arguments,
                                       is_void(result) || is_indirect_optional(result)
                                           ? ""
                                           : "call.value");
      call->setCallingConv(llvm::CallingConv::C);
      if (is_indirect_optional(result)) {
        return builder_.CreateLoad(module_.semantic_type(result), result_slot,
                                   "call.result.value");
      }
      if (is_void(result)) {
        return nullptr;
      }
      return from_physical_scalar(call, result);
    }

    [[nodiscard]] llvm::Value *lower_call(const instruction &instruction,
                                          bool imported,
                                          bool has_capability = false) {
      if (imported) {
        const auto &target = module_.find_import(*instruction.symbol);
        auto *callee = module_.imports_.at(symbol_key(*instruction.symbol));
        return lower_call_target(instruction, target.parameter_types,
                                 target.result_type, callee,
                                 callee->getFunctionType(),
                                 has_capability ? 1U : 0U);
      }
      const auto &target = module_.find_function(*instruction.symbol);
      auto *callee = module_.functions_.at(symbol_key(*instruction.symbol));
      return lower_call_target(instruction, target.parameter_types,
                               target.result_type, callee,
                               callee->getFunctionType());
    }

    [[nodiscard]] llvm::Value *lower_virtual_call(
        const instruction &instruction) {
      const auto &target = module_.find_virtual_signature(*instruction.symbol);
      auto *receiver = value(instruction.operands.front());
      auto *type_slot = builder_.CreateInBoundsGEP(
          builder_.getInt8Ty(), receiver, builder_.getInt64(0),
          "object.type.slot");
      auto *descriptor = builder_.CreateAlignedLoad(
          llvm::PointerType::getUnqual(module_.context_), type_slot,
          llvm::Align(alignof(void *)), "object.type");
      auto *table_slot = builder_.CreateStructGEP(
          module_.type_descriptor_type(), descriptor, 10U,
          "object.method.table.slot");
      auto *table = builder_.CreateAlignedLoad(
          llvm::PointerType::getUnqual(module_.context_), table_slot,
          llvm::Align(alignof(void *)), "object.method.table");
      auto *method_slot = builder_.CreateInBoundsGEP(
          llvm::PointerType::getUnqual(module_.context_), table,
          builder_.getInt64(module_.dispatch_slot_index(*instruction.symbol)),
          "object.method.slot");
      auto *callee = builder_.CreateAlignedLoad(
          llvm::PointerType::getUnqual(module_.context_), method_slot,
          llvm::Align(alignof(void *)), "object.method");
      auto *function_type = module_.physical_function_type(
          target.parameter_types, target.result_type);
      return lower_call_target(instruction, target.parameter_types,
                               target.result_type, callee, function_type);
    }

    [[nodiscard]] llvm::Value *array_length(llvm::Value *array) {
      auto *slot = builder_.CreateInBoundsGEP(
          builder_.getInt8Ty(), array,
          builder_.getInt64(sizeof(neri_object_header_v1)), "array.length.slot");
      return builder_.CreateAlignedLoad(builder_.getInt64Ty(), slot,
                                        llvm::Align(alignof(std::uint64_t)),
                                        "array.length");
    }

    void guard_array_index(llvm::Value *length, llvm::Value *index,
                           const std::optional<source_location> &location) {
      auto *nonnegative = builder_.CreateICmpSGE(
          index, llvm::ConstantInt::get(builder_.getInt64Ty(), 0U),
          "array.index.nonnegative");
      auto *inside = builder_.CreateICmpULT(index, length, "array.index.inside");
      emit_guard(builder_.CreateAnd(nonnegative, inside),
                 NERI_PANIC_BOUNDS_V1, "Array index is out of bounds.",
                 location);
    }

    [[nodiscard]] llvm::Value *array_element_slot(llvm::Value *array,
                                                   llvm::Value *index,
                                                   const type &element) {
      auto *offset = builder_.CreateAdd(
          builder_.getInt64(module_.array_elements_offset(element)),
          builder_.CreateMul(
              index, builder_.getInt64(module_.storage_size(element)),
              "array.element.byte.index"),
          "array.element.byte.offset");
      return builder_.CreateInBoundsGEP(builder_.getInt8Ty(), array, offset,
                                        "array.element.slot");
    }

    void store_array_element(llvm::Value *array, llvm::Value *slot,
                             llvm::Value *stored, const type &element) {
      if (is_managed_reference(element)) {
        auto *call = builder_.CreateCall(module_.gc_store_ref_function(),
                                         {array, slot, stored});
        call->setCallingConv(llvm::CallingConv::C);
        return;
      }
      builder_.CreateAlignedStore(to_physical_scalar(stored, element), slot,
                                  llvm::Align(module_.storage_alignment(element)));
    }

    [[nodiscard]] llvm::Value *load_array_element(llvm::Value *slot,
                                                   const type &element) {
      auto *loaded = builder_.CreateAlignedLoad(
          module_.physical_scalar_type(element), slot,
          llvm::Align(module_.storage_alignment(element)), "array.element");
      return from_physical_scalar(loaded, element);
    }

    [[nodiscard]] llvm::Value *field_slot(llvm::Value *object,
                                           const symbol_id &field) {
      return builder_.CreateInBoundsGEP(
          builder_.getInt8Ty(), object,
          builder_.getInt64(module_.field_offset(field)), "object.field.slot");
    }

    void store_field(llvm::Value *object, llvm::Value *slot,
                     llvm::Value *stored, const type &field_type) {
      if (is_managed_reference(field_type)) {
        auto *call = builder_.CreateCall(module_.gc_store_ref_function(),
                                         {object, slot, stored});
        call->setCallingConv(llvm::CallingConv::C);
        return;
      }
      builder_.CreateAlignedStore(
          to_physical_scalar(stored, field_type), slot,
          llvm::Align(module_.storage_alignment(field_type)));
    }

    [[nodiscard]] llvm::Value *load_field(llvm::Value *slot,
                                           const type &field_type) {
      auto *loaded = builder_.CreateAlignedLoad(
          module_.physical_scalar_type(field_type), slot,
          llvm::Align(module_.storage_alignment(field_type)), "object.field");
      return from_physical_scalar(loaded, field_type);
    }

    [[nodiscard]] llvm::Value *allocation_byte_count(
        llvm::Value *count, const type &element,
        const std::optional<source_location> &location) {
      auto *nonnegative = builder_.CreateICmpSGE(
          count, llvm::ConstantInt::get(builder_.getInt64Ty(), 0U),
          "allocation.count.nonnegative");
      auto *multiply = llvm::Intrinsic::getOrInsertDeclaration(
          module_.output_.get(), llvm::Intrinsic::umul_with_overflow,
          {builder_.getInt64Ty()});
      auto *pair = builder_.CreateCall(
          multiply,
          {count, builder_.getInt64(module_.storage_size(element))},
          "allocation.bytes.checked");
      auto *bytes = builder_.CreateExtractValue(pair, 0U, "allocation.bytes");
      auto *overflow =
          builder_.CreateExtractValue(pair, 1U, "allocation.overflow");
      emit_guard(builder_.CreateAnd(nonnegative,
                                    builder_.CreateNot(overflow)),
                 NERI_PANIC_ARITHMETIC_V1,
                 "Allocation size is outside the supported range.", location);
      return bytes;
    }

    [[nodiscard]] llvm::Value *compare_pointer(
        llvm::Value *left, llvm::Value *right, std::uint8_t predicate) {
      switch (predicate) {
      case NERI_IR_COMPARISON_EQUAL_V1:
        return builder_.CreateICmpEQ(left, right, "pointer.equal");
      case NERI_IR_COMPARISON_NOT_EQUAL_V1:
        return builder_.CreateICmpNE(left, right, "pointer.not.equal");
      case NERI_IR_COMPARISON_LESS_V1:
        return builder_.CreateICmpULT(left, right, "pointer.less");
      case NERI_IR_COMPARISON_LESS_OR_EQUAL_V1:
        return builder_.CreateICmpULE(left, right, "pointer.less.equal");
      case NERI_IR_COMPARISON_GREATER_V1:
        return builder_.CreateICmpUGT(left, right, "pointer.greater");
      case NERI_IR_COMPARISON_GREATER_OR_EQUAL_V1:
        return builder_.CreateICmpUGE(left, right, "pointer.greater.equal");
      default:
        throw codegen_error(std::string(lowering_error),
                            "Verified pointer predicate disappeared.");
      }
    }

    void lower_instruction(const instruction &instruction) {
      llvm::Value *result = nullptr;
      switch (instruction.opcode) {
      case NERI_IR_OPCODE_CONSTANT_V1:
        result = module_.semantic_constant(*instruction.constant_value,
                                           instruction.results.front().value_type);
        break;
      case NERI_IR_OPCODE_INT_NEG_CHECKED_V1: {
        std::array<llvm::Value *, 2> operands{
            llvm::ConstantInt::get(builder_.getInt64Ty(), 0U),
            value(instruction.operands[0])};
        result = checked_overflow(llvm::Intrinsic::ssub_with_overflow, operands,
                                  "Integer negation overflow.",
                                  instruction.location);
        break;
      }
      case NERI_IR_OPCODE_INT_ADD_CHECKED_V1:
      case NERI_IR_OPCODE_INT_SUB_CHECKED_V1:
      case NERI_IR_OPCODE_INT_MUL_CHECKED_V1: {
        std::array operands{value(instruction.operands[0]),
                            value(instruction.operands[1])};
        const auto intrinsic = instruction.opcode ==
                                       NERI_IR_OPCODE_INT_ADD_CHECKED_V1
                                   ? llvm::Intrinsic::sadd_with_overflow
                               : instruction.opcode ==
                                         NERI_IR_OPCODE_INT_SUB_CHECKED_V1
                                   ? llvm::Intrinsic::ssub_with_overflow
                                   : llvm::Intrinsic::smul_with_overflow;
        const auto message = instruction.opcode ==
                                     NERI_IR_OPCODE_INT_ADD_CHECKED_V1
                                 ? "Integer addition overflow."
                             : instruction.opcode ==
                                       NERI_IR_OPCODE_INT_SUB_CHECKED_V1
                                 ? "Integer subtraction overflow."
                                 : "Integer multiplication overflow.";
        result = checked_overflow(intrinsic, operands, message,
                                  instruction.location);
        break;
      }
      case NERI_IR_OPCODE_INT_DIV_CHECKED_V1: {
        auto *left = value(instruction.operands[0]);
        auto *right = value(instruction.operands[1]);
        auto *nonzero = builder_.CreateICmpNE(
            right, llvm::ConstantInt::get(builder_.getInt64Ty(), 0U),
            "division.nonzero");
        auto *minimum = llvm::ConstantInt::get(
            builder_.getInt64Ty(), UINT64_C(0x8000000000000000));
        auto *minus_one = llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1);
        auto *overflow = builder_.CreateAnd(
            builder_.CreateICmpEQ(left, minimum),
            builder_.CreateICmpEQ(right, minus_one), "division.overflow");
        emit_guard(builder_.CreateAnd(nonzero, builder_.CreateNot(overflow)),
                   NERI_PANIC_ARITHMETIC_V1,
                   "Invalid integer division.", instruction.location);
        result = builder_.CreateSDiv(left, right, "division.value");
        break;
      }
      case NERI_IR_OPCODE_FLOAT_NEG_V1:
        result = builder_.CreateFNeg(value(instruction.operands[0]), "float.neg");
        break;
      case NERI_IR_OPCODE_FLOAT_ADD_V1:
        result = builder_.CreateFAdd(value(instruction.operands[0]),
                                     value(instruction.operands[1]), "float.add");
        break;
      case NERI_IR_OPCODE_FLOAT_SUB_V1:
        result = builder_.CreateFSub(value(instruction.operands[0]),
                                     value(instruction.operands[1]), "float.sub");
        break;
      case NERI_IR_OPCODE_FLOAT_MUL_V1:
        result = builder_.CreateFMul(value(instruction.operands[0]),
                                     value(instruction.operands[1]), "float.mul");
        break;
      case NERI_IR_OPCODE_FLOAT_DIV_V1:
        result = builder_.CreateFDiv(value(instruction.operands[0]),
                                     value(instruction.operands[1]), "float.div");
        break;
      case NERI_IR_OPCODE_BOOL_NOT_V1:
        result = builder_.CreateNot(value(instruction.operands[0]), "bool.not");
        break;
      case NERI_IR_OPCODE_BOOL_AND_V1:
        result = builder_.CreateAnd(value(instruction.operands[0]),
                                    value(instruction.operands[1]), "bool.and");
        break;
      case NERI_IR_OPCODE_BOOL_OR_V1:
        result = builder_.CreateOr(value(instruction.operands[0]),
                                   value(instruction.operands[1]), "bool.or");
        break;
      case NERI_IR_OPCODE_COMPARE_V1: {
        const auto &operand_type =
            module_.value_type(input_, instruction.operands[0]);
        result = is_optional(operand_type)
                     ? compare_optional(value(instruction.operands[0]),
                                        value(instruction.operands[1]),
                                        operand_type, *instruction.predicate)
                     : compare_scalar(value(instruction.operands[0]),
                                      value(instruction.operands[1]),
                                      operand_type, *instruction.predicate);
        break;
      }
      case NERI_IR_OPCODE_CAST_INT_TO_FLOAT_V1:
        result = builder_.CreateSIToFP(value(instruction.operands[0]),
                                       builder_.getDoubleTy(), "int.to.float");
        break;
      case NERI_IR_OPCODE_CAST_FLOAT_TO_INT_CHECKED_V1: {
        auto *operand = value(instruction.operands[0]);
        auto *minimum = llvm::ConstantFP::get(builder_.getDoubleTy(),
                                              -9223372036854775808.0);
        auto *limit = llvm::ConstantFP::get(builder_.getDoubleTy(),
                                            9223372036854775808.0);
        auto *valid = builder_.CreateAnd(
            builder_.CreateFCmpOGE(operand, minimum),
            builder_.CreateFCmpOLT(operand, limit), "float.to.int.valid");
        emit_guard(valid, NERI_PANIC_ARITHMETIC_V1,
                   "Float value cannot be represented as Int.",
                   instruction.location);
        result = builder_.CreateFPToSI(operand, builder_.getInt64Ty(),
                                       "float.to.int");
        break;
      }
      case NERI_IR_OPCODE_CAST_INT_TO_BYTE_CHECKED_V1: {
        auto *operand = value(instruction.operands[0]);
        auto *valid = builder_.CreateAnd(
            builder_.CreateICmpSGE(
                operand, llvm::ConstantInt::get(builder_.getInt64Ty(), 0U)),
            builder_.CreateICmpSLE(
                operand, llvm::ConstantInt::get(builder_.getInt64Ty(), 255U)),
            "int.to.byte.valid");
        emit_guard(valid, NERI_PANIC_ARITHMETIC_V1,
                   "Integer value is outside the Byte range 0..255.",
                   instruction.location);
        result = builder_.CreateTrunc(operand, builder_.getInt8Ty(),
                                      "int.to.byte");
        break;
      }
      case NERI_IR_OPCODE_CAST_BYTE_TO_INT_V1:
        result = builder_.CreateZExt(value(instruction.operands[0]),
                                     builder_.getInt64Ty(), "byte.to.int");
        break;
      case NERI_IR_OPCODE_CLASS_UPCAST_V1:
        result = value(instruction.operands[0]);
        break;
      case NERI_IR_OPCODE_OPTIONAL_NONE_V1:
        if (uses_null_representation(instruction.results.front().value_type)) {
          result = llvm::ConstantPointerNull::get(
              llvm::PointerType::getUnqual(module_.context_));
        } else {
          result = llvm::ConstantAggregateZero::get(
              module_.semantic_type(instruction.results.front().value_type));
        }
        break;
      case NERI_IR_OPCODE_OPTIONAL_SOME_V1: {
        const auto &optional_type = instruction.results.front().value_type;
        if (uses_null_representation(optional_type)) {
          result = value(instruction.operands[0]);
          break;
        }
        auto *aggregate = llvm::ConstantAggregateZero::get(
            module_.semantic_type(optional_type));
        result = builder_.CreateInsertValue(aggregate, builder_.getInt8(1), 0U,
                                            "optional.tagged");
        const auto payload_index =
            optional_type.arguments.front().tag == NERI_IR_TYPE_BOOL_V1 ||
                    optional_type.arguments.front().tag == NERI_IR_TYPE_BYTE_V1
                ? 1U
                : 2U;
        result = builder_.CreateInsertValue(
            result,
            to_physical_scalar(value(instruction.operands[0]),
                               optional_type.arguments.front()),
            payload_index, "optional.some");
        break;
      }
      case NERI_IR_OPCODE_OPTIONAL_IS_SOME_V1: {
        const auto &operand_type =
            module_.value_type(input_, instruction.operands[0]);
        if (uses_null_representation(operand_type)) {
          result = builder_.CreateICmpNE(
              value(instruction.operands[0]),
              llvm::ConstantPointerNull::get(
                  llvm::PointerType::getUnqual(module_.context_)),
              "optional.is.some");
        } else {
          result = builder_.CreateICmpEQ(
              builder_.CreateExtractValue(value(instruction.operands[0]), 0U),
              builder_.getInt8(1), "optional.is.some");
        }
        break;
      }
      case NERI_IR_OPCODE_OPTIONAL_GET_CHECKED_V1: {
        auto *optional = value(instruction.operands[0]);
        const auto &result_type = instruction.results.front().value_type;
        if (uses_null_representation(result_type)) {
          emit_guard(
              builder_.CreateICmpNE(
                  optional,
                  llvm::ConstantPointerNull::get(
                      llvm::PointerType::getUnqual(module_.context_))),
              NERI_PANIC_RUNTIME_CONTRACT_V1,
              "Cannot read a missing optional value.", instruction.location);
          result = optional;
          break;
        }
        auto *tag = builder_.CreateExtractValue(optional, 0U, "optional.tag");
        emit_guard(builder_.CreateICmpEQ(tag, builder_.getInt8(1)),
                   NERI_PANIC_RUNTIME_CONTRACT_V1,
                   "Cannot read a missing optional value.",
                   instruction.location);
        const auto payload_index = result_type.tag == NERI_IR_TYPE_BOOL_V1 ||
                                           result_type.tag ==
                                               NERI_IR_TYPE_BYTE_V1
                                       ? 1U
                                       : 2U;
        result = from_physical_scalar(
            builder_.CreateExtractValue(optional, payload_index,
                                        "optional.value"),
            result_type);
        break;
      }
      case NERI_IR_OPCODE_STRING_CONCAT_V1: {
        auto *call = builder_.CreateCall(
            module_.string_concat_function(),
            {value(instruction.operands[0]), value(instruction.operands[1])},
            "string.concat");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_STRING_EQUAL_V1:
      case NERI_IR_OPCODE_STRING_NOT_EQUAL_V1: {
        auto *call = builder_.CreateCall(
            module_.string_equal_function(),
            {value(instruction.operands[0]), value(instruction.operands[1])},
            "string.equal.abi");
        call->setCallingConv(llvm::CallingConv::C);
        auto *equal = builder_.CreateICmpNE(call, builder_.getInt8(0),
                                            "string.equal");
        result = instruction.opcode == NERI_IR_OPCODE_STRING_EQUAL_V1
                     ? equal
                     : builder_.CreateNot(equal, "string.not.equal");
        break;
      }
      case NERI_IR_OPCODE_STRING_FROM_INT_V1: {
        auto *call = builder_.CreateCall(module_.string_from_int_function(),
                                         {value(instruction.operands[0])},
                                         "string.from.int");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_STRING_FROM_BYTE_V1: {
        auto *call = builder_.CreateCall(module_.string_from_byte_function(),
                                         {value(instruction.operands[0])},
                                         "string.from.byte");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_STRING_FROM_FLOAT_V1: {
        auto *call = builder_.CreateCall(module_.string_from_float_function(),
                                         {value(instruction.operands[0])},
                                         "string.from.float");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_CALL_V1:
      case NERI_IR_OPCODE_CALL_DIRECT_V1:
        result = lower_call(instruction, false);
        break;
      case NERI_IR_OPCODE_CALL_VIRTUAL_V1:
        result = lower_virtual_call(instruction);
        break;
      case NERI_IR_OPCODE_CALL_IMPORT_V1:
        result = lower_call(instruction, true);
        break;
      case NERI_IR_OPCODE_CALL_C_ABI_V1:
        result = lower_call(instruction, true, true);
        break;
      case NERI_IR_OPCODE_ARRAY_NEW_V1: {
        const auto &element = instruction.type_arguments.front();
        const auto count = static_cast<std::uint64_t>(instruction.operands.size());
        const auto payload_size = module_.array_payload_size(element, count);
        auto *call = builder_.CreateCall(
            module_.gc_alloc_function(),
            {module_.array_descriptor(element), builder_.getInt64(payload_size),
             builder_.getInt64(module_.array_payload_alignment(element))},
            "array.new");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;

        auto *length_slot = builder_.CreateInBoundsGEP(
            builder_.getInt8Ty(), result,
            builder_.getInt64(sizeof(neri_object_header_v1)),
            "array.length.slot");
        builder_.CreateAlignedStore(builder_.getInt64(count), length_slot,
                                    llvm::Align(alignof(std::uint64_t)));
        for (std::size_t index = 0; index < instruction.operands.size();
             ++index) {
          auto *slot = array_element_slot(result, builder_.getInt64(index),
                                          element);
          store_array_element(result, slot, value(instruction.operands[index]),
                              element);
        }
        break;
      }
      case NERI_IR_OPCODE_ARRAY_LENGTH_V1:
        result = array_length(value(instruction.operands[0]));
        break;
      case NERI_IR_OPCODE_ARRAY_LOAD_CHECKED_V1: {
        auto *array = value(instruction.operands[0]);
        auto *index = value(instruction.operands[1]);
        const auto &element = instruction.results.front().value_type;
        guard_array_index(array_length(array), index, instruction.location);
        result = load_array_element(array_element_slot(array, index, element),
                                    element);
        break;
      }
      case NERI_IR_OPCODE_ARRAY_STORE_CHECKED_V1: {
        auto *array = value(instruction.operands[0]);
        auto *index = value(instruction.operands[1]);
        const auto &array_type = module_.value_type(input_, instruction.operands[0]);
        const auto &element = array_type.arguments.front();
        guard_array_index(array_length(array), index, instruction.location);
        store_array_element(array, array_element_slot(array, index, element),
                            value(instruction.operands[2]), element);
        break;
      }
      case NERI_IR_OPCODE_OBJECT_ALLOC_V1: {
        const auto &layout = module_.layout_for_class(*instruction.symbol);
        auto *call = builder_.CreateCall(
            module_.gc_alloc_function(),
            {module_.class_descriptor(*instruction.symbol),
             builder_.getInt64(layout.payload_size),
             builder_.getInt64(layout.payload_alignment)},
            "object.alloc");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_FIELD_LOAD_V1: {
        const auto &field = module_.field_layout(*instruction.symbol);
        result = load_field(
            field_slot(value(instruction.operands[0]), *instruction.symbol),
            field.value->value_type);
        break;
      }
      case NERI_IR_OPCODE_FIELD_STORE_V1: {
        const auto &field = module_.field_layout(*instruction.symbol);
        auto *object = value(instruction.operands[0]);
        store_field(object, field_slot(object, *instruction.symbol),
                    value(instruction.operands[1]), field.value->value_type);
        break;
      }
      case NERI_IR_OPCODE_UNSAFE_BEGIN_V1:
        result = llvm::ConstantExpr::getIntToPtr(
            builder_.getInt64(1),
            llvm::PointerType::getUnqual(module_.context_));
        break;
      case NERI_IR_OPCODE_UNSAFE_END_V1:
        break;
      case NERI_IR_OPCODE_STACK_ALLOC_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *count = value(instruction.operands[1]);
        static_cast<void>(allocation_byte_count(count, element,
                                                instruction.location));
        auto *slot = builder_.CreateAlloca(
            module_.physical_scalar_type(element), count, "stack.alloc");
        slot->setAlignment(llvm::Align(module_.storage_alignment(element)));
        result = slot;
        break;
      }
      case NERI_IR_OPCODE_POINTER_LOAD_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *loaded = builder_.CreateAlignedLoad(
            module_.physical_scalar_type(element), value(instruction.operands[1]),
            llvm::Align(module_.storage_alignment(element)), "pointer.load");
        result = from_physical_scalar(loaded, element);
        break;
      }
      case NERI_IR_OPCODE_POINTER_STORE_V1: {
        const auto &element = instruction.type_arguments.front();
        builder_.CreateAlignedStore(
            to_physical_scalar(value(instruction.operands[2]), element),
            value(instruction.operands[1]),
            llvm::Align(module_.storage_alignment(element)));
        break;
      }
      case NERI_IR_OPCODE_POINTER_OFFSET_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *byte_offset = builder_.CreateMul(
            value(instruction.operands[2]),
            builder_.getInt64(module_.storage_size(element)),
            "pointer.byte.offset");
        result = builder_.CreateGEP(builder_.getInt8Ty(),
                                    value(instruction.operands[1]), byte_offset,
                                    "pointer.offset");
        break;
      }
      case NERI_IR_OPCODE_POINTER_DIFFERENCE_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *left = builder_.CreatePtrToInt(value(instruction.operands[1]),
                                             builder_.getInt64Ty());
        auto *right = builder_.CreatePtrToInt(value(instruction.operands[2]),
                                              builder_.getInt64Ty());
        auto *bytes = builder_.CreateSub(left, right, "pointer.byte.difference");
        result = builder_.CreateSDiv(
            bytes, builder_.getInt64(module_.storage_size(element)),
            "pointer.difference");
        break;
      }
      case NERI_IR_OPCODE_POINTER_COMPARE_V1:
        result = compare_pointer(value(instruction.operands[1]),
                                 value(instruction.operands[2]),
                                 *instruction.predicate);
        break;
      case NERI_IR_OPCODE_POINTER_CAST_V1:
        result = value(instruction.operands[1]);
        break;
      case NERI_IR_OPCODE_NATIVE_ALLOC_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *bytes = allocation_byte_count(value(instruction.operands[1]),
                                            element, instruction.location);
        auto *target = instruction.flag
                           ? module_.native_alloc_zeroed_function()
                           : module_.native_alloc_function();
        auto *call = builder_.CreateCall(
            target, {bytes, builder_.getInt64(module_.storage_alignment(element))},
            "native.alloc");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_NATIVE_REALLOC_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *bytes = allocation_byte_count(value(instruction.operands[2]),
                                            element, instruction.location);
        auto *call = builder_.CreateCall(
            module_.native_realloc_function(),
            {value(instruction.operands[1]), bytes,
             builder_.getInt64(module_.storage_alignment(element))},
            "native.realloc");
        call->setCallingConv(llvm::CallingConv::C);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_NATIVE_FREE_V1: {
        auto *call = builder_.CreateCall(
            module_.native_free_function(), {value(instruction.operands[1])});
        call->setCallingConv(llvm::CallingConv::C);
        break;
      }
      case NERI_IR_OPCODE_BORROW_BEGIN_V1: {
        const auto &element = instruction.type_arguments.front();
        auto *token_type = llvm::ArrayType::get(builder_.getInt64Ty(), 4U);
        auto *token = create_entry_alloca(token_type, "borrow.token");
        builder_.CreateStore(llvm::ConstantAggregateZero::get(token_type), token);
        auto *array = value(instruction.operands[1]);
        auto *byte_length = builder_.CreateMul(
            array_length(array), builder_.getInt64(module_.storage_size(element)),
            "borrow.byte.length");
        auto *call = builder_.CreateCall(
            module_.borrow_begin_function(),
            {array,
             builder_.getInt64(module_.array_payload_element_offset(element)),
             byte_length, token},
            "borrow.pointer");
        call->setCallingConv(llvm::CallingConv::C);
        values_.emplace(instruction.results[1].id, token);
        result = call;
        break;
      }
      case NERI_IR_OPCODE_BORROW_END_V1: {
        auto *call = builder_.CreateCall(
            module_.borrow_end_function(), {value(instruction.operands[1])});
        call->setCallingConv(llvm::CallingConv::C);
        break;
      }
      default:
        throw codegen_error(std::string(lowering_error),
                            "Verified IR contains an unsupported opcode.");
      }

      if (!instruction.results.empty()) {
        values_.emplace(instruction.results.front().id, result);
        root_value(instruction.results.front().id, result);
        if (auto *named = llvm::dyn_cast<llvm::Instruction>(result);
            named != nullptr && !named->hasName()) {
          named->setName("v" + std::to_string(instruction.results.front().id));
        }
        emit_debug_local(instruction.results.front().id, result);
      }
    }

    void add_edge_arguments(const edge &edge, llvm::BasicBlock *predecessor) {
      const auto &target = module_.find_block(input_, edge.target);
      for (std::size_t index = 0; index < edge.arguments.size(); ++index) {
        phis_.at(target.parameters[index].id)
            ->addIncoming(value(edge.arguments[index]), predecessor);
      }
    }

    [[nodiscard]] llvm::BasicBlock *lower_conditional_edge(
        const edge &edge, std::string_view name) {
      if (edge.arguments.empty()) {
        return blocks_.at(edge.target);
      }
      auto *bridge = llvm::BasicBlock::Create(module_.context_,
                                              llvm::StringRef(name), &output_);
      llvm::IRBuilder<> edge_builder(bridge);
      add_edge_arguments(edge, bridge);
      edge_builder.CreateBr(blocks_.at(edge.target));
      return bridge;
    }

    void lower_terminator(const terminator &terminator) {
      switch (terminator.tag) {
      case NERI_IR_TERMINATOR_BRANCH_V1: {
        auto *predecessor = builder_.GetInsertBlock();
        const auto &edge = terminator.edges.front();
        add_edge_arguments(edge, predecessor);
        builder_.CreateBr(blocks_.at(edge.target));
        return;
      }
      case NERI_IR_TERMINATOR_CONDITIONAL_BRANCH_V1: {
        auto *true_target =
            lower_conditional_edge(terminator.edges[0], "edge.true");
        auto *false_target =
            lower_conditional_edge(terminator.edges[1], "edge.false");
        builder_.CreateCondBr(value(*terminator.condition), true_target,
                              false_target);
        return;
      }
      case NERI_IR_TERMINATOR_RETURN_V1:
        if (is_void(input_.result_type)) {
          leave_root_frame();
          builder_.CreateRetVoid();
        } else if (is_indirect_optional(input_.result_type)) {
          builder_.CreateStore(value(*terminator.return_value), result_slot_);
          leave_root_frame();
          builder_.CreateRetVoid();
        } else {
          auto *returned = to_physical_scalar(value(*terminator.return_value),
                                              input_.result_type);
          leave_root_frame();
          builder_.CreateRet(returned);
        }
        return;
      case NERI_IR_TERMINATOR_PANIC_V1:
        module_.emit_panic(
            builder_, NERI_PANIC_RUNTIME_CONTRACT_V1,
            terminator.panic_code + ": " + terminator.panic_message,
            terminator.location);
        return;
      default:
        throw codegen_error(std::string(lowering_error),
                            "Verified IR contains an unsupported terminator.");
      }
    }

    module_lowerer &module_;
    const function &input_;
    llvm::Function &output_;
    llvm::IRBuilder<> builder_;
    llvm::Argument *result_slot_{};
    llvm::AllocaInst *root_array_{};
    llvm::AllocaInst *root_frame_{};
    llvm::StructType *root_frame_type_{};
    llvm::DISubprogram *subprogram_{};
    std::map<std::uint32_t, llvm::BasicBlock *> blocks_;
    std::map<std::uint32_t, llvm::Value *> values_;
    std::map<std::uint32_t, llvm::PHINode *> phis_;
    std::map<std::uint32_t, llvm::Value *> root_slots_;
  };

  struct lowered_field final {
    const field *value{};
    std::uint64_t offset{};
  };

  struct lowered_class final {
    const class_declaration *value{};
    std::uint64_t content_size{};
    std::uint64_t payload_size{};
    std::uint64_t payload_alignment{1U};
    std::vector<lowered_field> fields;
    std::vector<const function *> virtual_methods;
    std::map<decltype(symbol_key(symbol_id{})), std::size_t> dispatch_slots;
  };

  [[nodiscard]] static std::uint64_t align_up(std::uint64_t value,
                                               std::uint64_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
  }

  [[nodiscard]] const class_declaration &find_class(const symbol_id &id) const {
    for (const auto &candidate : input_.classes) {
      if (symbol_key(candidate.id) == symbol_key(id)) {
        return candidate;
      }
    }
    throw codegen_error(std::string(lowering_error),
                        "Verified class declaration disappeared.");
  }

  const lowered_class &build_class_layout(const class_declaration &declaration) {
    const auto key = symbol_key(declaration.id);
    if (const auto found = class_layouts_.find(key);
        found != class_layouts_.end()) {
      return found->second;
    }

    lowered_class result;
    result.value = &declaration;
    if (declaration.base.has_value()) {
      const auto &base = build_class_layout(find_class(*declaration.base));
      result.content_size = base.content_size;
      result.payload_alignment = base.payload_alignment;
      result.fields = base.fields;
      result.virtual_methods = base.virtual_methods;
      result.dispatch_slots = base.dispatch_slots;
    }
    for (const auto &item : declaration.fields) {
      const auto alignment = storage_alignment(item.value_type);
      result.content_size = align_up(result.content_size, alignment);
      result.fields.push_back(
          {&item, sizeof(neri_object_header_v1) + result.content_size});
      result.content_size += storage_size(item.value_type);
      result.payload_alignment = std::max(result.payload_alignment, alignment);
    }
    result.payload_size =
        align_up(result.content_size, result.payload_alignment);

    for (const auto &item : declaration.methods) {
      if (item.dispatch != NERI_IR_DISPATCH_VIRTUAL_V1) {
        continue;
      }
      const auto slot = symbol_key(*item.dispatch_slot);
      const auto *implementation = &find_function(item.function_id);
      if (const auto found = result.dispatch_slots.find(slot);
          found != result.dispatch_slots.end()) {
        result.virtual_methods[found->second] = implementation;
      } else {
        const auto index = result.virtual_methods.size();
        result.dispatch_slots.emplace(slot, index);
        result.virtual_methods.push_back(implementation);
      }
    }

    const auto [inserted, ignored] =
        class_layouts_.emplace(key, std::move(result));
    static_cast<void>(ignored);
    for (const auto &item : inserted->second.fields) {
      field_layouts_.insert_or_assign(symbol_key(item.value->id), item);
    }
    for (const auto &[slot, index] : inserted->second.dispatch_slots) {
      if (const auto existing = dispatch_slot_indices_.find(slot);
          existing != dispatch_slot_indices_.end() && existing->second != index) {
        throw codegen_error(std::string(lowering_error),
                            "Virtual dispatch slot changed ABI index.");
      }
      dispatch_slot_indices_.insert_or_assign(slot, index);
    }
    return inserted->second;
  }

  void build_class_layouts() {
    for (const auto &declaration : input_.classes) {
      static_cast<void>(build_class_layout(declaration));
    }
  }

  [[nodiscard]] const lowered_class &layout_for_class(
      const symbol_id &id) const {
    return class_layouts_.at(symbol_key(id));
  }

  [[nodiscard]] const lowered_field &field_layout(
      const symbol_id &id) const {
    return field_layouts_.at(symbol_key(id));
  }

  [[nodiscard]] std::uint64_t field_offset(const symbol_id &id) const {
    return field_layout(id).offset;
  }

  [[nodiscard]] std::size_t dispatch_slot_index(const symbol_id &id) const {
    return dispatch_slot_indices_.at(symbol_key(id));
  }

  [[nodiscard]] const function &find_virtual_signature(
      const symbol_id &slot) const {
    const function *signature = nullptr;
    for (const auto &candidate : input_.functions) {
      if (!candidate.dispatch_slot.has_value() ||
          symbol_key(*candidate.dispatch_slot) != symbol_key(slot)) {
        continue;
      }
      if (signature == nullptr) {
        signature = &candidate;
        continue;
      }
      const auto &current_class = find_class(*signature->declaring_class);
      auto base = current_class.base;
      while (base.has_value()) {
        if (symbol_key(*base) == symbol_key(*candidate.declaring_class)) {
          signature = &candidate;
          break;
        }
        base = find_class(*base).base;
      }
    }
    if (signature == nullptr) {
      throw codegen_error(std::string(lowering_error),
                          "Verified virtual dispatch slot disappeared.");
    }
    return *signature;
  }

  [[nodiscard]] llvm::Type *semantic_type(const type &value) const {
    switch (value.tag) {
    case NERI_IR_TYPE_BOOL_V1:
      return llvm::Type::getInt1Ty(context_);
    case NERI_IR_TYPE_BYTE_V1:
      return llvm::Type::getInt8Ty(context_);
    case NERI_IR_TYPE_INT_V1:
      return llvm::Type::getInt64Ty(context_);
    case NERI_IR_TYPE_FLOAT_V1:
      return llvm::Type::getDoubleTy(context_);
    case NERI_IR_TYPE_STRING_V1:
    case NERI_IR_TYPE_ARRAY_V1:
    case NERI_IR_TYPE_CLASS_V1:
    case NERI_IR_TYPE_POINTER_V1:
    case NERI_IR_TYPE_UNSAFE_CAPABILITY_V1:
    case NERI_IR_TYPE_BORROW_CAPABILITY_V1:
      return llvm::PointerType::getUnqual(context_);
    case NERI_IR_TYPE_OPTIONAL_V1: {
      if (uses_null_representation(value)) {
        return llvm::PointerType::getUnqual(context_);
      }
      auto *tag = llvm::Type::getInt8Ty(context_);
      auto *payload = physical_scalar_type(value.arguments.front());
      if (value.arguments.front().tag == NERI_IR_TYPE_BOOL_V1 ||
          value.arguments.front().tag == NERI_IR_TYPE_BYTE_V1) {
        return llvm::StructType::get(context_, {tag, payload});
      }
      auto *padding = llvm::ArrayType::get(tag, 7U);
      return llvm::StructType::get(context_, {tag, padding, payload});
    }
    default:
      throw codegen_error(std::string(lowering_error),
                          "Verified semantic type has no LLVM mapping.");
    }
  }

  [[nodiscard]] llvm::Type *physical_scalar_type(const type &value) const {
    if (value.tag == NERI_IR_TYPE_BOOL_V1) {
      return llvm::Type::getInt8Ty(context_);
    }
    return semantic_type(value);
  }

  [[nodiscard]] llvm::Type *physical_result_type(const type &value) const {
    if (is_void(value) || is_indirect_optional(value)) {
      return llvm::Type::getVoidTy(context_);
    }
    return physical_scalar_type(value);
  }

  [[nodiscard]] llvm::FunctionType *physical_function_type(
      const std::vector<type> &parameters, const type &result) const {
    std::vector<llvm::Type *> arguments;
    if (is_indirect_optional(result)) {
      arguments.push_back(llvm::PointerType::getUnqual(context_));
    }
    for (const auto &parameter : parameters) {
      arguments.push_back(is_indirect_optional(parameter)
                              ? llvm::PointerType::getUnqual(context_)
                              : physical_scalar_type(parameter));
    }
    return llvm::FunctionType::get(physical_result_type(result), arguments,
                                   false);
  }

  [[nodiscard]] llvm::Constant *physical_scalar_constant(
      const constant &value, const type &expected) const {
    switch (expected.tag) {
    case NERI_IR_TYPE_BOOL_V1:
    case NERI_IR_TYPE_BYTE_V1:
      return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), value.bits);
    case NERI_IR_TYPE_INT_V1:
      return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), value.bits);
    case NERI_IR_TYPE_FLOAT_V1:
      return llvm::ConstantFP::get(
          context_, llvm::APFloat(llvm::APFloat::IEEEdouble(),
                                  llvm::APInt(64U, value.bits)));
    default:
      throw codegen_error(std::string(lowering_error),
                          "Verified scalar constant has no LLVM mapping.");
    }
  }

  [[nodiscard]] llvm::Constant *semantic_constant(const constant &value,
                                                  const type &expected) const {
    switch (value.tag) {
    case NERI_IR_CONSTANT_BOOL_V1:
      return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context_), value.bits);
    case NERI_IR_CONSTANT_BYTE_V1:
      return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), value.bits);
    case NERI_IR_CONSTANT_INT_V1:
      return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), value.bits);
    case NERI_IR_CONSTANT_FLOAT_V1:
      return llvm::ConstantFP::get(
          context_, llvm::APFloat(llvm::APFloat::IEEEdouble(),
                                  llvm::APInt(64U, value.bits)));
    case NERI_IR_CONSTANT_STRING_GLOBAL_V1:
      return globals_.at(symbol_key(*value.symbol));
    case NERI_IR_CONSTANT_OPTIONAL_NONE_V1:
      if (uses_null_representation(expected)) {
        return llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(context_));
      }
      return llvm::ConstantAggregateZero::get(semantic_type(expected));
    case NERI_IR_CONSTANT_OPTIONAL_SOME_V1: {
      if (uses_null_representation(expected)) {
        return semantic_constant(value.nested.front(),
                                 expected.arguments.front());
      }
      auto *aggregate_type = llvm::cast<llvm::StructType>(semantic_type(expected));
      auto *tag = llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 1U);
      auto *payload = physical_scalar_constant(value.nested.front(),
                                               expected.arguments.front());
      if (expected.arguments.front().tag == NERI_IR_TYPE_BOOL_V1 ||
          expected.arguments.front().tag == NERI_IR_TYPE_BYTE_V1) {
        return llvm::ConstantStruct::get(aggregate_type, {tag, payload});
      }
      auto *padding = llvm::ConstantAggregateZero::get(
          llvm::ArrayType::get(llvm::Type::getInt8Ty(context_), 7U));
      return llvm::ConstantStruct::get(aggregate_type,
                                       {tag, padding, payload});
    }
    default:
      throw codegen_error(std::string(lowering_error),
                          "Verified constant has no LLVM mapping.");
    }
  }

  [[nodiscard]] llvm::Constant *global_constant(const constant &value,
                                                const type &expected) const {
    return is_indirect_optional(expected) || uses_null_representation(expected)
               ? semantic_constant(value, expected)
               : physical_scalar_constant(value, expected);
  }

  [[nodiscard]] std::uint64_t storage_size(const type &value) const {
    return output_->getDataLayout()
        .getTypeAllocSize(physical_scalar_type(value))
        .getFixedValue();
  }

  [[nodiscard]] std::uint64_t storage_alignment(const type &value) const {
    return output_->getDataLayout()
        .getABITypeAlign(physical_scalar_type(value))
        .value();
  }

  [[nodiscard]] std::uint64_t array_payload_alignment(
      const type &element) const {
    return std::max<std::uint64_t>(alignof(std::uint64_t),
                                   storage_alignment(element));
  }

  [[nodiscard]] std::uint64_t array_payload_element_offset(
      const type &element) const {
    const auto alignment = storage_alignment(element);
    return (sizeof(std::uint64_t) + alignment - 1U) & ~(alignment - 1U);
  }

  [[nodiscard]] std::uint64_t array_elements_offset(
      const type &element) const {
    return sizeof(neri_object_header_v1) +
           array_payload_element_offset(element);
  }

  [[nodiscard]] std::uint64_t array_payload_size(const type &element,
                                                 std::uint64_t count) const {
    const auto prefix = array_payload_element_offset(element);
    const auto size = storage_size(element);
    if (size != 0U && count > (std::numeric_limits<std::uint64_t>::max() -
                              prefix) /
                                 size) {
      throw codegen_error(std::string(lowering_error),
                          "Array allocation size exceeds the ABI range.");
    }
    return prefix + count * size;
  }

  [[nodiscard]] llvm::StructType *type_descriptor_type() {
    if (type_descriptor_type_ == nullptr) {
      auto *pointer = llvm::PointerType::getUnqual(context_);
      type_descriptor_type_ = llvm::StructType::get(
          context_,
          {llvm::Type::getInt32Ty(context_), llvm::Type::getInt16Ty(context_),
           llvm::Type::getInt16Ty(context_), llvm::Type::getInt32Ty(context_),
           llvm::Type::getInt32Ty(context_), llvm::Type::getInt64Ty(context_),
           llvm::Type::getInt64Ty(context_), llvm::Type::getInt64Ty(context_),
           llvm::Type::getInt64Ty(context_), pointer, pointer, pointer,
           llvm::Type::getInt64Ty(context_), pointer, pointer,
           llvm::Type::getInt32Ty(context_),
           llvm::Type::getInt32Ty(context_)});
    }
    return type_descriptor_type_;
  }

  [[nodiscard]] llvm::GlobalVariable *descriptor_name(
      std::string_view key, std::string_view mangled_name) {
    if (const auto found = descriptor_names_.find(std::string(key));
        found != descriptor_names_.end()) {
      return found->second;
    }
    auto *bytes = llvm::ConstantDataArray::getString(context_, mangled_name,
                                                     true);
    auto *name = new llvm::GlobalVariable(
        *output_, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage,
        bytes, ".hk.type.name." + std::string(key));
    name->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    name->setAlignment(llvm::Align(1));
    descriptor_names_.emplace(std::string(key), name);
    return name;
  }

  [[nodiscard]] llvm::GlobalVariable *scalar_descriptor(const type &value) {
    const auto key = type_code(value);
    if (const auto found = type_descriptors_.find(key);
        found != type_descriptors_.end()) {
      return found->second;
    }
    const auto scalar_kind = [&] {
      switch (value.tag) {
      case NERI_IR_TYPE_BOOL_V1:
        return NERI_SCALAR_KIND_BOOL_V1;
      case NERI_IR_TYPE_BYTE_V1:
        return NERI_SCALAR_KIND_BYTE_V1;
      case NERI_IR_TYPE_INT_V1:
        return NERI_SCALAR_KIND_INT_V1;
      case NERI_IR_TYPE_FLOAT_V1:
        return NERI_SCALAR_KIND_FLOAT_V1;
      default:
        throw codegen_error(std::string(lowering_error),
                            "Array element has no scalar ABI descriptor.");
      }
    }();
    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *null_pointer = llvm::ConstantPointerNull::get(pointer);
    auto *descriptor = new llvm::GlobalVariable(
        *output_, type_descriptor_type(), true,
        llvm::GlobalValue::PrivateLinkage, nullptr, ".hk.type." + key);
    type_descriptors_.emplace(key, descriptor);
    descriptor->setInitializer(llvm::ConstantStruct::get(
        type_descriptor_type(),
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                sizeof(neri_type_descriptor_v1)),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                NERI_RUNTIME_ABI_MAJOR),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                NERI_RUNTIME_ABI_MINOR),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                NERI_TYPE_KIND_SCALAR_V1),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0U),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                storage_size(value)),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                storage_alignment(value)),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
         null_pointer, null_pointer,
         descriptor_name(key, "hk1_t_" + key),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
         null_pointer, null_pointer,
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), scalar_kind),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0U)}));
    descriptor->setAlignment(llvm::Align(8));
    return descriptor;
  }

  [[nodiscard]] llvm::Function *array_trace_function(const type &element) {
    const auto key = type_code(element);
    if (const auto found = array_trace_functions_.find(key);
        found != array_trace_functions_.end()) {
      return found->second;
    }
    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *trace_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context_), {pointer, pointer, pointer}, false);
    auto *trace = llvm::Function::Create(
        trace_type, llvm::GlobalValue::PrivateLinkage, ".hk.trace.array." + key,
        output_.get());
    trace->setCallingConv(llvm::CallingConv::C);
    trace->addFnAttr(llvm::Attribute::NoUnwind);
    array_trace_functions_.emplace(key, trace);

    auto argument = trace->arg_begin();
    auto *object = &*argument++;
    object->setName("object");
    auto *visit = &*argument++;
    visit->setName("visit");
    auto *context = &*argument;
    context->setName("context");
    auto *entry = llvm::BasicBlock::Create(context_, "entry", trace);
    auto *condition = llvm::BasicBlock::Create(context_, "condition", trace);
    auto *body = llvm::BasicBlock::Create(context_, "body", trace);
    auto *done = llvm::BasicBlock::Create(context_, "done", trace);
    llvm::IRBuilder<> builder(entry);
    auto *length_slot = builder.CreateInBoundsGEP(
        builder.getInt8Ty(), object,
        builder.getInt64(sizeof(neri_object_header_v1)));
    auto *length = builder.CreateAlignedLoad(
        builder.getInt64Ty(), length_slot, llvm::Align(alignof(std::uint64_t)));
    builder.CreateBr(condition);
    builder.SetInsertPoint(condition);
    auto *index = builder.CreatePHI(builder.getInt64Ty(), 2U, "index");
    index->addIncoming(builder.getInt64(0U), entry);
    auto *has_next = builder.CreateICmpULT(index, length, "has.next");
    builder.CreateCondBr(has_next, body, done);
    builder.SetInsertPoint(body);
    auto *offset = builder.CreateAdd(
        builder.getInt64(array_elements_offset(element)),
        builder.CreateMul(index, builder.getInt64(storage_size(element))));
    auto *slot = builder.CreateInBoundsGEP(builder.getInt8Ty(), object, offset);
    auto *visit_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context_), {pointer, pointer}, false);
    auto *call = builder.CreateCall(visit_type, visit, {slot, context});
    call->setCallingConv(llvm::CallingConv::C);
    auto *next = builder.CreateAdd(index, builder.getInt64(1U), "next");
    builder.CreateBr(condition);
    index->addIncoming(next, body);
    builder.SetInsertPoint(done);
    builder.CreateRetVoid();
    return trace;
  }

  [[nodiscard]] llvm::GlobalVariable *element_descriptor(const type &value) {
    if (is_optional(value) && is_managed_reference(value)) {
      return element_descriptor(value.arguments.front());
    }
    if (is_string(value)) {
      return string_literal_descriptor();
    }
    if (is_array(value)) {
      return array_descriptor(value.arguments.front());
    }
    if (is_class(value)) {
      return class_descriptor(*value.symbol);
    }
    return scalar_descriptor(value);
  }

  [[nodiscard]] llvm::GlobalVariable *array_descriptor(const type &element) {
    const auto key = "a" + type_code(element) + "e";
    if (const auto found = type_descriptors_.find(key);
        found != type_descriptors_.end()) {
      return found->second;
    }
    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *null_pointer = llvm::ConstantPointerNull::get(pointer);
    auto *descriptor = new llvm::GlobalVariable(
        *output_, type_descriptor_type(), true,
        llvm::GlobalValue::PrivateLinkage, nullptr, ".hk.type." + key);
    type_descriptors_.emplace(key, descriptor);
    auto *trace = is_managed_reference(element)
                      ? static_cast<llvm::Constant *>(array_trace_function(element))
                      : null_pointer;
    const auto flags = is_managed_reference(element)
                           ? NERI_TYPE_FLAG_CONTAINS_REFS_V1
                           : 0U;
    descriptor->setInitializer(llvm::ConstantStruct::get(
        type_descriptor_type(),
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                sizeof(neri_type_descriptor_v1)),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                NERI_RUNTIME_ABI_MAJOR),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                NERI_RUNTIME_ABI_MINOR),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                NERI_TYPE_KIND_ARRAY_V1),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), flags),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                array_payload_element_offset(element)),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                array_payload_alignment(element)),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                storage_size(element)),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                storage_alignment(element)),
         trace, null_pointer, descriptor_name(key, "hk1_t_" + key),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
         element_descriptor(element), null_pointer,
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                NERI_SCALAR_KIND_NONE_V1),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0U)}));
    descriptor->setAlignment(llvm::Align(8));
    return descriptor;
  }

  [[nodiscard]] llvm::Function *class_trace_function(
      const lowered_class &layout) {
    const auto key = type_code(
        type{NERI_IR_TYPE_CLASS_V1, layout.value->id, {}});
    if (const auto found = class_trace_functions_.find(key);
        found != class_trace_functions_.end()) {
      return found->second;
    }
    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *trace = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                {pointer, pointer, pointer}, false),
        llvm::GlobalValue::PrivateLinkage, ".hk.trace.class." + key,
        output_.get());
    trace->setCallingConv(llvm::CallingConv::C);
    trace->addFnAttr(llvm::Attribute::NoUnwind);
    class_trace_functions_.emplace(key, trace);

    auto argument = trace->arg_begin();
    auto *object = &*argument++;
    object->setName("object");
    auto *visit = &*argument++;
    visit->setName("visit");
    auto *visit_context = &*argument;
    visit_context->setName("context");
    auto *entry = llvm::BasicBlock::Create(context_, "entry", trace);
    llvm::IRBuilder<> builder(entry);
    auto *visit_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context_), {pointer, pointer}, false);
    for (const auto &item : layout.fields) {
      if (!is_managed_reference(item.value->value_type)) {
        continue;
      }
      auto *slot = builder.CreateInBoundsGEP(
          builder.getInt8Ty(), object, builder.getInt64(item.offset));
      auto *call = builder.CreateCall(visit_type, visit, {slot, visit_context});
      call->setCallingConv(llvm::CallingConv::C);
    }
    builder.CreateRetVoid();
    return trace;
  }

  [[nodiscard]] llvm::GlobalVariable *class_descriptor(
      const symbol_id &id) const {
    return class_descriptors_.at(symbol_key(id));
  }

  void declare_class_metadata() {
    for (const auto &declaration : input_.classes) {
      const auto key = type_code(
          type{NERI_IR_TYPE_CLASS_V1, declaration.id, {}});
      auto *descriptor = new llvm::GlobalVariable(
          *output_, type_descriptor_type(), true,
          llvm::GlobalValue::PrivateLinkage, nullptr, ".hk.type." + key);
      descriptor->setAlignment(llvm::Align(8));
      class_descriptors_.emplace(symbol_key(declaration.id), descriptor);
      type_descriptors_.emplace(key, descriptor);
    }

    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *null_pointer = llvm::ConstantPointerNull::get(pointer);
    for (const auto &declaration : input_.classes) {
      const auto &layout = layout_for_class(declaration.id);
      const auto key = type_code(
          type{NERI_IR_TYPE_CLASS_V1, declaration.id, {}});
      llvm::Constant *method_table = null_pointer;
      if (!layout.virtual_methods.empty()) {
        auto *table_type =
            llvm::ArrayType::get(pointer, layout.virtual_methods.size());
        std::vector<llvm::Constant *> methods;
        methods.reserve(layout.virtual_methods.size());
        for (const auto *method : layout.virtual_methods) {
          methods.push_back(functions_.at(symbol_key(method->id)));
        }
        auto *table = new llvm::GlobalVariable(
            *output_, table_type, true, llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(table_type, methods),
            ".hk.vtable." + key);
        table->setAlignment(llvm::Align(alignof(void *)));
        method_table = table;
        class_method_tables_.emplace(symbol_key(declaration.id), table);
      }

      const auto has_references = std::ranges::any_of(
          layout.fields, [](const auto &item) {
            return is_managed_reference(item.value->value_type);
          });
      llvm::Constant *trace = has_references
                                  ? static_cast<llvm::Constant *>(
                                        class_trace_function(layout))
                                  : null_pointer;
      const auto flags = has_references ? NERI_TYPE_FLAG_CONTAINS_REFS_V1 : 0U;
      class_descriptor(declaration.id)->setInitializer(llvm::ConstantStruct::get(
          type_descriptor_type(),
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                  sizeof(neri_type_descriptor_v1)),
           llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                  NERI_RUNTIME_ABI_MAJOR),
           llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                  NERI_RUNTIME_ABI_MINOR),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                  NERI_TYPE_KIND_CLASS_V1),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), flags),
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                  layout.payload_size),
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                  layout.payload_alignment),
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
           trace, method_table, descriptor_name(key, "hk1_t_" + key),
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
           null_pointer, null_pointer,
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                  NERI_SCALAR_KIND_NONE_V1),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0U)}));
    }
  }

  void emit_program_requirements() {
    std::uint16_t minimum_minor = source_location_runtime_minor;
    std::uint64_t required_features = NERI_RT_FEATURE_SOURCE_LOCATIONS;
    if (std::ranges::find(input_.required_features, "native-strings-v1") !=
        input_.required_features.end()) {
      minimum_minor = std::max(minimum_minor, native_string_runtime_minor);
      required_features |= NERI_RT_FEATURE_NATIVE_STRINGS;
    }
    const auto module_uses_arrays = [&] {
      for (const auto &global : input_.globals) {
        if (contains_array(global.value_type)) {
          return true;
        }
      }
      for (const auto &function : input_.functions) {
        if (contains_array(function.result_type) ||
            std::ranges::any_of(function.parameter_types, contains_array)) {
          return true;
        }
        for (const auto &block : function.blocks) {
          if (std::ranges::any_of(block.parameters, [](const auto &parameter) {
                return contains_array(parameter.value_type);
              })) {
            return true;
          }
          for (const auto &instruction : block.instructions) {
            if (std::ranges::any_of(instruction.results, [](const auto &result) {
                  return contains_array(result.value_type);
                }) ||
                std::ranges::any_of(instruction.type_arguments,
                                    contains_array)) {
              return true;
            }
          }
        }
      }
      return false;
    }();
    if (module_uses_arrays) {
      minimum_minor = std::max(minimum_minor, native_array_runtime_minor);
    }
    if (!input_.classes.empty()) {
      minimum_minor = std::max(minimum_minor, native_class_runtime_minor);
    }
    for (const auto &import : input_.imports) {
      if (import.kind == NERI_IR_IMPORT_RUNTIME_V1 &&
          import.minimum_runtime.has_value()) {
        minimum_minor =
            std::max(minimum_minor, import.minimum_runtime->abi_minor);
        required_features |= import.minimum_runtime->feature_bits;
      }
    }

    auto *requirements_type = llvm::StructType::get(
        context_, {llvm::Type::getInt32Ty(context_),
                   llvm::Type::getInt16Ty(context_),
                   llvm::Type::getInt16Ty(context_),
                   llvm::Type::getInt64Ty(context_)});
    auto *requirements = llvm::ConstantStruct::get(
        requirements_type,
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                sizeof(neri_runtime_abi_requirements_v1)),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                NERI_RUNTIME_ABI_MAJOR),
         llvm::ConstantInt::get(llvm::Type::getInt16Ty(context_),
                                minimum_minor),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                required_features)});
    auto *declaration = new llvm::GlobalVariable(
        *output_, requirements_type, true, llvm::GlobalValue::ExternalLinkage,
        requirements, "neri_program_v1_abi_requirements");
    declaration->setAlignment(llvm::Align(8));
  }

  [[nodiscard]] llvm::GlobalVariable *string_literal_descriptor() {
    constexpr std::string_view name = "neri_rt_v1_string_literal_type";
    if (auto *existing = output_->getNamedGlobal(name)) {
      return existing;
    }
    return new llvm::GlobalVariable(
        *output_, llvm::Type::getInt8Ty(context_), true,
        llvm::GlobalValue::ExternalLinkage, nullptr, name);
  }

  void declare_globals() {
    for (const auto &global : input_.globals) {
      const auto linkage = global.linkage == NERI_IR_GLOBAL_INTERNAL_V1
                               ? llvm::GlobalValue::InternalLinkage
                               : llvm::GlobalValue::ExternalLinkage;
      if (is_string(global.value_type)) {
        auto *bytes = llvm::ConstantDataArray::getString(
            context_, global.initializer.text, true);
        auto *literal_type = llvm::StructType::get(
            context_,
            {llvm::PointerType::getUnqual(context_),
             llvm::Type::getInt64Ty(context_),
             llvm::Type::getInt64Ty(context_), bytes->getType()});
        auto *initializer = llvm::ConstantStruct::get(
            literal_type,
            {string_literal_descriptor(),
             llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0U),
             llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                    global.initializer.text.size()),
             bytes});
        auto *declaration = new llvm::GlobalVariable(
            *output_, literal_type, true, linkage, initializer,
            mangle_global(global));
        declaration->setAlignment(llvm::Align(8));
        declaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        globals_.emplace(symbol_key(global.id), declaration);
        continue;
      }

      auto *value_type = is_indirect_optional(global.value_type)
                             ? semantic_type(global.value_type)
                             : physical_scalar_type(global.value_type);
      auto *declaration = new llvm::GlobalVariable(
          *output_, value_type, true, linkage,
          global_constant(global.initializer, global.value_type),
          mangle_global(global));
      declaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      globals_.emplace(symbol_key(global.id), declaration);
    }
  }

  void declare_imports() {
    std::map<std::string, llvm::Function *> declarations;
    for (const auto &import : input_.imports) {
      auto *declaration = [&]() {
        if (const auto existing = declarations.find(import.link_name);
            existing != declarations.end()) {
          return existing->second;
        }
        auto *created = llvm::Function::Create(
            physical_function_type(import.parameter_types, import.result_type),
            llvm::GlobalValue::ExternalLinkage, import.link_name, output_.get());
        created->setCallingConv(llvm::CallingConv::C);
        created->addFnAttr(llvm::Attribute::NoUnwind);
        if ((import.effects & NERI_IR_EFFECT_NO_RETURN_V1) != 0U) {
          created->addFnAttr(llvm::Attribute::NoReturn);
        }
        declarations.emplace(import.link_name, created);
        return created;
      }();
      imports_.emplace(symbol_key(import.id), declaration);
    }
  }

  void declare_functions() {
    for (const auto &function : input_.functions) {
      auto *declaration = llvm::Function::Create(
          physical_function_type(function.parameter_types,
                                 function.result_type),
          llvm::GlobalValue::ExternalLinkage, mangle_function(function),
          output_.get());
      declaration->setCallingConv(llvm::CallingConv::C);
      declaration->addFnAttr(llvm::Attribute::NoUnwind);
      if ((function.effects & NERI_IR_EFFECT_NO_RETURN_V1) != 0U) {
        declaration->addFnAttr(llvm::Attribute::NoReturn);
      }
      functions_.emplace(symbol_key(function.id), declaration);
    }
  }

  void lower_functions() {
    for (const auto &function : input_.functions) {
      function_lowerer(*this, function, *functions_.at(symbol_key(function.id)))
          .lower();
    }
  }

  [[nodiscard]] const function &find_function(const symbol_id &id) const {
    for (const auto &candidate : input_.functions) {
      if (symbol_key(candidate.id) == symbol_key(id)) {
        return candidate;
      }
    }
    throw codegen_error(std::string(lowering_error),
                        "Verified direct-call target disappeared.");
  }

  [[nodiscard]] const import_declaration &
  find_import(const symbol_id &id) const {
    for (const auto &candidate : input_.imports) {
      if (symbol_key(candidate.id) == symbol_key(id)) {
        return candidate;
      }
    }
    throw codegen_error(std::string(lowering_error),
                        "Verified import target disappeared.");
  }

  [[nodiscard]] const block &find_block(const function &function,
                                        std::uint32_t id) const {
    for (const auto &candidate : function.blocks) {
      if (candidate.id == id) {
        return candidate;
      }
    }
    throw codegen_error(std::string(lowering_error),
                        "Verified branch target disappeared.");
  }

  [[nodiscard]] const type &value_type(const function &function,
                                       std::uint32_t id) const {
    if (function.unsafe_root.has_value() && function.unsafe_root->id == id) {
      return function.unsafe_root->value_type;
    }
    for (const auto &block : function.blocks) {
      for (const auto &parameter : block.parameters) {
        if (parameter.id == id) {
          return parameter.value_type;
        }
      }
      for (const auto &instruction : block.instructions) {
        for (const auto &result : instruction.results) {
          if (result.id == id) {
            return result.value_type;
          }
        }
      }
    }
    throw codegen_error(std::string(lowering_error),
                        "Verified SSA type disappeared.");
  }

  [[nodiscard]] llvm::Function *runtime_function(
      std::string_view name, llvm::Type *result,
      llvm::ArrayRef<llvm::Type *> parameters) {
    if (auto *existing = output_->getFunction(name)) {
      return existing;
    }
    auto *declaration = llvm::Function::Create(
        llvm::FunctionType::get(result, parameters, false),
        llvm::GlobalValue::ExternalLinkage, name, output_.get());
    declaration->setCallingConv(llvm::CallingConv::C);
    declaration->addFnAttr(llvm::Attribute::NoUnwind);
    return declaration;
  }

  [[nodiscard]] llvm::Function *root_frame_enter_function() {
    return runtime_function(
        "neri_rt_v1_gc_root_frame_enter",
        llvm::Type::getVoidTy(context_),
        {llvm::PointerType::getUnqual(context_)});
  }

  [[nodiscard]] llvm::Function *root_frame_leave_function() {
    return runtime_function(
        "neri_rt_v1_gc_root_frame_leave",
        llvm::Type::getVoidTy(context_),
        {llvm::PointerType::getUnqual(context_)});
  }

  [[nodiscard]] llvm::Function *gc_alloc_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_gc_alloc", pointer,
                            {pointer, llvm::Type::getInt64Ty(context_),
                             llvm::Type::getInt64Ty(context_)});
  }

  [[nodiscard]] llvm::Function *gc_store_ref_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_gc_store_ref",
                            llvm::Type::getVoidTy(context_),
                            {pointer, pointer, pointer});
  }

  [[nodiscard]] llvm::Function *borrow_begin_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_gc_borrow_begin", pointer,
                            {pointer, llvm::Type::getInt64Ty(context_),
                             llvm::Type::getInt64Ty(context_), pointer});
  }

  [[nodiscard]] llvm::Function *borrow_end_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_gc_borrow_end",
                            llvm::Type::getVoidTy(context_), {pointer});
  }

  [[nodiscard]] llvm::Function *native_alloc_function() {
    auto *integer = llvm::Type::getInt64Ty(context_);
    return runtime_function("neri_rt_v1_native_alloc",
                            llvm::PointerType::getUnqual(context_),
                            {integer, integer});
  }

  [[nodiscard]] llvm::Function *native_alloc_zeroed_function() {
    auto *integer = llvm::Type::getInt64Ty(context_);
    return runtime_function("neri_rt_v1_native_alloc_zeroed",
                            llvm::PointerType::getUnqual(context_),
                            {integer, integer});
  }

  [[nodiscard]] llvm::Function *native_realloc_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    auto *integer = llvm::Type::getInt64Ty(context_);
    return runtime_function("neri_rt_v1_native_realloc", pointer,
                            {pointer, integer, integer});
  }

  [[nodiscard]] llvm::Function *native_free_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_native_free",
                            llvm::Type::getVoidTy(context_), {pointer});
  }

  [[nodiscard]] llvm::Function *string_concat_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_string_concat", pointer,
                            {pointer, pointer});
  }

  [[nodiscard]] llvm::Function *string_equal_function() {
    auto *pointer = llvm::PointerType::getUnqual(context_);
    return runtime_function("neri_rt_v1_string_equal",
                            llvm::Type::getInt8Ty(context_),
                            {pointer, pointer});
  }

  [[nodiscard]] llvm::Function *string_from_int_function() {
    return runtime_function(
        "neri_rt_v1_string_from_int",
        llvm::PointerType::getUnqual(context_),
        {llvm::Type::getInt64Ty(context_)});
  }

  [[nodiscard]] llvm::Function *string_from_byte_function() {
    return runtime_function(
        "neri_rt_v1_string_from_byte",
        llvm::PointerType::getUnqual(context_),
        {llvm::Type::getInt8Ty(context_)});
  }

  [[nodiscard]] llvm::Function *string_from_float_function() {
    return runtime_function(
        "neri_rt_v1_string_from_float",
        llvm::PointerType::getUnqual(context_),
        {llvm::Type::getDoubleTy(context_)});
  }

  struct lowered_panic final {
    llvm::GlobalVariable *descriptor;
    llvm::Constant *location;
  };

  [[nodiscard]] llvm::Function *panic_function() {
    constexpr std::string_view name = "neri_rt_v1_panic_at";
    if (auto *existing = output_->getFunction(name)) {
      return existing;
    }
    auto *declaration = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                {llvm::PointerType::getUnqual(context_),
                                 llvm::PointerType::getUnqual(context_)},
                                false),
        llvm::GlobalValue::ExternalLinkage, name, output_.get());
    declaration->setCallingConv(llvm::CallingConv::C);
    declaration->addFnAttr(llvm::Attribute::NoReturn);
    declaration->addFnAttr(llvm::Attribute::NoUnwind);
    declaration->addFnAttr(llvm::Attribute::Cold);
    return declaration;
  }

  [[nodiscard]] lowered_panic panic_descriptor(
      std::uint32_t code, std::string_view message,
      const std::optional<source_location> &location) {
    const auto key = std::tuple(
        code, std::string(message),
        location.has_value() ? location->source : std::string(),
        location.has_value() ? location->utf8_start : 0U,
        location.has_value() ? location->utf8_length : 0U);
    if (const auto found = panic_descriptors_.find(key);
        found != panic_descriptors_.end()) {
      return found->second;
    }

    const auto index = panic_descriptors_.size();
    auto *message_value =
        llvm::ConstantDataArray::getString(context_, message, false);
    auto *message_global = new llvm::GlobalVariable(
        *output_, message_value->getType(), true,
        llvm::GlobalValue::PrivateLinkage, message_value,
        ".hk.panic.message." + std::to_string(index));
    message_global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    message_global->setAlignment(llvm::Align(1));

    llvm::Constant *location_pointer =
        llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context_));
    if (location.has_value()) {
      auto *source_value =
          llvm::ConstantDataArray::getString(context_, location->source, false);
      auto *source_global = new llvm::GlobalVariable(
          *output_, source_value->getType(), true,
          llvm::GlobalValue::PrivateLinkage, source_value,
          ".hk.panic.source." + std::to_string(index));
      source_global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      source_global->setAlignment(llvm::Align(1));
      auto *location_type = llvm::StructType::get(
          context_, {llvm::PointerType::getUnqual(context_),
                     llvm::Type::getInt64Ty(context_),
                     llvm::Type::getInt32Ty(context_),
                     llvm::Type::getInt32Ty(context_)});
      auto *location_value = llvm::ConstantStruct::get(
          location_type,
          {source_global,
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                  location->source.size()),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                  location->utf8_start),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                  location->utf8_length)});
      auto *location_global = new llvm::GlobalVariable(
          *output_, location_type, true, llvm::GlobalValue::PrivateLinkage,
          location_value, ".hk.panic.location." + std::to_string(index));
      location_global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      location_pointer = location_global;
    }

    auto *descriptor_type = llvm::StructType::get(
        context_, {llvm::Type::getInt32Ty(context_),
                   llvm::Type::getInt32Ty(context_),
                   llvm::PointerType::getUnqual(context_),
                   llvm::Type::getInt64Ty(context_)});
    auto *descriptor_value = llvm::ConstantStruct::get(
        descriptor_type,
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), code),
         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0U),
         message_global,
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                message.size())});
    auto *descriptor = new llvm::GlobalVariable(
        *output_, descriptor_type, true, llvm::GlobalValue::PrivateLinkage,
        descriptor_value, ".hk.panic." + std::to_string(index));
    descriptor->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    const lowered_panic result{descriptor, location_pointer};
    panic_descriptors_.emplace(key, result);
    return result;
  }

  void emit_panic(llvm::IRBuilder<> &builder, std::uint32_t code,
                  std::string_view message,
                  const std::optional<source_location> &location) {
    const auto panic = panic_descriptor(code, message, location);
    auto *call = builder.CreateCall(panic_function(),
                                    {panic.descriptor, panic.location});
    call->setCallingConv(llvm::CallingConv::C);
    call->setDoesNotReturn();
    call->setDoesNotThrow();
    builder.CreateUnreachable();
  }

  const ir_module &input_;
  llvm::LLVMContext &context_;
  std::unique_ptr<llvm::Module> output_;
  bool emit_debug_information_{};
  std::unique_ptr<llvm::DIBuilder> debug_builder_;
  std::map<std::string, llvm::DIFile *> debug_files_;
  std::map<std::string, llvm::DIType *> debug_types_;
  std::map<decltype(symbol_key(symbol_id{})), llvm::GlobalVariable *> globals_;
  std::map<decltype(symbol_key(symbol_id{})), llvm::Function *> imports_;
  std::map<decltype(symbol_key(symbol_id{})), llvm::Function *> functions_;
  std::map<decltype(symbol_key(symbol_id{})), lowered_class> class_layouts_;
  std::map<decltype(symbol_key(symbol_id{})), lowered_field> field_layouts_;
  std::map<decltype(symbol_key(symbol_id{})), std::size_t>
      dispatch_slot_indices_;
  std::map<decltype(symbol_key(symbol_id{})), llvm::GlobalVariable *>
      class_descriptors_;
  std::map<decltype(symbol_key(symbol_id{})), llvm::GlobalVariable *>
      class_method_tables_;
  llvm::StructType *type_descriptor_type_{};
  std::map<std::string, llvm::GlobalVariable *> descriptor_names_;
  std::map<std::string, llvm::GlobalVariable *> type_descriptors_;
  std::map<std::string, llvm::Function *> array_trace_functions_;
  std::map<std::string, llvm::Function *> class_trace_functions_;
  std::map<std::tuple<std::uint32_t, std::string, std::string, std::uint32_t,
                      std::uint32_t>,
           lowered_panic>
      panic_descriptors_;

  friend class function_lowerer;
};

} // namespace

std::unique_ptr<llvm::Module>
lower_to_llvm(const verified_module &input, llvm::LLVMContext &context,
              const llvm::Triple &triple, const llvm::DataLayout &layout,
              bool emit_debug_information) {
  return module_lowerer(input.value(), context, triple, layout,
                        emit_debug_information)
      .lower();
}

} // namespace neri::codegen
