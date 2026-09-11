/* Native feasibility boundary: Neri cannot invoke dlsym function pointers or
 * own a runtime root frame outside generated call frames. Submission behavior
 * and verification belong to the Neri fixtures and runner. */
#include "neri/runtime_abi.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef neri_ref_v1 (*submission_fn)(neri_ref_v1);

static int compatible(const neri_runtime_abi_requirements_v1 *required) {
  const neri_runtime_abi_info_v1 *actual = neri_rt_v1_get_abi();
  return required != NULL && required->struct_size == sizeof(*required) &&
         required->major == actual->major &&
         required->minimum_minor <= actual->minor &&
         (required->required_features & ~actual->features) == 0;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: loader first-module second-module entry-symbol\n");
    return 2;
  }
  const neri_runtime_abi_requirements_v1 initial = {
      sizeof(initial), NERI_RUNTIME_ABI_MAJOR, NERI_RUNTIME_ABI_MINOR,
      NERI_RT_FEATURE_PRECISE_GC | NERI_RT_FEATURE_ROOT_FRAMES};
  if (neri_rt_v1_initialize(&initial) != NERI_ABI_STATUS_OK_V1) {
    fprintf(stderr, "runtime initialization failed\n");
    return 1;
  }
  neri_ref_v1 state = NULL;
  neri_gc_root_frame_v1 roots = {NULL, &state, 1, 0};
  neri_rt_v1_gc_root_frame_enter(&roots);
  void *modules[2] = {NULL, NULL};
  int result = 0;
  for (int index = 0; index < 2; ++index) {
    modules[index] = dlopen(argv[index + 1], RTLD_NOW | RTLD_LOCAL);
    if (modules[index] == NULL) {
      fprintf(stderr, "load failed: %s\n", dlerror());
      result = 1;
      break;
    }
    const neri_runtime_abi_requirements_v1 *required =
        dlsym(modules[index], "neri_program_v1_abi_requirements");
    if (!compatible(required)) {
      fprintf(stderr, "incompatible submission runtime requirements\n");
      result = 1;
      break;
    }
    void *symbol = dlsym(modules[index], argv[3]);
    if (symbol == NULL) {
      fprintf(stderr, "entry lookup failed: %s\n", dlerror());
      result = 1;
      break;
    }
    submission_fn invoke = NULL;
    _Static_assert(sizeof(invoke) == sizeof(symbol), "POSIX function pointer ABI");
    memcpy(&invoke, &symbol, sizeof(invoke));
    /* This fixed fixture has a compiler-checked State? -> State entry. A
     * production loader must obtain the signature from verified compiler IR. */
    state = invoke(state);
    neri_rt_v1_gc_collect();
  }
  state = NULL;
  neri_rt_v1_gc_collect();
  neri_gc_stats_v1 stats = {sizeof(stats), 0, 0, 0, 0, 0};
  neri_rt_v1_gc_get_stats(&stats);
  printf("reset: %llu managed objects\n",
         (unsigned long long)stats.managed_object_count);
  neri_rt_v1_gc_root_frame_leave(&roots);
  /* Descriptors, trace functions, literals and closures reside in the modules.
   * Destroy the heap before releasing any of their code. */
  neri_rt_v1_shutdown();
  for (int index = 1; index >= 0; --index) {
    if (modules[index] != NULL && dlclose(modules[index]) != 0) {
      fprintf(stderr, "unload failed: %s\n", dlerror());
      result = 1;
    }
  }
  return result;
}
