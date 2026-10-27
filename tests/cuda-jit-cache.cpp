// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
// Private cache tests; not installed or part of the public API.
#include "../backends/cuda/ceed-cuda-cache-hash.h"
#include "../backends/cuda/ceed-cuda-cache.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using ceed_cuda_cache::Hash;
using ceed_cuda_cache::Hex;
static void Check(bool good, const char *message) {
  if (!good) throw std::runtime_error(message);
}
static void Environment(const char *name, const std::string &value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}
static std::string Digest(const std::string &s) {
  Hash h;
  h.Update(s.data(), s.size());
  return Hex(h.Finish());
}
int main(int argc, char **argv) {
  try {
    if (argc < 3) return 1;
    const auto directory = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    Environment("CEED_JIT_CACHE_DIR", directory.generic_u8string());
    if (std::string(argv[1]) == "poison") {
      const char invalid[] = "not a CUDA module\n";
      for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".bin") continue;
        const auto key = entry.path().stem().u8string();
        CeedCudaCacheDiscard(key);
        Check(CeedCudaCacheStore(key, invalid, sizeof(invalid)) == CeedCudaCacheWrite::stored, "Poison publication failed");
      }
      return 0;
    }
    Check(Digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA empty");
    Check(Digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA abc");
    Check(Digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA padding");
    Check(Digest(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA million a");
    for (size_t size : {0, 1, 55, 56, 63, 64, 65, 127, 128}) {
      Hash h;
      for (size_t i = 0; i < size; i++) h.Update("x", 1);
      Check(Hex(h.Finish()) == Digest(std::string(size, 'x')), "SHA incremental");
    }
    Hash a, b;
    a.Add("ab");
    a.Add("c");
    b.Add("a");
    b.Add("bc");
    Check(a.Finish() != b.Finish(), "Field boundaries");
    const char *opts[] = {"-arch=sm_80", "-DCOEFFICIENT=2"};
    auto        key    = [&](const std::string &code, const std::string &compiler = "toolchain-driver-format-v1") {
      return CeedCudaCacheKey(code, compiler, 0, nullptr, nullptr, 2, opts);
    };
    const auto original = key("kernel");
    Check(!original.empty(), "Key unavailable");
    Check(original != key("changed kernel") && original != key("kernel", "toolchain-driver-format-v2"), "Source/compiler invalidation");
    for (const auto identity : {"different-toolkit", "different-driver", "different-format"})
      Check(original != key("kernel", identity), "Compiler/driver/format invalidation");
    opts[0] = "-arch=sm_90";
    Check(original != key("kernel"), "Architecture invalidation");
    opts[0] = "-arch=sm_80";
    opts[1] = "-DCOEFFICIENT=3";
    Check(original != key("kernel"), "Macro invalidation");
    opts[1] = "-DCOEFFICIENT=2";
    std::filesystem::create_directories(directory);
    const auto header = directory / "nested.h";
    auto       source = "#include \"" + header.generic_u8string() + "\"\n";
    {
      std::ofstream f(header);
      f << "#define VALUE 2\n";
    }
    const auto old_header = key(source);
    {
      std::ofstream f(header);
      f << "#define VALUE 3\n";
    }
    Check(!old_header.empty() && old_header != key(source), "Header-content invalidation");
    Check(key("#include SOME_MACRO\n").empty() && key("__TIME__").empty(), "Unsafe dependencies accepted");
    const char *names[]   = {"top.h", "nested/value.h"};
    const char *headers[] = {"#include \"nested/../nested/value.h\"\n", "#define VALUE 3\n"};
    Check(!CeedCudaCacheKey("#include <top.h>\n", "compiler", 2, names, headers, 2, opts).empty(), "Embedded relative include");
    headers[1]          = "#define VALUE 4\n";
    const auto embedded = CeedCudaCacheKey("#include <top.h>\n", "compiler", 2, names, headers, 2, opts);
    headers[1]          = "#define VALUE 5\n";
    Check(!embedded.empty() && embedded != CeedCudaCacheKey("#include <top.h>\n", "compiler", 2, names, headers, 2, opts), "Embedded invalidation");
    const char payload[] = "test PTX\n";
    CeedCudaCacheDiscard(original);
    {
      std::ofstream orphan(directory / (original + ".killed-writer.tmp"));
      orphan << "incomplete";
    }
    std::vector<char> bytes;
    Check(CeedCudaCacheLoad(original, bytes) == CeedCudaCacheRead::missing, "Killed-writer temporary file accepted");
    Check(CeedCudaCacheStore(original, payload, sizeof(payload)) == CeedCudaCacheWrite::stored, "Store failed");
    Check(CeedCudaCacheStore(original, payload, sizeof(payload)) == CeedCudaCacheWrite::existing, "Must not replace winner");
    Check(CeedCudaCacheLoad(original, bytes) == CeedCudaCacheRead::valid && bytes == std::vector<char>(payload, payload + sizeof(payload)),
          "Round trip");
    const auto file  = directory / (original + ".bin");
    const auto wrong = key("other");
    std::filesystem::copy_file(file, directory / (wrong + ".bin"), std::filesystem::copy_options::overwrite_existing);
    Check(CeedCudaCacheLoad(wrong, bytes) == CeedCudaCacheRead::invalid, "Renamed entry accepted");
    {
      std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
      f.seekp(105);
      f.put('!');
    }
    Check(CeedCudaCacheLoad(original, bytes) == CeedCudaCacheRead::invalid, "Digest corruption accepted");
    {
      std::ofstream f(file, std::ios::binary | std::ios::trunc);
      f << "short";
    }
    Check(CeedCudaCacheLoad(original, bytes) == CeedCudaCacheRead::invalid, "Truncated entry accepted");
    CeedCudaCacheDiscard(original);
    Check(CeedCudaCacheStore(original, payload, sizeof(payload)) == CeedCudaCacheWrite::stored, "Repair publication failed");
    for (int i = 0; i < 1000; i++) {
      Check(key("kernel") == original, "Unstable repeated key");
      Check(CeedCudaCacheLoad(original, bytes) == CeedCudaCacheRead::valid, "Repeated load failed");
      Check(CeedCudaCacheStore(original, payload, sizeof(payload)) == CeedCudaCacheWrite::existing, "Repeated publication failed");
    }
    Environment("CEED_JIT_CACHE", "off");
    Check(!CeedCudaCacheEnabled(), "OFF ignored");
    Environment("CEED_JIT_CACHE", "ON");
    Check(CeedCudaCacheEnabled(), "ON ignored");
    std::cout << "Cache hash, key invalidation, integrity and publication tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
