// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ceed_cuda_cache {
struct Include {
  std::string name;
  bool        quoted;
};
struct Includes {
  bool                 safe = true;
  std::vector<Include> entries;
};

// C preprocessing splices physical lines before recognizing comments/directives.
// Read directly from the source; never construct a second full source buffer.
struct SourceCursor {
  const char *data;
  size_t      size, position;
  int         Get() {
    while (position < size) {
      const unsigned char ch = data[position++];
      if (ch == '\\' && position < size) {
        if (data[position] == '\n') {
          ++position;
          continue;
        }
        if (data[position] == '\r' && position + 1 < size && data[position + 1] == '\n') {
          position += 2;
          continue;
        }
      }
      return ch;
    }
    return -1;
  }
  int Peek() const {
    auto copy = *this;
    return copy.Get();
  }
  bool StartsWith(const char *text) const {
    auto copy = *this;
    while (*text)
      if (copy.Get() != *text++) return false;
    return true;
  }
};

inline bool ParseDirective(const std::string &line, Includes &output) {
  size_t p = line.find_first_not_of(" \t\r\f\v", 1);
  if (p == std::string::npos) return true;
  const auto end       = line.find_first_not_of("abcdefghijklmnopqrstuvwxyz_", p);
  const auto directive = line.substr(p, end - p);
  if (directive == "include_next" || directive == "import" || directive == "embed") return false;
  if (directive != "include") return true;
  p = line.find_first_not_of(" \t\r\f\v", end);
  if (p == std::string::npos || (line[p] != '"' && line[p] != '<')) return false;
  const bool quoted = line[p] == '"';
  const auto close  = line.find(quoted ? '"' : '>', p + 1);
  if (close == std::string::npos || close == p + 1 || line.find_first_not_of(" \t\r\f\v", close + 1) != std::string::npos) return false;
  output.entries.push_back({line.substr(p + 1, close - p - 1), quoted});
  return true;
}

inline Includes ParseIncludes(const char *source, size_t size) {
  Includes     output;
  SourceCursor cursor{source, size, 0};
  if (size >= 3 && !std::memcmp(source, "\357\273\277", 3)) cursor.position = 3;
  enum { normal, line_comment, block_comment, quoted_string, character } state = normal;
  bool        beginning = true, directive = false;
  std::string line;
  auto        emit = [&](char ch) -> bool {
    if (ch == '\n') {
      if (directive && !ParseDirective(line, output)) return false;
      line.clear();
      beginning = true;
      directive = false;
    } else if (directive) {
      if (line.size() >= 65536) return false;
      line += ch;
    } else if (beginning && ch != ' ' && ch != '\t' && ch != '\r' && ch != '\f' && ch != '\v') {
      beginning = false;
      if (ch == '#') {
        directive = true;
        line      = "#";
      }
    }
    return true;
  };
  int value;
  while ((value = cursor.Get()) >= 0) {
    const char ch = static_cast<char>(value);
    // Conservative bypass, including markers split across physical lines.
    if ((ch == 'R' && cursor.Peek() == '"') || (ch == '?' && cursor.Peek() == '?') || (ch == '%' && cursor.Peek() == ':')) {
      output.safe = false;
      return output;
    }
    if (ch == '_' && cursor.Peek() == '_') {
      for (const auto marker : {"_has_include", "_DATE__", "_TIME__", "_TIMESTAMP__"}) {
        if (cursor.StartsWith(marker)) {
          output.safe = false;
          return output;
        }
      }
    }
    if (state == line_comment) {
      if (ch == '\n') {
        state = normal;
        if (!emit(ch)) {
          output.safe = false;
          return output;
        }
      }
    } else if (state == block_comment) {
      if (ch == '*' && cursor.Peek() == '/') {
        cursor.Get();
        state = normal;
      } else if (ch == '\n' && !emit(ch)) {
        output.safe = false;
        return output;
      }
    } else if (state == normal && ch == '/' && (cursor.Peek() == '/' || cursor.Peek() == '*')) {
      // Header-name tokens containing comment-like text are outside this scanner's subset.
      const auto opening = line.find('<');
      const auto word    = line.find_first_not_of(" \t\r\f\v", 1);
      if (directive && opening != std::string::npos && line.find('>', opening) == std::string::npos && word != std::string::npos &&
          line.compare(word, 7, "include") == 0) {
        output.safe = false;
        return output;
      }
      state = cursor.Get() == '/' ? line_comment : block_comment;
      if (!emit(' ')) {
        output.safe = false;
        return output;
      }
    } else {
      if (!emit(ch)) {
        output.safe = false;
        return output;
      }
      if (state == normal && (ch == '"' || ch == '\'')) state = ch == '"' ? quoted_string : character;
      else if (state != normal && ch == '\\') {
        const int escaped = cursor.Get();
        if (escaped >= 0 && !emit(static_cast<char>(escaped))) {
          output.safe = false;
          return output;
        }
      } else if ((state == quoted_string && ch == '"') || (state == character && ch == '\'')) state = normal;
    }
  }
  output.safe = emit('\n');
  return output;
}

// Only parse results are shared. Compare actual bytes on every call, so a same-size
// rewrite (even preserving timestamps) cannot reuse stale dependency information.
// Thread-local, bounded storage avoids locking and unbounded retained application source.
inline Includes HeaderIncludes(const char *source, size_t size, const std::string &identity) {
  struct Cached {
    std::string source;
    Includes    includes;
  };
  struct Cache {
    size_t                        bytes = 0;
    std::map<std::string, Cached> entries;
  };
  static thread_local Cache cache;
  auto                      found = cache.entries.find(identity);
  if (found != cache.entries.end()) {
    if (found->second.source.size() == size && (!size || !std::memcmp(found->second.source.data(), source, size))) return found->second.includes;
    cache.bytes -= found->second.source.size();
    cache.entries.erase(found);
  }
  auto parsed = ParseIncludes(source, size);
  if (size <= 1024 * 1024) {
    if (cache.bytes + size > 8 * 1024 * 1024 || cache.entries.size() >= 128) {
      cache.entries.clear();
      cache.bytes = 0;
    }
    cache.entries.emplace(identity, Cached{std::string(source, size), parsed});
    cache.bytes += size;
  }
  return parsed;
}
}  // namespace ceed_cuda_cache
