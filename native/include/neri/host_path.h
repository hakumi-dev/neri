#ifndef NERI_HOST_PATH_H
#define NERI_HOST_PATH_H
#include <filesystem>
#include <string>
#include <string_view>
namespace neri {
inline std::filesystem::path host_path(std::string_view value) {
  return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
inline std::string path_text(const std::filesystem::path &value) {
  const auto text = value.generic_u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}
}
#endif
