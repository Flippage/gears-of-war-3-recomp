#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace gow3::setup {

namespace detail {
// Reads `width` bytes at `off`; 0 when the range runs past the end.
inline uint64_t Read(std::span<const uint8_t> b, size_t off, size_t width, bool big) {
  if (off > b.size() || width > b.size() - off) return 0;
  uint64_t v = 0;
  for (size_t i = 0; i < width; ++i) {
    uint64_t byte = b[off + (big ? i : width - 1 - i)];
    v = (v << 8) | byte;
  }
  return v;
}
}  // namespace detail

inline uint16_t Be16(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint16_t>(detail::Read(b, off, 2, true));
}
inline uint32_t Be32(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint32_t>(detail::Read(b, off, 4, true));
}
inline uint64_t Be64(std::span<const uint8_t> b, size_t off) {
  return detail::Read(b, off, 8, true);
}
inline uint16_t Le16(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint16_t>(detail::Read(b, off, 2, false));
}
inline uint32_t Le32(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint32_t>(detail::Read(b, off, 4, false));
}

}  // namespace gow3::setup
