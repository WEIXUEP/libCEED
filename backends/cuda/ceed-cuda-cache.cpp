// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#include "ceed-cuda-cache.h"
#include "ceed-cuda-cache-hash.h"
#include "ceed-cuda-cache-source.h"

#include <cuda.h>
#include <nvrtc.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
using ceed_cuda_cache::Hash;
using ceed_cuda_cache::Hex;
const size_t max_binary_size = 8 * 1024 * 1024;
const size_t header_size     = 104;
#ifdef _WIN32
using Path = std::wstring;
Path Native(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (!n) throw std::runtime_error("Invalid UTF-8");
  Path p(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &p[0], n);
  return p;
}
std::string Utf8(const Path &p) {
  if (p.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p.data(), static_cast<int>(p.size()), nullptr, 0, nullptr, nullptr);
  if (!n) throw std::runtime_error("Invalid path");
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p.data(), static_cast<int>(p.size()), &s[0], n, nullptr, nullptr);
  return s;
}
Path Environment(const char *name) {
  const auto value = _wgetenv(Native(name).c_str());
  return value ? value : L"";
}
FILE *Open(const Path &p) { return _wfopen(p.c_str(), L"rb"); }
void  Remove(const Path &p) { DeleteFileW(p.c_str()); }
bool  Directory(const Path &p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
Path Absolute(const Path &p) {
  const DWORD attributes = GetFileAttributesW(p.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
  std::vector<wchar_t> buffer(512);
  DWORD                n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
  if (n >= buffer.size()) {
    buffer.resize(n + 1);
    n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
  }
  return n && n < buffer.size() ? Path(buffer.data(), n) : Path();
}
#else
using Path = std::string;
Path        Native(const std::string &s) { return s; }
std::string Utf8(const Path &p) { return p; }
Path        Environment(const char *name) {
  const auto value = std::getenv(name);
  return value ? value : "";
}
FILE *Open(const Path &p) { return std::fopen(p.c_str(), "rb"); }
void  Remove(const Path &p) { unlink(p.c_str()); }
bool  Directory(const Path &p) {
  struct stat st;
  return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode);
}
Path Absolute(const Path &p) {
  char *resolved = realpath(p.c_str(), nullptr);
  if (!resolved) return {};
  const Path result(resolved);
  std::free(resolved);
  return result;
}
#endif
bool IsAbsolute(const Path &p) {
#ifdef _WIN32
  return p.size() > 2 && ((p[1] == L':' && (p[2] == L'/' || p[2] == L'\\')) || (p[0] == L'\\' && p[1] == L'\\'));
#else
  return !p.empty() && p[0] == '/';
#endif
}
Path Parent(const Path &p) {
  const auto n = p.find_last_of(Native("/\\"));
  return n == Path::npos ? Path() : p.substr(0, n + 1);
}
Path Join(const Path &a, const Path &b) {
  return IsAbsolute(b) || a.empty() ? b : a + ((a.back() == '/' || a.back() == '\\') ? Path() : Native("/")) + b;
}
std::string Logical(std::string name) {
  for (auto &ch : name)
    if (ch == '\\') ch = '/';
  std::istringstream       stream(name);
  std::vector<std::string> parts;
  std::string              part;
  while (std::getline(stream, part, '/')) {
    if (part.empty() || part == ".") continue;
    if (part == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
    else parts.push_back(part);
  }
  std::string result;
  for (const auto &p : parts) result += (result.empty() ? "" : "/") + p;
  return result;
}
bool MakeDirectories(const Path &p) {
  if (p.empty()) return false;
  if (Directory(p)) return true;
  Path parent = Parent(p);
  while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\')) parent.pop_back();
  if (!parent.empty() && parent != p && !Directory(parent) && !MakeDirectories(parent)) return false;
#ifdef _WIN32
  return CreateDirectoryW(p.c_str(), nullptr) || Directory(p);
#else
  return !mkdir(p.c_str(), 0700) || Directory(p);
#endif
}
Path CacheDirectory() {
  const auto explicit_path = Environment("CEED_JIT_CACHE_DIR");
  if (!explicit_path.empty()) return explicit_path;
#ifdef _WIN32
  const auto base = Environment("LOCALAPPDATA");
#else
  auto base = Environment("XDG_CACHE_HOME");
  if (!IsAbsolute(base)) {
    base = Environment("HOME");
    if (!base.empty()) base = Join(base, Native(".cache"));
  }
#endif
  return IsAbsolute(base) ? Join(base, Native("compute-cache/jit-cache-v2")) : Path();
}
bool ValidKey(const std::string &key) { return key.size() == 64 && key.find_first_not_of("0123456789abcdef") == std::string::npos; }
using File = std::unique_ptr<FILE, int (*)(FILE *)>;
bool Read(const Path &p, std::string &data, size_t limit) {
  File f(Open(p), std::fclose);
  if (!f || std::fseek(f.get(), 0, SEEK_END)) return false;
  const long size = std::ftell(f.get());
  if (size < 0 || static_cast<unsigned long>(size) > limit || std::fseek(f.get(), 0, SEEK_SET)) return false;
  data.resize(size);
  return (!size || std::fread(&data[0], 1, size, f.get()) == static_cast<size_t>(size)) && !std::ferror(f.get());
}
std::string ModuleIdentity(const void *address) {
#ifdef _WIN32
  HMODULE module;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address),
                          &module))
    return {};
  std::vector<wchar_t> buffer(32768);
  DWORD                n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!n || n >= buffer.size()) return {};
  Path                      path(buffer.data(), n);
  WIN32_FILE_ATTRIBUTE_DATA st;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &st)) return {};
  return Utf8(path) + ":" + std::to_string(st.nFileSizeHigh) + ":" + std::to_string(st.nFileSizeLow) + ":" +
         std::to_string(st.ftLastWriteTime.dwHighDateTime) + ":" + std::to_string(st.ftLastWriteTime.dwLowDateTime);
