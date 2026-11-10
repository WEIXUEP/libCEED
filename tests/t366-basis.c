/// @file
/// Test non-tensor basis kernels and resource fallback through backend-independent operator layouts
/// \test Test non-tensor basis kernels and resource fallback through backend-independent operator layouts
#include <ceed.h>
#include <ceed/backend.h>
#include <math.h>
#include <stdio.h>

#include "t366-basis.h"

int main(int argc, char **argv) {
  Ceed          ceed;
  const CeedInt sizes[2] = {512, 87000};
  CeedScalar    interp[24], grad[72], q_ref[72] = {0}, weights[24];
  CeedBasis     basis;
  int           failed = 0;

  CeedCall(CeedInit(argv[1], &ceed));
  for (CeedInt j = 0; j < 24; j++) {
    interp[j]  = 0.5 + 0.01 * j;
    weights[j] = 1.0 / 24;
    for (CeedInt d = 0; d < 3; d++) grad[d * 24 + j] = 0.02 * (d + 1) * (j + 1);
  }
  CeedCall(CeedBasisCreateH1(ceed, CEED_TOPOLOGY_TET, 2, 1, 24, interp, grad, q_ref, weights, &basis));
  for (CeedInt batch = 0; batch < 2; batch++) {
    const CeedInt num_elem = sizes[batch], n = 2 * num_elem;
    const CeedInt strides[3] = {1, num_elem, 1}, strides_q[3] = {1, 24 * num_elem, 24};

    for (CeedInt derivative = 0; derivative < 2; derivative++) {
      const CeedEvalMode   mode   = derivative ? CEED_EVAL_GRAD : CEED_EVAL_INTERP;
      const CeedInt        q_comp = derivative ? 3 : 1;
      const CeedScalar    *tab    = derivative ? grad : interp;
      CeedElemRestriction  restriction, restriction_q;
      CeedQFunction        qf, qf_t;
      CeedQFunctionContext context;
      const CeedInt        field_size = 2 * q_comp;
      CeedOperator         op, op_t;
      CeedVector           u, v;
      CeedScalar          *write;
      const CeedScalar    *values;
      CeedScalar           error = 0;

      // Identity QFunctions expose basis products in an explicit L-vector layout,
      // so the test also works with CPU and GPU backends' different E-vector packing.
      CeedCall(CeedElemRestrictionCreateStrided(ceed, num_elem, 1, 2, n, strides, &restriction));
      CeedCall(CeedElemRestrictionCreateStrided(ceed, num_elem, 24, 2 * q_comp, n * 24 * q_comp, strides_q, &restriction_q));
      CeedCall(CeedQFunctionCreateIdentity(ceed, 2 * q_comp, mode, CEED_EVAL_NONE, &qf));
      CeedCall(CeedQFunctionCreateInterior(ceed, 1, Duplicate, Duplicate_loc, &qf_t));
      CeedCall(CeedQFunctionContextCreate(ceed, &context));
      CeedCall(CeedQFunctionContextSetData(context, CEED_MEM_HOST, CEED_COPY_VALUES, sizeof(field_size), (void *)&field_size));
      CeedCall(CeedQFunctionSetContext(qf_t, context));
      CeedCall(CeedQFunctionAddInput(qf_t, "input", field_size, CEED_EVAL_NONE));
      CeedCall(CeedQFunctionAddOutput(qf_t, "first", field_size, mode));
      CeedCall(CeedQFunctionAddOutput(qf_t, "second", field_size, mode));
      CeedCall(CeedOperatorCreate(ceed, qf, CEED_QFUNCTION_NONE, CEED_QFUNCTION_NONE, &op));
      CeedCall(CeedOperatorSetField(op, "input", restriction, basis, CEED_VECTOR_ACTIVE));
      CeedCall(CeedOperatorSetField(op, "output", restriction_q, CEED_BASIS_NONE, CEED_VECTOR_ACTIVE));
      CeedCall(CeedOperatorCreate(ceed, qf_t, CEED_QFUNCTION_NONE, CEED_QFUNCTION_NONE, &op_t));
      CeedCall(CeedOperatorSetField(op_t, "input", restriction_q, CEED_BASIS_NONE, CEED_VECTOR_ACTIVE));
      // Two fields sharing one active output force both BasisApply and
      // BasisApplyAdd, including the GEMM fallback's accumulation branch.
      CeedCall(CeedOperatorSetField(op_t, "first", restriction, basis, CEED_VECTOR_ACTIVE));
      CeedCall(CeedOperatorSetField(op_t, "second", restriction, basis, CEED_VECTOR_ACTIVE));
      CeedCall(CeedVectorCreate(ceed, n, &u));
      CeedCall(CeedVectorCreate(ceed, n * 24 * q_comp, &v));
      CeedCall(CeedVectorGetArrayWrite(u, CEED_MEM_HOST, &write));
      for (CeedInt e = 0; e < n; e++) write[e] = 0.1 + 0.01 * (e % 17);
      CeedCall(CeedVectorRestoreArray(u, &write));
      CeedCall(CeedOperatorApply(op, u, v, CEED_REQUEST_IMMEDIATE));
      CeedCall(CeedVectorGetArrayRead(v, CEED_MEM_HOST, &values));
      for (CeedInt d = 0; d < q_comp; d++) {
        for (CeedInt e = 0; e < n; e++) {
          for (CeedInt j = 0; j < 24; j++) {
            const CeedScalar diff = fabs(values[(d * n + e) * 24 + j] - tab[d * 24 + j] * (0.1 + 0.01 * (e % 17)));

            if (diff > error) error = diff;
          }
        }
      }
      CeedCall(CeedVectorRestoreArrayRead(v, &values));
      if (error > 500 * CEED_EPSILON) {
        printf("Non-tensor forward, batch %d, derivative %d: error %g\n", num_elem, derivative, (double)error);
        failed = 1;
      }
      CeedCall(CeedVectorGetArrayWrite(v, CEED_MEM_HOST, &write));
      for (CeedInt k = 0; k < n * 24 * q_comp; k++) write[k] = 0.2 + 0.005 * (k % 23);
      CeedCall(CeedVectorRestoreArray(v, &write));
      for (CeedInt add = 0; add < 2; add++) {
        error = 0;
        CeedCall(CeedVectorSetValue(u, 0.25));
        if (add) CeedCall(CeedOperatorApplyAdd(op_t, v, u, CEED_REQUEST_IMMEDIATE));
        else CeedCall(CeedOperatorApply(op_t, v, u, CEED_REQUEST_IMMEDIATE));
        CeedCall(CeedVectorGetArrayRead(u, CEED_MEM_HOST, &values));
        for (CeedInt e = 0; e < n; e++) {
          CeedScalar expected = add ? 0.25 : 0;

          for (CeedInt d = 0; d < q_comp; d++) {
            for (CeedInt j = 0; j < 24; j++) expected += 2 * tab[d * 24 + j] * (0.2 + 0.005 * (((d * n + e) * 24 + j) % 23));
          }
          if (fabs(values[e] - expected) > error) error = fabs(values[e] - expected);
        }
        CeedCall(CeedVectorRestoreArrayRead(u, &values));
        if (error > 5000 * CEED_EPSILON) {
          printf("Non-tensor transpose, batch %d, derivative %d, add %d: error %g\n", num_elem, derivative, add, (double)error);
          failed = 1;
        }
      }
      CeedCall(CeedVectorDestroy(&u));
      CeedCall(CeedVectorDestroy(&v));
      CeedCall(CeedOperatorDestroy(&op));
      CeedCall(CeedOperatorDestroy(&op_t));
      CeedCall(CeedQFunctionDestroy(&qf));
      CeedCall(CeedQFunctionDestroy(&qf_t));
      CeedCall(CeedQFunctionContextDestroy(&context));
      CeedCall(CeedElemRestrictionDestroy(&restriction));
      CeedCall(CeedElemRestrictionDestroy(&restriction_q));
    }
  }
  CeedCall(CeedBasisDestroy(&basis));
  CeedCall(CeedDestroy(&ceed));
  return failed;
}
