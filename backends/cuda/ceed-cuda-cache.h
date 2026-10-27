// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <ceed/backend.h>

#include <string>
#include <vector>

enum class CeedCudaCacheRead { missing, valid, invalid };
enum class CeedCudaCacheWrite { stored, existing, failed, oversized };
CEED_VISIBILITY(hidden) bool CeedCudaCacheEnabled() noexcept;
CEED_VISIBILITY(hidden) std::string CeedCudaCacheCompiler() noexcept;
CEED_VISIBILITY(hidden)
std::string CeedCudaCacheKey(const std::string &code, const std::string &compiler, int num_headers, const char *const *names,
                             const char *const *sources, int num_opts, const char *const *opts) noexcept;
CEED_VISIBILITY(hidden) CeedCudaCacheRead CeedCudaCacheLoad(const std::string &key, std::vector<char> &binary) noexcept;
CEED_VISIBILITY(hidden) CeedCudaCacheWrite CeedCudaCacheStore(const std::string &key, const char *binary, size_t size) noexcept;
CEED_VISIBILITY(hidden) void CeedCudaCacheDiscard(const std::string &key) noexcept;
