#include "neri/runtime_abi.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>

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
