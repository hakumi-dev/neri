#ifndef NERI_PLATFORM_ERROR_TEXT_H
#define NERI_PLATFORM_ERROR_TEXT_H

#include <errno.h>
#include <stddef.h>
#include <string.h>

/* Copies into caller-owned storage instead of borrowing strerror's storage. */
static inline size_t neri_platform_error_text_length(const char *text, size_t capacity) {
  size_t length = 0;
  while (length < capacity && text[length] != '\0') ++length;
  return length;
}

static inline size_t neri_platform_error_text(int error, char *buffer, size_t capacity) {
  if (buffer == NULL || capacity == 0) return 0;

  const int caller_errno = errno;
  buffer[0] = '\0';
#if defined(_WIN32)
  if (strerror_s(buffer, capacity, error) != 0) {
    const char fallback[] = "Unknown error";
    const size_t length = sizeof(fallback) - 1 < capacity - 1 ? sizeof(fallback) - 1 : capacity - 1;
    memcpy(buffer, fallback, length);
    buffer[length] = '\0';
  }
#elif defined(__GLIBC__) && defined(_GNU_SOURCE)
  char *result = strerror_r(error, buffer, capacity);
  if (result == NULL) {
    const char fallback[] = "Unknown error";
    const size_t length = sizeof(fallback) - 1 < capacity - 1 ? sizeof(fallback) - 1 : capacity - 1;
    memcpy(buffer, fallback, length);
    buffer[length] = '\0';
  } else if (result != buffer) {
    const size_t length = neri_platform_error_text_length(result, capacity - 1);
    memmove(buffer, result, length);
    buffer[length] = '\0';
  }
#else
  if (strerror_r(error, buffer, capacity) != 0) {
    const char fallback[] = "Unknown error";
    const size_t length = sizeof(fallback) - 1 < capacity - 1 ? sizeof(fallback) - 1 : capacity - 1;
    memcpy(buffer, fallback, length);
    buffer[length] = '\0';
  }
#endif
  const size_t length = neri_platform_error_text_length(buffer, capacity);
  errno = caller_errno;
  return length;
}

#endif
