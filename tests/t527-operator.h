// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
// SPDX-License-Identifier: BSD-2-Clause

#include <ceed/types.h>

CEED_QFUNCTION(CoupledApply)(void *ctx, const CeedInt Q, const CeedScalar *const *in, CeedScalar *const *out) {
  const CeedScalar *u = in[0], *du = in[1], *scale = in[2];
  CeedScalar       *v = out[0], *dv = out[1];

  for (CeedInt i = 0; i < Q; i++) {
    v[i]      = scale[i] * (2 * u[i] + 3 * du[i] + 5 * du[i + Q]);
    dv[i]     = scale[i] * (7 * u[i] + 11 * du[i] + 13 * du[i + Q]);
    dv[i + Q] = scale[i] * (17 * u[i] + 19 * du[i] + 23 * du[i + Q]);
  }
  return 0;
}
