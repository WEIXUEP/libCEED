#include <ceed.h>
#include <ceed/backend.h>
#include <ceed/jit-tools.h>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  Ceed       ceed;
  CeedVector vector;
  std::vector<CeedScalar> values(4, 3);

  CeedCall(CeedInit(argv[1], &ceed));
  CeedCall(CeedVectorCreate(ceed, values.size(), &vector));
  CeedCall(CeedVectorSetArray(vector, CEED_MEM_HOST, CEED_COPY_VALUES, values.data()));
  CeedCall(CeedVectorScale(vector, 2));
  const CeedScalar *array;
  CeedCall(CeedVectorGetArrayRead(vector, CEED_MEM_HOST, &array));
  for (size_t i = 0; i < values.size(); i++) {
    if (array[i] != 6) return 1;
  }
  CeedCall(CeedVectorRestoreArrayRead(vector, &array));
#if !defined(CEED_DISABLE_JIT_SOURCE) || !CEED_DISABLE_JIT_SOURCE
  const char *path;
  CeedCall(CeedGetJitAbsolutePath(ceed, "ceed/jit-source/gallery/ceed-massapply.h", &path));
  if (CEED_TEST_INSTALL_ROOT[0] && std::string(path).find(CEED_TEST_INSTALL_ROOT) != 0) {
    std::fprintf(stderr, "JIT did not use the installed headers: %s\n", path);
    return 1;
  }
  CeedCall(CeedFree(&path));
#endif
  CeedCall(CeedVectorDestroy(&vector));
  CeedCall(CeedDestroy(&ceed));
  return 0;
}
