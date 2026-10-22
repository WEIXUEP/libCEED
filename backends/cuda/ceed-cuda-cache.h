// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#ifdef CEED_EMBEDDED_JIT
#include <string>
#include <vector>

std::string CeedCudaCacheKey(const std::string &code, const std::string &compiler, int num_headers, const char *const *names,
                             const char *const *sources, int num_opts, const char *const *opts);
bool        CeedCudaCacheLoad(const std::string &key, std::vector<char> &binary);
void        CeedCudaCacheStore(const std::string &key, const char *binary, size_t size);
#endif
