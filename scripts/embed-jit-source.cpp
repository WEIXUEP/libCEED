// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#include "../interface/ceed-jit-crypto.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>

static void AppendSize(std::vector<unsigned char> &data, size_t size) {
  if (size > UINT32_MAX) throw std::runtime_error("JIT resource is too large");
  for (int shift = 0; shift < 32; shift += 8) data.push_back(static_cast<unsigned char>(size >> shift));
}

static void WriteArray(std::ostream &output, const char *name, const unsigned char *data, size_t size) {
  output << "static const unsigned char " << name << "[] = {";
  for (size_t i = 0; i < size; i++) {
    if (i % 24 == 0) output << '\n';
    output << static_cast<unsigned int>(data[i]) << ',';
  }
  output << "\n};\n";
}

int main(int argc, char **argv) {
  try {
    if (argc != 4) throw std::runtime_error("Expected include root, file list, output header");
    const auto    root = std::filesystem::u8path(argv[1]);
    std::ifstream list(std::filesystem::u8path(argv[2]));
    if (!list) throw std::runtime_error("Cannot open JIT resource list");
    std::vector<unsigned char> plaintext;
    std::string                path;
    while (std::getline(list, path)) {
      if (!path.empty() && path.back() == '\r') path.pop_back();
      if (path.empty()) continue;
      const auto file = std::filesystem::u8path(path);
      const auto name = file.lexically_relative(root).generic_u8string();
      if (name.empty() || name.find("..") != std::string::npos) throw std::runtime_error("Invalid JIT resource path");
      std::ifstream input(file, std::ios::binary);
      if (!input) throw std::runtime_error("Cannot open JIT resource");
      const std::string original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
      // NVRTC's in-memory headers require explicit virtual names for relative includes.
      std::istringstream lines(original);
      std::string        source, line;
      const std::regex   local_include(R"re(^(\s*#\s*include\s*)"([^"]+)"(.*)$)re");
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch match;
        if (std::regex_match(line, match, local_include)) {
          const auto dependency = (file.parent_path() / std::filesystem::u8path(match[2].str())).lexically_normal();
          const auto relative   = dependency.lexically_relative(root).generic_u8string();
          if (!std::filesystem::is_regular_file(dependency) || relative.empty() || relative.compare(0, 3, "../") == 0)
            throw std::runtime_error("Invalid JIT include dependency");
          line = match[1].str() + "<" + relative + ">" + match[3].str();
        }
        source += line + '\n';
      }
      AppendSize(plaintext, name.size());
      AppendSize(plaintext, source.size());
      plaintext.insert(plaintext.end(), name.begin(), name.end());
      plaintext.push_back(0);
      plaintext.insert(plaintext.end(), source.begin(), source.end());
      plaintext.push_back(0);
    }
    if (plaintext.empty()) throw std::runtime_error("Empty JIT resource bundle");
    unsigned char key[32], nonce[12], tag[16];
    ceed_jit::Random(key, sizeof(key));
    ceed_jit::Random(nonce, sizeof(nonce));
    auto encrypted = ceed_jit::Crypt(true, key, nonce, tag, plaintext.data(), plaintext.size());
    SecureZeroMemory(plaintext.data(), plaintext.size());
    std::ofstream output(std::filesystem::u8path(argv[3]), std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write encrypted JIT resource header");
    WriteArray(output, "CeedEmbeddedKey", key, sizeof(key));
    WriteArray(output, "CeedEmbeddedNonce", nonce, sizeof(nonce));
    WriteArray(output, "CeedEmbeddedTag", tag, sizeof(tag));
    WriteArray(output, "CeedEmbeddedData", encrypted.data(), encrypted.size());
    output.close();
    if (!output) throw std::runtime_error("Cannot finish encrypted JIT resource header");
    SecureZeroMemory(key, sizeof(key));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
