// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#include <ceed.h>

#ifdef CEED_EMBEDDED_JIT
CEED_EXTERN int CeedEmbeddedJitOpen(Ceed ceed, int *count, const char ***names, const char ***sources, void **handle);
CEED_EXTERN int CeedEmbeddedJitClose(Ceed ceed, void **handle);
CEED_EXTERN int CeedEmbeddedJitFind(Ceed ceed, const char *path, char **source, bool *found);
#endif
