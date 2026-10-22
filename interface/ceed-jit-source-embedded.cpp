// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#ifdef CEED_EMBEDDED_JIT
#include "ceed-jit-source-embedded.h"
#include "ceed-jit-crypto.h"
#include "ceed-jit-resources.h"

#include <ceed/backend.h>

#include <cstring>
#include <memory>
#include <string>

namespace {
struct Bundle {
  std::vector<unsigned char> bytes;
  std::vector<const char *>  names, sources;
  ~Bundle() {
    if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
  }
  uint32_t ReadSize(size_t &offset) {
    if (bytes.size() - offset < 4) throw std::runtime_error("Invalid JIT resource bundle");
    uint32_t size = 0;
    for (int shift = 0; shift < 32; shift += 8) size |= static_cast<uint32_t>(bytes[offset++]) << shift;
    return size;
  }
  const char *ReadString(size_t &offset, uint32_t size) {
    if (static_cast<size_t>(size) >= bytes.size() - offset || bytes[offset + size] != 0) throw std::runtime_error("Invalid JIT resource bundle");
    const char *value = reinterpret_cast<const char *>(bytes.data() + offset);
    if (std::strlen(value) != size) throw std::runtime_error("Invalid JIT resource string");
    offset += static_cast<size_t>(size) + 1;
    return value;
  }
};
}  // namespace

int CeedEmbeddedJitOpen(Ceed ceed, int *count, const char ***names, const char ***sources, void **handle) {
  *handle = nullptr;
  try {
    auto          bundle = std::make_unique<Bundle>();
    unsigned char tag[16];
    std::memcpy(tag, CeedEmbeddedTag, sizeof(tag));
    bundle->bytes = ceed_jit::Crypt(false, CeedEmbeddedKey, CeedEmbeddedNonce, tag, CeedEmbeddedData, sizeof(CeedEmbeddedData));
    size_t offset = 0;
    while (offset < bundle->bytes.size()) {
      const auto name_size = bundle->ReadSize(offset), source_size = bundle->ReadSize(offset);
      bundle->names.push_back(bundle->ReadString(offset, name_size));
      bundle->sources.push_back(bundle->ReadString(offset, source_size));
    }
    *count   = static_cast<int>(bundle->names.size());
    *names   = bundle->names.data();
    *sources = bundle->sources.data();
    *handle  = bundle.release();
    return CEED_ERROR_SUCCESS;
  } catch (...) {
    return CeedError(ceed, CEED_ERROR_BACKEND, "Cannot authenticate or load protected JIT resources");
  }
}

int CeedEmbeddedJitClose(Ceed ceed, void **handle) {
  delete static_cast<Bundle *>(*handle);
  *handle = nullptr;
  return CEED_ERROR_SUCCESS;
}

int CeedEmbeddedJitFind(Ceed ceed, const char *path, char **source, bool *found) {
  *found = false;
  if (source) *source = nullptr;
  // Accept existing absolute gallery locations as well as virtual include names.
  const char *name = !std::strncmp(path, "ceed/", 5) ? path : std::strstr(path, "/ceed/");
  if (!name) return CEED_ERROR_SUCCESS;
  if (*name == '/') name++;
  std::string target(name);
  const auto  colon = target.rfind(':');
  if (colon != std::string::npos) target.resize(colon);
  int          count;
  const char **names, **sources;
  void        *handle;
  CeedCall(CeedEmbeddedJitOpen(ceed, &count, &names, &sources, &handle));
  int result = CEED_ERROR_SUCCESS;
  for (int i = 0; i < count; i++) {
    if (target != names[i]) continue;
    *found = true;
    if (source) result = CeedStringAllocCopy(sources[i], source);
    break;
  }
  CeedCall(CeedEmbeddedJitClose(ceed, &handle));
  return result;
}
#endif
