#include "neri/runtime_abi.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <sys/random.h>
#else
#include <openssl/evp.h>
#include <sys/random.h>
#endif

struct neri_sha256_stream {
#if defined(_WIN32)
  BCRYPT_ALG_HANDLE algorithm;
  BCRYPT_HASH_HANDLE hash;
#elif defined(__APPLE__)
  CC_SHA256_CTX context;
#else
  EVP_MD_CTX *context;
#endif
};

void *neri_rt_v1_crypto_sha256_begin(void) {
  struct neri_sha256_stream *stream = calloc(1, sizeof(*stream));
  if (stream == NULL) return NULL;
#if defined(_WIN32)
  NTSTATUS status = BCryptOpenAlgorithmProvider(
      &stream->algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
  if (BCRYPT_SUCCESS(status))
    status = BCryptCreateHash(stream->algorithm, &stream->hash, NULL, 0,
                              NULL, 0, 0);
  if (!BCRYPT_SUCCESS(status)) {
    if (stream->hash != NULL) BCryptDestroyHash(stream->hash);
    if (stream->algorithm != NULL)
      BCryptCloseAlgorithmProvider(stream->algorithm, 0);
    free(stream);
    return NULL;
  }
#elif defined(__APPLE__)
  if (CC_SHA256_Init(&stream->context) != 1) {
    free(stream);
    return NULL;
  }
#else
  stream->context = EVP_MD_CTX_new();
  if (stream->context == NULL ||
      EVP_DigestInit_ex(stream->context, EVP_sha256(), NULL) != 1) {
    EVP_MD_CTX_free(stream->context);
    free(stream);
    return NULL;
  }
#endif
  return stream;
}

int64_t neri_rt_v1_crypto_sha256_update(void *opaque,
                                         const uint8_t *input,
                                         int64_t length) {
  if (opaque == NULL || length < 0 || (length != 0 && input == NULL)) return -1;
  struct neri_sha256_stream *stream = opaque;
#if defined(_WIN32)
  if ((uint64_t)length > ULONG_MAX) return -1;
  return length == 0 || BCRYPT_SUCCESS(BCryptHashData(
      stream->hash, (PUCHAR)input, (ULONG)length, 0)) ? 0 : -1;
#elif defined(__APPLE__)
  if ((uint64_t)length > UINT32_MAX) return -1;
  return length == 0 || CC_SHA256_Update(&stream->context, input,
                                         (CC_LONG)length) == 1 ? 0 : -1;
#else
  if ((uint64_t)length > SIZE_MAX) return -1;
  return length == 0 || EVP_DigestUpdate(stream->context, input,
                                         (size_t)length) == 1 ? 0 : -1;
#endif
}

int64_t neri_rt_v1_crypto_sha256_finish(void *opaque, uint8_t *digest) {
  if (opaque == NULL || digest == NULL) return -1;
  struct neri_sha256_stream *stream = opaque;
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptFinishHash(stream->hash, digest, 32, 0)) ? 0 : -1;
#elif defined(__APPLE__)
  return CC_SHA256_Final(digest, &stream->context) == 1 ? 0 : -1;
#else
  unsigned int length = 0;
  return EVP_DigestFinal_ex(stream->context, digest, &length) == 1 &&
         length == 32 ? 0 : -1;
#endif
}

int64_t neri_rt_v1_crypto_sha256_dispose(void *opaque) {
  if (opaque == NULL) return -1;
  struct neri_sha256_stream *stream = opaque;
  int64_t result = 0;
#if defined(_WIN32)
  if (!BCRYPT_SUCCESS(BCryptDestroyHash(stream->hash))) result = -1;
  if (!BCRYPT_SUCCESS(BCryptCloseAlgorithmProvider(stream->algorithm, 0)))
    result = -1;
#elif defined(__APPLE__)
  (void)stream;
#else
  EVP_MD_CTX_free(stream->context);
#endif
  free(stream);
  return result;
}

int64_t neri_rt_v1_crypto_sha256(const uint8_t *input, int64_t length,
                                  uint8_t *digest) {
  if (length < 0 || digest == NULL || (length != 0 && input == NULL)) return -1;
#if defined(_WIN32)
  if ((uint64_t)length > ULONG_MAX) return -1;
  BCRYPT_ALG_HANDLE algorithm = NULL;
  BCRYPT_HASH_HANDLE hash = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(
      &algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
  if (BCRYPT_SUCCESS(status))
    status = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0);
  if (BCRYPT_SUCCESS(status) && length != 0)
    status = BCryptHashData(hash, (PUCHAR)input, (ULONG)length, 0);
  if (BCRYPT_SUCCESS(status))
    status = BCryptFinishHash(hash, digest, 32, 0);
  NTSTATUS destroy_status = 0;
  NTSTATUS close_status = 0;
  if (hash != NULL) destroy_status = BCryptDestroyHash(hash);
  if (algorithm != NULL)
    close_status = BCryptCloseAlgorithmProvider(algorithm, 0);
  return BCRYPT_SUCCESS(status) && BCRYPT_SUCCESS(destroy_status) &&
      BCRYPT_SUCCESS(close_status) ? 0 : -1;
#elif defined(__APPLE__)
  if ((uint64_t)length > UINT32_MAX) return -1;
  return CC_SHA256(input, (CC_LONG)length, digest) != NULL ? 0 : -1;
#else
  unsigned int digest_length = 0;
  if ((uint64_t)length > SIZE_MAX) return -1;
  return EVP_Digest(input, (size_t)length, digest, &digest_length,
                    EVP_sha256(), NULL) == 1 && digest_length == 32 ? 0 : -1;
#endif
}

int64_t neri_rt_v1_crypto_random(uint8_t *output, int64_t length) {
  if (length < 0 || length > 256 || (length != 0 && output == NULL)) return -1;
  if (length == 0) return 0;
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(
      NULL, output, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ? 0 : -1;
#elif defined(__APPLE__)
  return getentropy(output, (size_t)length) == 0 ? 0 : -1;
#else
  size_t offset = 0;
  while (offset < (size_t)length) {
    const ssize_t count = getrandom(output + offset, (size_t)length - offset, 0);
    if (count > 0) {
      offset += (size_t)count;
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }
  return 0;
#endif
}
