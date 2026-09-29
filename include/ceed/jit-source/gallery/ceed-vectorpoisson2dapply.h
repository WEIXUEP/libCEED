// Copyright (c) 2017-2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
//
// SPDX-License-Identifier: BSD-2-Clause
//
// This file is part of CEED:  http://github.com/ceed

/**
  @brief Ceed QFunction for applying the 2D Poisson operator on a vector system with three components
**/
#include <ceed/types.h>

CEED_QFUNCTION(Vector3Poisson2DApply)(void *ctx, const CeedInt Q, const CeedScalar *const *in, CeedScalar *const *out) {
  // in[0] is gradient u, shape [2, nc=3, Q]
  // in[1] is quadrature data, size (3*Q)
  const CeedScalar *ug = in[0], *q_data = in[1];
  // out[0] is output to multiply against gradient v, shape [2, nc=3, Q]
  CeedScalar *vg = out[0];

  const CeedInt dim = 2, num_comp = 3;

  // Quadrature point loop
  CeedPragmaSIMD for (CeedInt i = 0; i < Q; i++) {
    // Read qdata (dXdxdXdxT symmetric matrix)
    // Stored in Voigt convention
    // 0 2
    // 2 1
    const CeedScalar dXdxdXdxT[2][2] = {
        {q_data[(0) * Q + i], q_data[(2) * Q + i]},
        {q_data[(2) * Q + i], q_data[(1) * Q + i]}
    };

    // Apply Poisson operator
    // j = direction of vg
    for (CeedInt j = 0; j < dim; j++)
      for (CeedInt c = 0; c < num_comp; c++) vg[(j * num_comp + c) * Q + i] = (ug[(0 * num_comp + c) * Q + i] * dXdxdXdxT[0][j] + ug[(1 * num_comp + c) * Q + i] * dXdxdXdxT[1][j]);
  }  // End of Quadrature Point Loop
  return CEED_ERROR_SUCCESS;
}
