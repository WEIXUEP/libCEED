/// @file
/// Test QFunction reassembly after operator applications overwrite active work vectors
/// \test Test QFunction reassembly after operator applications overwrite active work vectors
#include <ceed.h>
#include <ceed/backend.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "t527-operator.h"

int main(int argc, char **argv) {
  Ceed                ceed;
  CeedBasis           basis;
  CeedElemRestriction restriction, restriction_q, restriction_assembled = NULL;
  CeedQFunction       qf;
  CeedOperator        op;
  CeedVector          u, v, scale, assembled = NULL;
  CeedScalar         *reference;
  CeedSize            length;
  const CeedInt       strides[3] = {1, 4, 4}, strides_q[3] = {1, 9, 9};
  const CeedScalar    input[8] = {1, 2, 4, 8, 3, 5, 7, 11};
  int                 failed   = 0;

  CeedCall(CeedInit(argv[1], &ceed));
  CeedCall(CeedElemRestrictionCreateStrided(ceed, 2, 4, 1, 8, strides, &restriction));
  CeedCall(CeedElemRestrictionCreateStrided(ceed, 2, 9, 1, 18, strides_q, &restriction_q));
  CeedCall(CeedBasisCreateTensorH1Lagrange(ceed, 2, 1, 2, 3, CEED_GAUSS, &basis));
  CeedCall(CeedVectorCreate(ceed, 8, &u));
  CeedCall(CeedVectorCreate(ceed, 8, &v));
  CeedCall(CeedVectorCreate(ceed, 18, &scale));
  CeedCall(CeedVectorSetArray(u, CEED_MEM_HOST, CEED_COPY_VALUES, (CeedScalar *)input));
  CeedCall(CeedVectorSetValue(scale, 1));
  CeedCall(CeedQFunctionCreateInterior(ceed, 1, CoupledApply, CoupledApply_loc, &qf));
  CeedCall(CeedQFunctionAddInput(qf, "u", 1, CEED_EVAL_INTERP));
  CeedCall(CeedQFunctionAddInput(qf, "du", 2, CEED_EVAL_GRAD));
  CeedCall(CeedQFunctionAddInput(qf, "scale", 1, CEED_EVAL_NONE));
  CeedCall(CeedQFunctionAddOutput(qf, "v", 1, CEED_EVAL_INTERP));
  CeedCall(CeedQFunctionAddOutput(qf, "dv", 2, CEED_EVAL_GRAD));
  CeedCall(CeedOperatorCreate(ceed, qf, CEED_QFUNCTION_NONE, CEED_QFUNCTION_NONE, &op));
  CeedCall(CeedOperatorSetField(op, "u", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetField(op, "du", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetField(op, "scale", restriction_q, CEED_BASIS_NONE, scale));
  CeedCall(CeedOperatorSetField(op, "v", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetField(op, "dv", restriction, basis, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetQFunctionAssemblyReuse(op, true));
  CeedCall(CeedOperatorLinearAssembleQFunctionBuildOrUpdate(op, &assembled, &restriction_assembled, CEED_REQUEST_IMMEDIATE));
  CeedCall(CeedVectorGetLength(assembled, &length));
  reference = (CeedScalar *)malloc(length * sizeof(*reference));
  if (!reference) return 1;
  {
    const CeedScalar *values;

    CeedCall(CeedVectorGetArrayRead(assembled, CEED_MEM_HOST, &values));
    for (CeedSize i = 0; i < length; i++) reference[i] = values[i];
    CeedCall(CeedVectorRestoreArrayRead(assembled, &values));
  }
  // Three active components in two fields: clearing just the previous probe
  // leaves another component contaminated by the intervening operator apply.
  for (CeedInt iteration = 0; iteration < 3; iteration++) {
    const CeedScalar *values;
    const CeedScalar  factor = iteration + 1;

    CeedCall(CeedOperatorApply(op, u, v, CEED_REQUEST_IMMEDIATE));
    CeedCall(CeedVectorSetValue(scale, factor));
    CeedCall(CeedOperatorSetQFunctionAssemblyDataUpdateNeeded(op, true));
    CeedCall(CeedOperatorLinearAssembleQFunctionBuildOrUpdate(op, &assembled, &restriction_assembled, CEED_REQUEST_IMMEDIATE));
    CeedCall(CeedVectorGetArrayRead(assembled, CEED_MEM_HOST, &values));
    for (CeedSize i = 0; i < length; i++) {
      if (fabs(values[i] - factor * reference[i]) > 500 * CEED_EPSILON) {
        printf("QFunction reassembly changed after apply, iteration %d, entry %td: %g != %g\n", iteration, (ptrdiff_t)i, (double)values[i],
               (double)(factor * reference[i]));
        failed = 1;
        break;
      }
    }
    CeedCall(CeedVectorRestoreArrayRead(assembled, &values));
  }
  free(reference);
  CeedCall(CeedVectorDestroy(&assembled));
  CeedCall(CeedVectorDestroy(&scale));
  CeedCall(CeedVectorDestroy(&u));
  CeedCall(CeedVectorDestroy(&v));
  CeedCall(CeedElemRestrictionDestroy(&restriction_assembled));
  CeedCall(CeedElemRestrictionDestroy(&restriction_q));
  CeedCall(CeedElemRestrictionDestroy(&restriction));
  CeedCall(CeedBasisDestroy(&basis));
  CeedCall(CeedQFunctionDestroy(&qf));
  CeedCall(CeedOperatorDestroy(&op));
  CeedCall(CeedDestroy(&ceed));
  return failed;
}
