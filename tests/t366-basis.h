// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
// SPDX-License-Identifier: BSD-2-Clause

#include <ceed/types.h>

CEED_QFUNCTION(Duplicate)(void *ctx, const CeedInt Q, const CeedScalar *const *in, CeedScalar *const *out) {
  const CeedInt size = *(const CeedInt *)ctx;

  for (CeedInt i = 0; i < size * Q; i++) out[0][i] = out[1][i] = in[0][i];
  return 0;
}