#else
  Dl_info info;
  if (!dladdr(address, &info) || !info.dli_fname) return {};
  Path        path = Absolute(info.dli_fname);
  struct stat st;
  if (path.empty() || stat(path.c_str(), &st)) return {};
  return path + ":" + std::to_string(st.st_size) + ":" + std::to_string(st.st_mtim.tv_sec) + ":" + std::to_string(st.st_mtim.tv_nsec);
#endif
}

// Scan conservatively: all literal includes, even inactive branches, contribute.
// Unknown preprocessing constructs bypass caching rather than risk a stale hit.
struct Dependencies {
  Hash                               &hash;
  std::map<std::string, const char *> headers;
  std::vector<Path>                   roots;
  std::vector<Path>                   parents;
  std::set<std::string>               visited;
  std::set<std::string>               active;
  std::map<Path, Path>                paths;
  std::map<Path, std::string>         files;
  explicit Dependencies(Hash &h) : hash(h) {}
  const Path &Resolve(const Path &path) {
    const auto found = paths.find(path);
    return found == paths.end() ? paths.emplace(path, Absolute(path)).first->second : found->second;
  }
  const std::string *Contents(const Path &path) {
    const auto entry = files.emplace(path, std::string());
    if (entry.second && !Read(path, entry.first->second, 16 * 1024 * 1024)) {
      files.erase(entry.first);
      return nullptr;
    }
    return &entry.first->second;
  }
  bool Include(const std::string &name, bool quoted, int depth) {
    if (depth > 64 || visited.size() >= 512) return false;
    bool              found = false;
    std::vector<Path> candidates;
    if (quoted) {
      for (auto parent = parents.rbegin(); parent != parents.rend(); ++parent) candidates.push_back(Join(*parent, Native(name)));
    }
    candidates.push_back(Native(name));
    for (const auto &root : roots) candidates.push_back(Join(root, Native(name)));
    // Hash every candidate that could be chosen, avoiding assumptions about NVRTC's
    // embedded/disk search precedence. Adding a shadowing header changes the key.
    std::string context;
    for (const auto &parent : parents) context += Utf8(parent) + "\n";
    std::set<Path> distinct;
    for (const auto &candidate : candidates) {
      if (!distinct.insert(candidate).second) continue;
      const auto embedded = headers.find(Logical(Utf8(candidate)));
      if (embedded != headers.end()) {
        found               = true;
        const auto identity = "embedded:" + embedded->first;
        if (!active.count(identity) && visited.insert(identity + context).second) {
          active.insert(identity);
          const bool good = Scan(embedded->second, std::strlen(embedded->second), embedded->first, depth + 1, identity);
          active.erase(identity);
          if (!good) return false;
        }
      }
      const auto &path = Resolve(candidate);
      if (path.empty()) continue;
      found               = true;
      const auto identity = "file:" + Utf8(candidate) + ":" + Utf8(path);
      if (active.count(identity) || !visited.insert(identity + context).second) continue;
      const auto *source = Contents(path);
      if (!source) return false;
      hash.Add(identity);
      hash.Add(*source);
      active.insert(identity);
      const bool good = Scan(source->data(), source->size(), Utf8(path), depth + 1, "file:" + Utf8(path));
      active.erase(identity);
      if (!good) return false;
    }
    if (found) return true;
    // NVRTC built-ins are bound to the compiler identity. Other unresolved names bypass.
    static const std::set<std::string> builtins = {"stddef.h", "stdint.h", "stdbool.h", "math.h", "float.h", "limits.h"};
    return !quoted && builtins.count(name);
  }
  bool Scan(const char *source, size_t size, const std::string &origin, int depth, const std::string &identity = "") {
    struct ParentGuard {
      std::vector<Path> &stack;
      ParentGuard(std::vector<Path> &stack, const Path &path) : stack(stack) { stack.push_back(Parent(path)); }
      ~ParentGuard() { stack.pop_back(); }
    } guard(parents, Native(origin));
    const auto parsed = identity.empty() ? ceed_cuda_cache::ParseIncludes(source, size) : ceed_cuda_cache::HeaderIncludes(source, size, identity);
    if (!parsed.safe) return false;
    for (const auto &include : parsed.entries) {
      if (!Include(include.name, include.quoted, depth)) return false;
    }
    return true;
  }
};
}  // namespace

