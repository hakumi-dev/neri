#include "neri/codegen/emitter.h"

#include "llvm_lowering.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace neri::codegen {
namespace {

constexpr std::string_view driver_error = "NCG001";
constexpr std::string_view verification_error = "NCG003";
constexpr std::string_view emission_error = "NCG004";
constexpr std::string_view output_error = "NCG005";

void initialize_targets() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
  });
}

[[nodiscard]] std::unique_ptr<llvm::TargetMachine>
create_target_machine(target_platform target, optimization_mode optimization) {
  initialize_targets();

  const llvm::Triple triple(std::string(target_triple(target)));
  std::string lookup_error;
  const auto *backend = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (backend == nullptr) {
    throw codegen_error(
        std::string(emission_error),
        "LLVM cannot select target '" + triple.str() + "': " + lookup_error);
  }

  const llvm::TargetOptions options;
  auto machine = std::unique_ptr<llvm::TargetMachine>(backend->createTargetMachine(
      triple, "generic", "", options, llvm::Reloc::PIC_,
      llvm::CodeModel::Small,
      optimization == optimization_mode::debug ? llvm::CodeGenOptLevel::None
                                               : llvm::CodeGenOptLevel::Default));
  if (machine == nullptr) {
    throw codegen_error(std::string(emission_error),
                        "LLVM could not create a target machine for '" +
                            triple.str() + "'.");
  }
  return machine;
}

void verify_module(const llvm::Module &module, std::string_view stage) {
  std::string detail;
  llvm::raw_string_ostream stream(detail);
  if (llvm::verifyModule(module, &stream)) {
    stream.flush();
    throw codegen_error(std::string(verification_error),
                        "LLVM rejected the module " + std::string(stage) +
                            ": " + detail);
  }
}

void optimize_module(llvm::Module &module, llvm::TargetMachine &machine,
                     optimization_mode optimization) {
  llvm::LoopAnalysisManager loops;
  llvm::FunctionAnalysisManager functions;
  llvm::CGSCCAnalysisManager call_graph;
  llvm::ModuleAnalysisManager modules;
  llvm::PassBuilder passes(&machine);
  passes.registerModuleAnalyses(modules);
  passes.registerCGSCCAnalyses(call_graph);
  passes.registerFunctionAnalyses(functions);
  passes.registerLoopAnalyses(loops);
  passes.crossRegisterProxies(loops, functions, call_graph, modules);

  auto pipeline = optimization == optimization_mode::debug
                      ? passes.buildO0DefaultPipeline(llvm::OptimizationLevel::O0)
                      : passes.buildPerModuleDefaultPipeline(
                            llvm::OptimizationLevel::O2);
  pipeline.run(module, modules);
}

[[nodiscard]] artifact print_llvm_ir(const llvm::Module &module) {
  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream stream(buffer);
  module.print(stream, nullptr);
  return {std::vector<std::uint8_t>(buffer.begin(), buffer.end()), true};
}

[[nodiscard]] artifact emit_target_file(llvm::Module &module,
                                        llvm::TargetMachine &machine,
                                        output_kind kind) {
  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream stream(buffer);
  llvm::legacy::PassManager passes;
  const auto file_type = kind == output_kind::assembly
                             ? llvm::CodeGenFileType::AssemblyFile
                             : llvm::CodeGenFileType::ObjectFile;
  if (machine.addPassesToEmitFile(passes, stream, nullptr, file_type,
                                  false)) {
    throw codegen_error(
        std::string(emission_error),
        "LLVM target '" + module.getTargetTriple().str() +
            "' cannot emit " + std::string(output_kind_name(kind)) + ".");
  }
  passes.run(module);
  return {std::vector<std::uint8_t>(buffer.begin(), buffer.end()),
          kind == output_kind::assembly};
}

} // namespace

