#include "setup/xex_info.h"

#include <cstdio>
#include <fstream>
#include <vector>

#include "setup/be.h"

namespace gow3::setup {

namespace {
constexpr uint32_t kExecutionInfoKey = 0x00040006;
constexpr size_t kHeaderPairsOffset = 0x18;
constexpr size_t kMaxHeader = 64 * 1024;
}  // namespace

std::optional<XexInfo> ReadXexInfo(std::span<const uint8_t> h) {
  if (h.size() < kHeaderPairsOffset || Be32(h, 0) != 0x58455832) return std::nullopt;  // "XEX2"
  uint32_t count = Be32(h, 0x14);
  for (uint32_t i = 0; i < count; ++i) {
    size_t at = kHeaderPairsOffset + size_t{i} * 8;
    if (at + 8 > h.size()) return std::nullopt;
    if (Be32(h, at) != kExecutionInfoKey) continue;
    size_t off = Be32(h, at + 4);
    if (off > h.size() || h.size() - off < 16) return std::nullopt;
    XexInfo info;
    info.media_id = Be32(h, off);
    info.version = Be32(h, off + 4);
    info.title_id = Be32(h, off + 12);
    return info;
  }
  return std::nullopt;
}

std::optional<XexInfo> ReadXexInfoFile(const std::filesystem::path& xex) {
  std::ifstream f(xex, std::ios::binary);
  if (!f) return std::nullopt;
  std::vector<uint8_t> buf(kMaxHeader);
  f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  buf.resize(static_cast<size_t>(f.gcount()));
  return ReadXexInfo(buf);
}

std::string Hex8(uint32_t v) {
  char s[9];
  std::snprintf(s, sizeof s, "%08X", v);
  return s;
}

}  // namespace gow3::setup