bool CeedCudaCacheEnabled() noexcept {
  try {
    auto setting = Utf8(Environment("CEED_JIT_CACHE"));
    for (auto &ch : setting)
      if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return setting != "0" && setting != "off" && setting != "false" && setting != "no";
  } catch (...) {
    return false;
  }
}

std::string CeedCudaCacheCompiler() noexcept {
  try {
    static const std::string identity = []() -> std::string {
      int major, minor, driver;
      if (nvrtcVersion(&major, &minor) != NVRTC_SUCCESS || cuDriverGetVersion(&driver) != CUDA_SUCCESS) return {};
      const auto nvrtc = ModuleIdentity(reinterpret_cast<const void *>(&nvrtcVersion));
      const auto cuda  = ModuleIdentity(reinterpret_cast<const void *>(&cuDriverGetVersion));
      if (nvrtc.empty() || cuda.empty()) return {};
      return "cuda-cache-v2:" + std::to_string(CUDA_VERSION) + ":" + std::to_string(major) + ":" + std::to_string(minor) + ":" +
             std::to_string(driver) + ":" + nvrtc + ":" + cuda;
    }();
    return identity;
  } catch (...) {
    return {};
  }
}

namespace {
// SHA state after an embedded bundle is independent of each kernel. Validate the
// bytes, not addresses/timestamps: protected bundles are decrypted anew per call.
// Only the most recent bundle is retained, with a strict per-thread memory bound.
Hash EmbeddedPrefix(int count, const char *const *names, const char *const *sources) {
  struct Bundle {
    std::vector<std::string> names, sources;
    Hash                     prefix;
    bool                     valid = false;
  };
  static thread_local Bundle previous;
  bool                       same  = previous.valid && previous.names.size() == static_cast<size_t>(count);
  size_t                     bytes = 0;
  std::vector<size_t>        name_sizes(count), source_sizes(count);
  for (int i = 0; i < count; ++i) {
    name_sizes[i]   = std::strlen(names[i]);
    source_sizes[i] = std::strlen(sources[i]);
    bytes += name_sizes[i] + source_sizes[i];
    if (same &&
        (previous.names[i].size() != name_sizes[i] || previous.sources[i].size() != source_sizes[i] ||
         std::memcmp(previous.names[i].data(), names[i], name_sizes[i]) || std::memcmp(previous.sources[i].data(), sources[i], source_sizes[i])))
      same = false;
  }
  if (same) return previous.prefix;
  Hash prefix;
  // Distinct domain: earlier keys hashed the kernel before the entire bundle.
  prefix.Add("cuda-cache-v2:embedded-prefix-v1");
  prefix.Add(std::to_string(count));
  for (int i = 0; i < count; ++i) {
    prefix.Add(names[i], name_sizes[i]);
    prefix.Add(sources[i], source_sizes[i]);
  }
  previous.valid = false;
  previous.names.clear();
  previous.sources.clear();
  if (bytes <= 8 * 1024 * 1024 && count <= 512) {
    for (int i = 0; i < count; ++i) {
      previous.names.emplace_back(names[i], name_sizes[i]);
      previous.sources.emplace_back(sources[i], source_sizes[i]);
    }
    previous.prefix = prefix;
    previous.valid  = true;
  }
  return prefix;
}
}  // namespace

