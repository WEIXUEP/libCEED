// Copyright (c) 2017-2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
//
// SPDX-License-Identifier: BSD-2-Clause
//
// This file is part of CEED:  http://github.com/ceed

/**
  @brief Ceed QFunction for building the geometric data for the 3D mass matrix
**/
#include <ceed/types.h>

CEED_QFUNCTION(Mass3DBuild)(void *ctx, const CeedInt Q, const CeedScalar *const *in, CeedScalar *const *out) {
  // in[0] is Jacobians with shape [2, nc=3, Q]
  // in[1] is quadrature weights, size (Q)
  const CeedScalar *J = in[0], *w = in[1];
  // out[0] is quadrature data, size (Q)
  CeedScalar *q_data = out[0];

  // Quadrature point loop
  CeedPragmaSIMD for (CeedInt i = 0; i < Q; i++) {
    q_data[i] = (J[((0) * 2 + (0)) * Q + i] * (J[((1) * 2 + (1)) * Q + i] * J[((2) * 2 + (2)) * Q + i] - J[((1) * 2 + (2)) * Q + i] * J[((2) * 2 + (1)) * Q + i]) - J[((0) * 2 + (1)) * Q + i] * (J[((1) * 2 + (0)) * Q + i] * J[((2) * 2 + (2)) * Q + i] - J[((1) * 2 + (2)) * Q + i] * J[((2) * 2 + (0)) * Q + i]) +
                 J[((0) * 2 + (2)) * Q + i] * (J[((1) * 2 + (0)) * Q + i] * J[((2) * 2 + (1)) * Q + i] - J[((1) * 2 + (1)) * Q + i] * J[((2) * 2 + (0)) * Q + i])) *
                w[i];
  }  // End of Quadrature Point Loop
  return CEED_ERROR_SUCCESS;
}
