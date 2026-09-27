#ifndef NERI_SESSION_OBJECT_LINKER_H
#define NERI_SESSION_OBJECT_LINKER_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define NERI_SESSION_LINKER_API __declspec(dllexport)
#else
#define NERI_SESSION_LINKER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neri_session_linker_v1 neri_session_linker_v1;
typedef struct neri_session_generation_v1 neri_session_generation_v1;

NERI_SESSION_LINKER_API neri_session_linker_v1 *
neri_session_linker_create_v1(char *error, size_t error_size);
NERI_SESSION_LINKER_API void
neri_session_linker_destroy_v1(neri_session_linker_v1 *linker);
NERI_SESSION_LINKER_API int neri_session_linker_add_library_v1(
    neri_session_linker_v1 *linker, const char *name, const char *directory,
    char *error, size_t error_size);
NERI_SESSION_LINKER_API neri_session_generation_v1 *
neri_session_linker_add_object_v1(
    neri_session_linker_v1 *linker, const char *generation_name,
    const char *object_path, neri_session_generation_v1 *const *dependencies,
    size_t dependency_count, char *error, size_t error_size);
NERI_SESSION_LINKER_API void *
neri_session_linker_symbol_v1(neri_session_generation_v1 *generation,
                              const char *name, char *error,
                              size_t error_size);
NERI_SESSION_LINKER_API int
neri_session_linker_remove_v1(neri_session_generation_v1 *generation,
                              char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
