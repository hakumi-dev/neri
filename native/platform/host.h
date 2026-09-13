#ifndef NERI_PLATFORM_HOST_H
#define NERI_PLATFORM_HOST_H
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Private OS boundary. Managed ownership and ABI validation stay in runtime.cpp.
namespace neri::platform {
void *allocate(size_t size, size_t alignment, bool zeroed);
void deallocate(void *pointer);
double parse_float(const char *text, char **end);
bool write_atomic(std::string_view path, const uint8_t *bytes, size_t size, std::string &error);
bool remove_file(std::string_view path, std::string &error);
std::optional<std::string> environment(std::string_view name);
std::optional<std::string> executable_path();
std::optional<int64_t> run(std::vector<std::string> &arguments, std::string &error);
}
#endif