std::string CeedCudaCacheKey(const std::string &code, const std::string &compiler, int num_headers, const char *const *names,
                             const char *const *sources, int num_opts, const char *const *opts) noexcept {
  try {
    if (compiler.empty() || CacheDirectory().empty()) return {};
    Hash hash;
    if (num_headers > 0) hash = EmbeddedPrefix(num_headers, names, sources);
    else hash.Add("cuda-cache-v2");
    hash.Add(compiler);
    hash.Add(code);
    if (num_headers == 0) hash.Add(std::to_string(num_headers));
    Dependencies dependencies(hash);
    for (int i = 0; i < num_headers; i++) {
      if (!dependencies.headers.emplace(Logical(names[i]), sources[i]).second) return {};
    }
    hash.Add(std::to_string(num_opts));
    for (int i = 0; i < num_opts; i++) {
      const std::string option(opts[i]);
      hash.Add(option);
      if (option.compare(0, 2, "-I") == 0) dependencies.roots.push_back(Native(option.substr(2)));
      else if (option.find("include") != std::string::npos || option.find("options-file") != std::string::npos ||
               (!option.empty() && option[0] == '@'))
        return {};
    }
    if (!dependencies.Scan(code.data(), code.size(), "", 0)) return {};
    return Hex(hash.Finish());
  } catch (...) {
    return {};
  }
}

CeedCudaCacheRead CeedCudaCacheLoad(const std::string &key, std::vector<char> &binary) noexcept {
  binary.clear();
  try {
    if (!ValidKey(key)) return CeedCudaCacheRead::missing;
    const auto directory = CacheDirectory();
    if (directory.empty()) return CeedCudaCacheRead::missing;
    std::string bytes;
    const auto  path = Join(directory, Native(key + ".bin"));
    File        exists(Open(path), std::fclose);
    if (!exists) return CeedCudaCacheRead::missing;
    exists.reset();
    if (!Read(path, bytes, max_binary_size + header_size) || bytes.size() <= header_size || std::memcmp(bytes.data(), "CUDAJC02", 8) ||
        bytes.compare(8, 64, key))
      return CeedCudaCacheRead::invalid;
    Hash hash;
    hash.Update(bytes.data() + header_size, bytes.size() - header_size);
    const auto digest = hash.Finish();
    if (std::memcmp(bytes.data() + 72, digest.data(), digest.size())) return CeedCudaCacheRead::invalid;
    binary.assign(bytes.begin() + header_size, bytes.end());
    // CUBIN is ELF; legacy PTX is NUL terminated. Reject unsafe, unterminated input.
    if ((binary.size() < 4 || std::memcmp(binary.data(), "\177ELF", 4)) && binary.back() != '\0') {
      binary.clear();
      return CeedCudaCacheRead::invalid;
    }
    return CeedCudaCacheRead::valid;
  } catch (...) {
    binary.clear();
    return CeedCudaCacheRead::invalid;
  }
}

void CeedCudaCacheDiscard(const std::string &key) noexcept {
  try {
    const auto directory = CacheDirectory();
    if (ValidKey(key) && !directory.empty()) Remove(Join(directory, Native(key + ".bin")));
  } catch (...) {
  }
}

CeedCudaCacheWrite CeedCudaCacheStore(const std::string &key, const char *binary, size_t size) noexcept {
  Path temporary;
  try {
    if (!size || size > max_binary_size) return CeedCudaCacheWrite::oversized;
    const auto directory = CacheDirectory();
    if (!ValidKey(key) || directory.empty() || !MakeDirectories(directory)) return CeedCudaCacheWrite::failed;
    static std::atomic<unsigned long> counter(0);
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary = Join(directory, Native(key + "." + std::to_string(pid) + "." + std::to_string(tick) + "." + std::to_string(counter++) + ".tmp"));
    const auto target = Join(directory, Native(key + ".bin"));
    Hash       hash;
    hash.Update(binary, size);
    const auto  digest = hash.Finish();
    std::string data("CUDAJC02", 8);
    data += key;
    data.append(reinterpret_cast<const char *>(digest.data()), digest.size());
    data.append(binary, size);
#ifdef _WIN32
    HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return CeedCudaCacheWrite::failed;
    DWORD written = 0;
    bool  good    = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    if (good) good = FlushFileBuffers(f) != 0;
    if (!CloseHandle(f)) good = false;
    if (good && MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return CeedCudaCacheWrite::stored;
    const DWORD error = GetLastError();
    Remove(temporary);
    return good && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) ? CeedCudaCacheWrite::existing : CeedCudaCacheWrite::failed;
#else
    int f = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (f < 0) return CeedCudaCacheWrite::failed;
    size_t offset = 0;
    while (offset < data.size()) {
      const auto n = write(f, data.data() + offset, data.size() - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      offset += n;
    }
    bool good = offset == data.size() && !fsync(f);
    if (close(f)) good = false;
    const int published = good ? link(temporary.c_str(), target.c_str()) : -1;
    const int error     = errno;
    Remove(temporary);
    return published == 0 ? CeedCudaCacheWrite::stored : good && error == EEXIST ? CeedCudaCacheWrite::existing : CeedCudaCacheWrite::failed;
#endif
  } catch (...) {
    if (!temporary.empty()) Remove(temporary);
    return CeedCudaCacheWrite::failed;
  }
}
