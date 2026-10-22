// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// CNG declarations require the Windows types above.
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ceed_jit {
inline void Check(NTSTATUS status) {
  if (status < 0) throw std::runtime_error("JIT cryptography failed");
}

struct Algorithm {
  BCRYPT_ALG_HANDLE value = nullptr;
  explicit Algorithm(LPCWSTR name) { Check(BCryptOpenAlgorithmProvider(&value, name, nullptr, 0)); }
  ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); }
  Algorithm(const Algorithm &) = delete;
};

inline void Random(unsigned char *data, ULONG size) { Check(BCryptGenRandom(nullptr, data, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG)); }

inline std::vector<unsigned char> Crypt(bool encrypt, const unsigned char *key, const unsigned char *nonce, unsigned char *tag,
                                        const unsigned char *input, size_t size) {
  if (size > std::numeric_limits<ULONG>::max()) throw std::runtime_error("JIT resource is too large");
  Algorithm algorithm(BCRYPT_AES_ALGORITHM);
  Check(BCryptSetProperty(algorithm.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
  BCRYPT_KEY_HANDLE handle = nullptr;
  Check(BCryptGenerateSymmetricKey(algorithm.value, &handle, nullptr, 0, const_cast<PUCHAR>(key), 32, 0));
  try {
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = const_cast<PUCHAR>(nonce);
    auth.cbNonce = 12;
    auth.pbTag   = tag;
    auth.cbTag   = 16;
    std::vector<unsigned char> output(size);
    ULONG                      written   = 0;
    const auto                 operation = encrypt ? BCryptEncrypt : BCryptDecrypt;
    Check(operation(handle, const_cast<PUCHAR>(input), static_cast<ULONG>(size), &auth, nullptr, 0, output.data(), static_cast<ULONG>(output.size()),
                    &written, 0));
    BCryptDestroyKey(handle);
    output.resize(written);
    return output;
  } catch (...) {
    BCryptDestroyKey(handle);
    throw;
  }
}

struct Hash {
  Algorithm          algorithm{BCRYPT_SHA256_ALGORITHM};
  BCRYPT_HASH_HANDLE value = nullptr;
  Hash() { Check(BCryptCreateHash(algorithm.value, &value, nullptr, 0, nullptr, 0, 0)); }
  ~Hash() { BCryptDestroyHash(value); }
  Hash(const Hash &) = delete;
  void Add(const void *data, size_t size) {
    if (size > std::numeric_limits<ULONG>::max()) throw std::runtime_error("JIT cache input is too large");
    const uint64_t length = size;
    Check(BCryptHashData(value, reinterpret_cast<PUCHAR>(const_cast<uint64_t *>(&length)), sizeof(length), 0));
    Check(BCryptHashData(value, reinterpret_cast<PUCHAR>(const_cast<void *>(data)), static_cast<ULONG>(size), 0));
  }
  std::array<unsigned char, 32> Finish() {
    std::array<unsigned char, 32> result;
    Check(BCryptFinishHash(value, result.data(), static_cast<ULONG>(result.size()), 0));
    return result;
  }
};
}  // namespace ceed_jit
