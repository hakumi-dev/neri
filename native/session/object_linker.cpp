#include "neri/session_object_linker.h"

#include <llvm/ExecutionEngine/Orc/EHFrameRegistrationPlugin.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/SelfExecutorProcessControl.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

struct neri_session_linker_v1 final {
  std::unique_ptr<llvm::orc::ExecutionSession> session;
  std::unique_ptr<llvm::orc::ObjectLinkingLayer> objects;
  llvm::orc::JITDylib *process{};
  char global_prefix{};
  std::mutex mutex;

  ~neri_session_linker_v1() {
    if (session)
      llvm::consumeError(session->endSession());
    objects.reset();
  }
};

enum class generation_phase { live, resources_removed };

struct neri_session_generation_v1 final {
  neri_session_linker_v1 *owner{};
  llvm::orc::JITDylib *dylib{};
  llvm::orc::ResourceTrackerSP resources;
  generation_phase phase = generation_phase::live;
};

namespace {
void copy_error(char *output, size_t size, std::string message) {
  if (output == nullptr || size == 0)
    return;
  const auto count = std::min(size - 1, message.size());
  std::memcpy(output, message.data(), count);
  output[count] = '\0';
}

template <typename T>
T fail(char *output, size_t size, llvm::Error error, T result) {
  copy_error(output, size, llvm::toString(std::move(error)));
  return result;
}
} // namespace

extern "C" neri_session_linker_v1 *
neri_session_linker_create_v1(char *error, size_t error_size) {
  auto process_control = llvm::orc::SelfExecutorProcessControl::Create();
  if (!process_control)
    return fail(error, error_size, process_control.takeError(),
                static_cast<neri_session_linker_v1 *>(nullptr));
  auto result = std::make_unique<neri_session_linker_v1>();
  result->session = std::make_unique<llvm::orc::ExecutionSession>(
      std::move(*process_control));
  result->objects =
      std::make_unique<llvm::orc::ObjectLinkingLayer>(*result->session);
  auto eh_frames =
      llvm::orc::EHFrameRegistrationPlugin::Create(*result->session);
  if (!eh_frames)
    return fail(error, error_size, eh_frames.takeError(),
                static_cast<neri_session_linker_v1 *>(nullptr));
  result->objects->addPlugin(std::move(*eh_frames));
  result->global_prefix =
      result->session->getTargetTriple().isOSBinFormatMachO() ? '_' : '\0';
  result->process =
      &result->session->createBareJITDylib("neri-session-process");
  auto generator =
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
          result->global_prefix);
  if (!generator)
    return fail(error, error_size, generator.takeError(),
                static_cast<neri_session_linker_v1 *>(nullptr));
  result->process->addGenerator(std::move(*generator));
  return result.release();
}

extern "C" void neri_session_linker_destroy_v1(neri_session_linker_v1 *linker) {
  delete linker;
}

extern "C" neri_session_generation_v1 *neri_session_linker_add_object_v1(
    neri_session_linker_v1 *linker, const char *generation_name,
    const char *object_path, neri_session_generation_v1 *const *dependencies,
    size_t dependency_count, char *error, size_t error_size) {
  if (linker == nullptr || generation_name == nullptr ||
      object_path == nullptr) {
    copy_error(error, error_size, "invalid object linker arguments");
    return nullptr;
  }
  std::lock_guard lock(linker->mutex);
  if (dependency_count != 0 && dependencies == nullptr) {
    copy_error(error, error_size, "missing object linker dependencies");
    return nullptr;
  }
  for (size_t index = 0; index < dependency_count; ++index) {
    auto *dependency = dependencies[index];
    if (dependency == nullptr || dependency->owner != linker ||
        dependency->phase != generation_phase::live) {
      copy_error(error, error_size, "invalid object linker dependency");
      return nullptr;
    }
  }
  auto buffer = llvm::MemoryBuffer::getFile(object_path);
  if (!buffer)
    return fail(error, error_size, llvm::errorCodeToError(buffer.getError()),
                static_cast<neri_session_generation_v1 *>(nullptr));
  if (linker->session->getJITDylibByName(generation_name) != nullptr) {
    copy_error(error, error_size, "duplicate object linker generation");
    return nullptr;
  }
  auto &dylib = linker->session->createBareJITDylib(generation_name);
  for (size_t index = 0; index < dependency_count; ++index) {
    auto *dependency = dependencies[index];
    dylib.addToLinkOrder(*dependency->dylib,
                         llvm::orc::JITDylibLookupFlags::MatchAllSymbols);
  }
  dylib.addToLinkOrder(*linker->process,
                       llvm::orc::JITDylibLookupFlags::MatchAllSymbols);
  auto result = std::make_unique<neri_session_generation_v1>();
  result->owner = linker;
  result->dylib = &dylib;
  result->resources = dylib.createResourceTracker();
  if (auto added =
          linker->objects->add(result->resources, std::move(*buffer))) {
    static_cast<void>(linker->session->removeJITDylib(dylib));
    return fail(error, error_size, std::move(added),
                static_cast<neri_session_generation_v1 *>(nullptr));
  }
  return result.release();
}

extern "C" void *
neri_session_linker_symbol_v1(neri_session_generation_v1 *generation,
                              const char *name, char *error,
                              size_t error_size) {
  if (generation == nullptr || name == nullptr) {
    copy_error(error, error_size, "invalid object linker symbol lookup");
    return nullptr;
  }
  std::lock_guard lock(generation->owner->mutex);
  if (generation->phase != generation_phase::live) {
    copy_error(error, error_size, "invalid object linker symbol lookup");
    return nullptr;
  }
  std::string mangled;
  if (generation->owner->global_prefix != '\0')
    mangled.push_back(generation->owner->global_prefix);
  mangled += name;
  auto symbol = generation->owner->session->lookup(
      {generation->dylib}, generation->owner->session->intern(mangled));
  if (!symbol)
    return fail(error, error_size, symbol.takeError(),
                static_cast<void *>(nullptr));
  return symbol->toPtr<void *>();
}

extern "C" int
neri_session_linker_remove_v1(neri_session_generation_v1 *generation,
                              char *error, size_t error_size) {
  if (generation == nullptr)
    return 0;
  std::lock_guard lock(generation->owner->mutex);
  if (generation->phase == generation_phase::live) {
    if (auto removed = generation->resources->remove())
      return fail(error, error_size, std::move(removed), 0);
    generation->phase = generation_phase::resources_removed;
  }
  // Failed dylib removal can be retried, but its removed resources can no
  // longer satisfy lookups or dependencies. Success consumes the handle.
  if (auto removed =
          generation->owner->session->removeJITDylib(*generation->dylib))
    return fail(error, error_size, std::move(removed), 0);
  delete generation;
  return 1;
}
