#include "session_loader.h"
#include "neri/host_path.h"

#if defined(_WIN32)
#include "windows_support.h"
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace neri::platform {
void *session_module_open(const std::string &path, std::string &error) {
#if defined(_WIN32)
  auto *result = reinterpret_cast<void *>(LoadLibraryW(windows::wide(path).c_str()));
  if (result == nullptr) error = "LoadLibrary failed";
#else
  auto *result = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (result == nullptr) error = dlerror();
#endif
  return result;
}

void *session_module_symbol(void *module, const char *name) {
#if defined(_WIN32)
  return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(module), name));
#else
  return dlsym(module, name);
#endif
}

bool session_module_close(void *module, std::string &error) {
#if defined(_WIN32)
  if (FreeLibrary(reinterpret_cast<HMODULE>(module)) == 0) {
    error = "FreeLibrary failed";
    return false;
  }
#else
  if (dlclose(module) != 0) {
    error = dlerror();
    return false;
  }
#endif
  return true;
}
}
