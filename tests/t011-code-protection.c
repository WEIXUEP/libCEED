/// @file
/// Test diagnostic source protection and gallery QFunction evaluation
/// \test Test diagnostic source protection and gallery QFunction evaluation
#include <ceed.h>
#include <ceed/backend.h>
#include <ceed/jit-tools.h>
#include <stdio.h>
#include <string.h>

static int num_errors = 0;

static int CheckLocation(Ceed ceed, const char *filename, int line, const char *func, int code, const char *format, va_list *args) {
  num_errors++;
#if defined(CEED_CODE_PROTECTION) && CEED_CODE_PROTECTION
  if (strcmp(filename, "<protected>") || line != 0) printf("Source location was not protected\n");
#else
  if (!strcmp(filename, "<protected>") || line <= 0) printf("Source location was unexpectedly protected\n");
#endif
  return code;
}

int main(int argc, char **argv) {
  Ceed                ceed;
  CeedVector          input, output, qdata, invalid = NULL;
  CeedQFunction       qf;
  CeedOperator        op;
  CeedElemRestriction restriction;
  CeedBasis           basis;
  const CeedInt       strides[3] = {1, 2, 2};
  const CeedScalar   *values;

  CeedCall(CeedInit(argv[1], &ceed));
  CeedCall(CeedSetErrorHandler(ceed, CheckLocation));
  if (CeedError(ceed, CEED_ERROR_MINOR, "Test diagnostic") != CEED_ERROR_MINOR) printf("Error return code changed\n");
  if (CeedVectorCreate(ceed, -1, &invalid) != CEED_ERROR_UNSUPPORTED) printf("Library error return code changed\n");
  if (num_errors != 2) printf("Expected two diagnostic callbacks\n");
  CeedCall(CeedSetErrorHandler(ceed, CeedErrorReturn));

  // CPU-only protection omits JIT locations; GPU builds must retain them.
  CeedCall(CeedQFunctionCreateInteriorByName(ceed, "MassApply", &qf));
  {
    const char  *source_path;
    const char **roots;
    CeedInt      num_roots;

    CeedCall(CeedQFunctionGetSourcePath(qf, &source_path));
#if defined(CEED_DISABLE_JIT_SOURCE) && CEED_DISABLE_JIT_SOURCE
    if (source_path) printf("CPU-only gallery source path was not removed\n");
#else
    if (!source_path) printf("Gallery JIT source path was unexpectedly removed\n");
#endif
    CeedCall(CeedGetJitSourceRoots(ceed, &num_roots, &roots));
#if defined(CEED_DISABLE_JIT_SOURCE) && CEED_DISABLE_JIT_SOURCE
    if (num_roots) printf("CPU-only default JIT source root was not removed\n");
    {
      char *buffer = NULL;

      if (CeedLoadSourceToBuffer(ceed, "unused", &buffer) != CEED_ERROR_UNSUPPORTED) printf("CPU-only JIT source loading was not disabled\n");
      CeedCall(CeedFree(&buffer));
    }
#else
    if (!num_roots) printf("Default JIT source root was unexpectedly removed\n");
#endif
    CeedCall(CeedRestoreJitSourceRoots(ceed, &roots));
  }
  CeedCall(CeedElemRestrictionCreateStrided(ceed, 2, 2, 1, 4, strides, &restriction));
  CeedCall(CeedBasisCreateTensorH1Lagrange(ceed, 1, 1, 2, 2, CEED_GAUSS_LOBATTO, &basis));
  CeedCall(CeedVectorCreate(ceed, 4, &input));
  CeedCall(CeedVectorCreate(ceed, 4, &output));
  CeedCall(CeedVectorCreate(ceed, 4, &qdata));
  CeedCall(CeedVectorSetValue(input, 3));
  CeedCall(CeedVectorSetValue(output, 0));
  CeedCall(CeedVectorSetValue(qdata, 2));
  CeedCall(CeedOperatorCreate(ceed, qf, CEED_QFUNCTION_NONE, CEED_QFUNCTION_NONE, &op));
  CeedCall(CeedOperatorSetField(op, "u", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetField(op, "qdata", restriction, CEED_BASIS_NONE, qdata));
  CeedCall(CeedOperatorSetField(op, "v", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorApply(op, input, output, CEED_REQUEST_IMMEDIATE));
  CeedCall(CeedVectorGetArrayRead(output, CEED_MEM_HOST, &values));
  for (CeedInt i = 0; i < 4; i++) {
    if (values[i] != 6) printf("Gallery QFunction result changed\n");
  }
  CeedCall(CeedVectorRestoreArrayRead(output, &values));
  CeedCall(CeedVectorDestroy(&input));
  CeedCall(CeedVectorDestroy(&output));
  CeedCall(CeedVectorDestroy(&qdata));
  CeedCall(CeedOperatorDestroy(&op));
  CeedCall(CeedElemRestrictionDestroy(&restriction));
  CeedCall(CeedBasisDestroy(&basis));
  CeedCall(CeedQFunctionDestroy(&qf));
  CeedCall(CeedDestroy(&ceed));
  return 0;
}
