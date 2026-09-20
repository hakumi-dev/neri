#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_exports.h"
#include "other_exports.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

/* Independent C declarations check the emitted ABI, including uint8_t Bool.
 * The host starts without initializing Neri and owns all callback storage. */
typedef int32_t (*operation)(int32_t);
typedef struct callback_record {
  uint8_t tag;
  operation callback;
  operation optional;
} callback_record;

extern int32_t app_increment(int32_t);
extern int32_t app_roundtrip(void);
extern operation app_choose(uint8_t);
extern int32_t app_optional(operation, int32_t);
extern double app_mixed(uint8_t, uint8_t, int32_t, uint32_t, uint64_t,
                        float, double, int32_t *);
extern int32_t app_divide(int32_t);
extern int32_t app_other(int32_t);

#define CHECK(value) do { if (!(value)) abort(); } while (0)

int32_t oracle_twice(int32_t value) { return value * 2; }
operation oracle_callback(void) { return oracle_twice; }
int32_t oracle_apply(operation callback, int32_t value) { return callback(value); }
void oracle_record_fill(callback_record *record) {
  record->tag = 7;
  record->callback = oracle_twice;
  record->optional = NULL;
}
uint8_t oracle_record_check(callback_record *record) {
  return record->tag == 7 && record->callback(41) == 42 &&
         record->optional != NULL && record->optional(21) == 42;
}

#if defined(_WIN32)
static DWORD WINAPI worker(LPVOID context) {
#else
static void *worker(void *context) {
#endif
  (void)context;
  for (unsigned count = 0; count < 16; ++count) {
    CHECK(app_roundtrip() == 42);
  }
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  if (argc != 1) return app_divide(0);
  int32_t output = 0;
  const double result = app_mixed(UINT8_MAX, 1, INT32_MIN, UINT32_MAX,
                                  UINT64_C(9223372036854775808),
                                  1.25f, 2.5, &output);

  CHECK(result == 259.75 && output == INT32_MIN);
  CHECK(app_roundtrip() == 42);
  CHECK(app_other(25) == 42);
  CHECK(app_choose(0) == NULL);
  CHECK(app_choose(1) == app_increment);
  CHECK(app_choose(128) == app_increment);
  CHECK(app_choose(1)(41) == 42);
  CHECK(app_optional(NULL, 21) == -1);
  CHECK(app_optional(oracle_twice, 21) == 42);

  void *storage = malloc(sizeof(callback_record));

  CHECK(storage != NULL);
  CHECK(app_record((uint8_t *)storage)->neri_field_callback(41) == 42);

  callback_record record;
  memcpy(&record, storage, sizeof(record));
  free(storage);

  CHECK(record.tag == 7 && record.callback == app_increment && record.optional == NULL);

#if defined(_WIN32)
  HANDLE threads[4];
  for (unsigned index = 0; index < 4; ++index) {
    threads[index] = CreateThread(NULL, 0, worker, NULL, 0, NULL);

    CHECK(threads[index] != NULL);
  }

  CHECK(WaitForMultipleObjects(4, threads, TRUE, INFINITE) == WAIT_OBJECT_0);

  for (unsigned index = 0; index < 4; ++index) {
    CHECK(CloseHandle(threads[index]));
  }
#else
  pthread_t threads[4];
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(pthread_create(&threads[index], NULL, worker, NULL) == 0);
  }
  for (unsigned index = 0; index < 4; ++index) {
    CHECK(pthread_join(threads[index], NULL) == 0);
  }
#endif
  return 0;
}
