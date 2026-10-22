// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#ifdef CEED_EMBEDDED_JIT
#include "ceed-cuda-cache.h"
#include "../../interface/ceed-jit-crypto.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace {
namespace fs                     = std::filesystem;
constexpr size_t max_binary_size = 64 * 1024 * 1024;

fs::path CacheDirectory() {
  if (const auto setting = std::getenv("CEED_JIT_CACHE")) {
    if (!std::strcmp(setting, "0") || !_stricmp(setting, "OFF")) return {};
  }
  if (const auto path = _wgetenv(L"CEED_JIT_CACHE_DIR")) return fs::path(path);
  if (const auto path = _wgetenv(L"LOCALAPPDATA")) return fs::path(path) / "compute-cache" / "jit-cache-v1";
  return {};
}

std::string Hex(const std::array<unsigned char, 32> &bytes) {
  std::ostringstream output;
  for (const auto byte : bytes) output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
  return output.str();
}

struct Dependencies {
  ceed_jit::Hash                    &hash;
  std::map<std::string, std::string> headers;
  std::vector<fs::path>              roots;
  std::set<std::string>              visited;

  bool Include(const std::string &name, bool quoted, const std::string &origin, int depth) {
    if (depth > 64 || visited.size() > 512) return false;
    std::vector<std::string> candidates;
    if (quoted && !origin.empty()) candidates.push_back((fs::u8path(origin).parent_path() / fs::u8path(name)).lexically_normal().generic_u8string());
    candidates.push_back(fs::u8path(name).lexically_normal().generic_u8string());
    for (const auto &candidate : candidates) {
      const auto header = headers.find(candidate);
      if (header != headers.end()) {
        if (!visited.insert("embedded:" + candidate).second) return true;
        return Scan(header->second, candidate, depth + 1);
      }
    }
    for (const auto &root : roots) candidates.push_back((root / fs::u8path(name)).lexically_normal().generic_u8string());
    for (const auto &candidate : candidates) {
      if (!fs::is_regular_file(fs::u8path(candidate))) continue;
      const auto path     = fs::weakly_canonical(fs::u8path(candidate));
      const auto identity = path.generic_u8string();
      if (!visited.insert("file:" + identity).second) return true;
      if (fs::file_size(path) > 16 * 1024 * 1024) return false;
      std::ifstream input(path, std::ios::binary);
      if (!input) return false;
      const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
      hash.Add(identity.data(), identity.size());
      hash.Add(source.data(), source.size());
      return Scan(source, identity, depth + 1);
    }
    // NVRTC's built-in C headers are identified by the compiler version in the key.
    static const std::set<std::string> builtins = {"stddef.h", "stdint.h", "stdbool.h", "math.h", "float.h", "limits.h"};
    return !quoted && builtins.count(name);
  }

  bool Scan(const std::string &source, const std::string &origin, int depth) {
    // Unknown preprocessing constructs must disable reuse, rather than miss an input dependency.
    for (const auto marker : {"R\"", "??", "%:", "__has_include", "__DATE__", "__TIME__", "__TIMESTAMP__"}) {
      if (source.find(marker) != std::string::npos) return false;
    }
    const auto       joined = std::regex_replace(source, std::regex("\\\\\r?\n"), "");
    const std::regex tokens(R"re("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*[\s\S]*?\*/)re");
    std::string      text;
    size_t           position = 0;
    for (auto it = std::sregex_iterator(joined.begin(), joined.end(), tokens); it != std::sregex_iterator(); ++it) {
      text.append(joined, position, it->position() - position);
      const auto token = it->str();
      if (token[0] == '/') {
        for (const auto ch : token) text += ch == '\n' ? '\n' : ' ';
      } else {
        text += token;
      }
      position = it->position() + it->length();
    }
    text.append(joined, position, std::string::npos);
    std::istringstream lines(text);
    std::string        line;
    const std::regex   directive(R"(^\s*#\s*(include|include_next|import)\b(.*)$)");
    const std::regex   literal(R"re(^\s*("[^"]+"|<[^>]+>)\s*$)re");
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      std::smatch match;
      if (!std::regex_match(line, match, directive)) continue;
      if (match[1] != "include") return false;
      const std::string argument = match[2];
      if (!std::regex_match(argument, match, literal)) return false;
      const std::string token = match[1];
      if (!Include(token.substr(1, token.size() - 2), token[0] == '"', origin, depth)) return false;
    }
    return true;
  }
};
}  // namespace

std::string CeedCudaCacheKey(const std::string &code, const std::string &compiler, int num_headers, const char *const *names,
                             const char *const *sources, int num_opts, const char *const *opts) {
  try {
    if (CacheDirectory().empty()) return {};
    ceed_jit::Hash hash;
    hash.Add(compiler.data(), compiler.size());
    hash.Add(code.data(), code.size());
    Dependencies dependencies{hash};
    for (int i = 0; i < num_headers; i++) {
      hash.Add(names[i], std::strlen(names[i]));
      hash.Add(sources[i], std::strlen(sources[i]));
      dependencies.headers.emplace(names[i], sources[i]);
    }
    for (int i = 0; i < num_opts; i++) {
      const std::string option(opts[i]);
      hash.Add(option.data(), option.size());
      if (option.compare(0, 2, "-I") == 0) dependencies.roots.push_back(fs::u8path(option.substr(2)));
    }
    if (!dependencies.Scan(code, "", 0)) return {};
    return Hex(hash.Finish());
  } catch (...) {
    return {};
  }
}

bool CeedCudaCacheLoad(const std::string &key, std::vector<char> &binary) {
  binary.clear();
  if (key.empty()) return false;
  try {
    const auto file = CacheDirectory() / (key + ".bin");
    const auto size = fs::file_size(file);
    if (size <= 40 || size > max_binary_size) return false;
    std::ifstream                 input(file, std::ios::binary);
    char                          magic[8];
    std::array<unsigned char, 32> expected;
    input.read(magic, sizeof(magic));
    input.read(reinterpret_cast<char *>(expected.data()), expected.size());
    if (!input || std::memcmp(magic, "CEEDJIT1", 8)) return false;
    binary.resize(static_cast<size_t>(size) - 40);
    input.read(binary.data(), binary.size());
    if (!input) return false;
    ceed_jit::Hash hash;
    hash.Add(binary.data(), binary.size());
    return hash.Finish() == expected;
  } catch (...) {
    return false;
  }
}

void CeedCudaCacheStore(const std::string &key, const char *binary, size_t size) {
  if (key.empty() || size > max_binary_size - 40) return;
  fs::path temporary;
  try {
    const auto directory = CacheDirectory();
    if (directory.empty()) return;
    fs::create_directories(directory);
    unsigned char random[32];
    ceed_jit::Random(random, sizeof(random));
    std::array<unsigned char, 32> random_name;
    std::memcpy(random_name.data(), random, sizeof(random));
    temporary = directory / (Hex(random_name) + ".tmp");
    ceed_jit::Hash hash;
    hash.Add(binary, size);
    const auto digest = hash.Finish();
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output.write("CEEDJIT1", 8);
      output.write(reinterpret_cast<const char *>(digest.data()), digest.size());
      output.write(binary, size);
      output.close();
      if (!output) throw std::runtime_error("Cannot write JIT cache");
    }
    const auto target = directory / (key + ".bin");
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Cannot publish JIT cache");
  } catch (...) {
    if (!temporary.empty()) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
    }
  }
}
#endif