codegen_error::codegen_error(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

const std::string &codegen_error::code() const noexcept { return code_; }

target_platform parse_target(std::string_view value) {
  if (value == target_name(target_platform::macos_arm64)) {
    return target_platform::macos_arm64;
  }
  if (value == target_name(target_platform::linux_x86_64)) {
    return target_platform::linux_x86_64;
  }
  throw codegen_error(
      std::string(driver_error), "Unsupported target '" + std::string(value) +
                                     "'; expected macos-arm64 or linux-x86_64.");
}

optimization_mode parse_optimization(std::string_view value) {
  if (value == optimization_name(optimization_mode::debug)) {
    return optimization_mode::debug;
  }
  if (value == optimization_name(optimization_mode::release)) {
    return optimization_mode::release;
  }
  throw codegen_error(std::string(driver_error),
                      "Unsupported optimization mode '" + std::string(value) +
                          "'; expected debug or release.");
}

output_kind parse_output_kind(std::string_view value) {
  if (value == output_kind_name(output_kind::llvm_ir)) {
    return output_kind::llvm_ir;
  }
  if (value == output_kind_name(output_kind::assembly)) {
    return output_kind::assembly;
  }
  if (value == output_kind_name(output_kind::object)) {
    return output_kind::object;
  }
  throw codegen_error(std::string(driver_error),
                      "Unsupported output kind '" + std::string(value) +
                          "'; expected llvm-ir, assembly, or object.");
}

std::string_view target_name(target_platform target) noexcept {
  switch (target) {
  case target_platform::macos_arm64:
    return "macos-arm64";
  case target_platform::linux_x86_64:
    return "linux-x86_64";
  }
  return "unknown";
}

std::string_view target_triple(target_platform target) noexcept {
  switch (target) {
  case target_platform::macos_arm64:
    return "arm64-apple-macosx";
  case target_platform::linux_x86_64:
    return "x86_64-unknown-linux-gnu";
  }
  return "unknown-unknown-unknown";
}

std::string_view optimization_name(optimization_mode mode) noexcept {
  switch (mode) {
  case optimization_mode::debug:
    return "debug";
  case optimization_mode::release:
    return "release";
  }
  return "unknown";
}

std::string_view output_kind_name(output_kind kind) noexcept {
  switch (kind) {
  case output_kind::llvm_ir:
    return "llvm-ir";
  case output_kind::assembly:
    return "assembly";
  case output_kind::object:
    return "object";
  }
  return "unknown";
}

artifact emit_module(const verified_module &input, target_platform target,
                     optimization_mode optimization, output_kind kind,
                     emission_metrics *metrics) {
  auto machine = create_target_machine(target, optimization);
  llvm::LLVMContext context;
  const llvm::Triple triple(std::string(target_triple(target)));
  const auto lowering_started = std::chrono::steady_clock::now();
  auto module = lower_to_llvm(input, context, triple, machine->createDataLayout(),
                              optimization == optimization_mode::debug);

  verify_module(*module, "before optimization");
  const auto optimization_started = std::chrono::steady_clock::now();
  optimize_module(*module, *machine, optimization);
  verify_module(*module, "before artifact emission");
  const auto codegen_started = std::chrono::steady_clock::now();

  artifact result;
  if (kind == output_kind::llvm_ir) {
    result = print_llvm_ir(*module);
  } else {
    result = emit_target_file(*module, *machine, kind);
  }
  if (metrics != nullptr) {
    const auto completed = std::chrono::steady_clock::now();
    metrics->lowering_and_preverify_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            optimization_started - lowering_started)
            .count());
    metrics->llvm_optimization_and_postverify_ns =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                codegen_started - optimization_started)
                .count());
    metrics->target_codegen_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            completed - codegen_started)
            .count());
  }
  return result;
}

void write_artifact_atomically(const std::filesystem::path &path,
                               std::span<const std::uint8_t> bytes) {
  if (path.empty() || path.filename().empty()) {
    throw codegen_error(std::string(output_error),
                        "Output path must name a file.");
  }

  const auto model = path.string() + ".tmp-%%%%%%";
  int descriptor = -1;
  llvm::SmallString<256> temporary;
  if (const auto error =
          llvm::sys::fs::createUniqueFile(model, descriptor, temporary)) {
    throw codegen_error(std::string(output_error),
                        "Cannot create an atomic output beside '" +
                            path.string() + "': " + error.message());
  }

  std::error_code write_error;
  {
    llvm::raw_fd_ostream stream(descriptor, true);
    stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    stream.close();
    if (stream.has_error()) {
      write_error = stream.error();
      stream.clear_error();
    }
  }
  if (write_error) {
    static_cast<void>(llvm::sys::fs::remove(temporary));
    throw codegen_error(std::string(output_error),
                        "Cannot write output '" + path.string() + "': " +
                            write_error.message());
  }
  if (const auto error = llvm::sys::fs::rename(temporary, path.string())) {
    static_cast<void>(llvm::sys::fs::remove(temporary));
    throw codegen_error(std::string(output_error),
                        "Cannot publish output '" + path.string() + "': " +
                            error.message());
  }
}

} // namespace neri::codegen
