#ifndef NERI_PLATFORM_SESSION_LOADER_H
#define NERI_PLATFORM_SESSION_LOADER_H

#include <string>

namespace neri::platform {
void *session_module_open(const std::string &path, std::string &error);
void *session_module_symbol(void *module, const char *name);
bool session_module_close(void *module, std::string &error);
}

#endif
