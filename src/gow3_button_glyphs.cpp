// gow3 - ReXGlue Recompiled Project
//
// Dynamic button prompt glyph replacement for Gears of War 3.
#include "gow3_button_glyphs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/window_listener.h>

REXCVAR_DEFINE_STRING(gow3_button_prompts, "auto", "input",
                      "Button prompt glyph style: auto (switch on the fly), xbox, playstation, "
                      "keyboard, original (Xbox 360).");

namespace rex::glue {
namespace {

constexpr uint64_t kAtlas512Hash = UINT64_C(0xa000624a174e08ba);
constexpr uint64_t kAtlas256Hash = UINT64_C(0xae5c68a9d7f7398c);
// Returning to the main menu loads the 256x256 atlas again, laid out one 4 KB page earlier, which
// hashes differently (the same blocks: compared from dumps). Its replacement is the same image.
constexpr uint64_t kAtlas256MenuHash = UINT64_C(0x2587228e1f70bb5f);
// 256x256 melee atlas: chainsaw duel "mash B", LT/RT and Y (found in a dump of a duel).
constexpr uint64_t kAtlasDuelHash = UINT64_C(0x30d01cc6c7ff5cd3);
constexpr const char kAtlasDumpHashes[] = "a000624a174e08ba,ae5c68a9d7f7398c,30d01cc6c7ff5cd3";

constexpr size_t kExpectedSize512 = 1398248;
constexpr size_t kExpectedSize256 = 349672;

enum class DeviceType {
  kGamepad,
  kKeyboard,
};

enum class ControllerType {
  kXbox,
  kPlayStation,
};

struct SpriteDef {
  const char* button;
  int box[4];     // x0, y0, x1, y1
  int center[2];  // cx, cy
  int size;       // target square size
};

// 512x512 HUD action prompt sprites
const SpriteDef kSprites512[] = {
    {"Y", {176, 314, 208, 376}, {191, 350}, 28},
    {"A", {214, 314, 245, 356}, {229, 342}, 28},
    {"A", {250, 314, 281, 357}, {265, 344}, 28},
    {"B", {285, 314, 327, 345}, {312, 329}, 28},
    {"X", {331, 314, 374, 345}, {345, 329}, 28},
    {"X", {378, 314, 419, 345}, {391, 330}, 28},
    {"Y", {390, 423, 422, 462}, {405, 436}, 28},
    {"RS", {426, 423, 459, 476}, {442, 459}, 28},
    {"RS", {462, 423, 493, 476}, {477, 460}, 28},
};

// 256x256 font button glyphs (menus, bottom bars, lobby/invite badges)
const SpriteDef kSprites256[] = {
    // Face buttons
    {"A", {201, 55, 224, 78}, {212, 66}, 24},
    {"B", {228, 55, 251, 78}, {239, 66}, 24},
    {"X", {196, 108, 219, 131}, {207, 119}, 24},
    {"Y", {222, 108, 245, 131}, {233, 119}, 24},

    // Large Triggers (LT/RT) - big trigger shapes (Horde map-select arrows)
    {"LT", {32, 134, 55, 162}, {43, 148}, 24},
    {"RT", {57, 134, 80, 162}, {68, 148}, 24},

    // First Bumpers pair (LB/RB) - Manage / Invite screen badges
    {"LB", {82, 139, 109, 157}, {95, 148}, 22},
    {"RB", {110, 139, 136, 157}, {123, 148}, 22},

    // Second Bumpers pair (small LB/RB)
    {"LB", {0, 219, 26, 237}, {13, 228}, 22},
    {"RB", {29, 219, 55, 237}, {42, 228}, 22},

    // Second Triggers pair (small LT/RT)
    {"LT", {203, 188, 226, 216}, {214, 202}, 24},
    {"RT", {230, 188, 253, 216}, {241, 202}, 24},

    // D-pad directional icons (mapped by highlighted arm)
    {"DpadDown", {1, 58, 19, 78}, {10, 68}, 20},
    {"DpadLeft", {22, 58, 42, 76}, {32, 67}, 20},
    {"DpadRight", {196, 32, 215, 49}, {205, 40}, 20},
    {"DpadUp", {219, 29, 238, 49}, {228, 39}, 20},
    {"DpadLeft", {0, 196, 21, 213}, {10, 204}, 20},
    {"DpadRight", {31, 196, 50, 213}, {40, 204}, 20},
    {"DpadDown", {82, 196, 98, 215}, {90, 205}, 20},
    {"DpadUpDown", {159, 190, 177, 215}, {168, 202}, 20},
    {"DpadLeftRight", {179, 192, 200, 209}, {189, 200}, 20},
    {"DpadAll", {207, 165, 226, 184}, {216, 174}, 20},
    {"DpadUp", {233, 162, 250, 184}, {241, 173}, 20},

    // 360 Back pills (◀) - Horde "SELECT MAP", Manage/Invite "MUTE"
    {"Back", {46, 58, 70, 75}, {58, 66}, 22},
    {"Back", {107, 58, 130, 75}, {118, 66}, 22},

    // 360 Start pills (▶) - Pause / Menu
    {"Start", {75, 58, 100, 75}, {87, 66}, 22},
    {"Start", {151, 58, 176, 75}, {163, 66}, 22},

    // Sticks
    {"RS", {0, 84, 21, 103}, {10, 93}, 24},
    {"LS", {202, 84, 223, 103}, {212, 93}, 24},
    {"LS", {224, 84, 245, 103}, {234, 93}, 24},
    {"RSPress", {82, 162, 107, 188}, {94, 175}, 24},
    {"LSPress", {145, 162, 170, 188}, {157, 175}, 24},
    {"StickMove", {61, 216, 84, 242}, {72, 229}, 24},
};

// 256x256 melee atlas (chainsaw duels). Each pair is a large and a small frame (the prompt pulses),
// so these cells are replaced by ReplaceButtonInPlace at each frame's own size: box is the area to
// look in, center a point inside the button. size 1 marks B and Y, whose box also holds three empty
// buttons showing where the button sits on an Xbox pad (wrong once rebound): the whole box is
// cleared, leaving only the new glyph.
const SpriteDef kSpritesDuel[] = {
    {"B", {80, 0, 127, 31}, {112, 15}, 1},
    {"B", {80, 32, 127, 63}, {112, 46}, 1},
    {"LT", {0, 128, 34, 166}, {17, 147}, 0},
    {"RT", {50, 131, 80, 163}, {65, 147}, 0},
    {"LT", {2, 168, 33, 201}, {17, 185}, 0},
    {"RT", {47, 166, 83, 204}, {65, 185}, 0},
    {"Y", {2, 203, 38, 250}, {18, 221}, 1},
    {"Y", {42, 203, 75, 250}, {58, 222}, 1},
};

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
};

uint32_t Le32(const uint8_t* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t Le16(const uint8_t* p) {
  return p[0] | (uint16_t(p[1]) << 8);
}

// Decodes standard or DX10 BC3 (DXT5) to RGBA8.
bool DecodeBc3(const uint8_t* data, size_t size, int w, int h, Image* out) {
  if (w <= 0 || h <= 0 || (w % 4 != 0) || (h % 4 != 0)) return false;
  const size_t num_blocks = size_t(w / 4) * (h / 4);
  if (size < num_blocks * 16) return false;

  out->width = w;
  out->height = h;
  out->rgba.assign(size_t(w) * h * 4, 0);

  const int blocks_x = w / 4;
  const int blocks_y = h / 4;

  for (int by = 0; by < blocks_y; ++by) {
    for (int bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* block = data + (size_t(by) * blocks_x + bx) * 16;
      const uint8_t a0 = block[0], a1 = block[1];
      uint8_t alphas[8];
      alphas[0] = a0;
      alphas[1] = a1;
      if (a0 > a1) {
        for (int i = 1; i < 7; ++i) alphas[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
      } else {
        for (int i = 1; i < 5; ++i) alphas[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
      }

      uint64_t abits = uint64_t(block[2]) | (uint64_t(block[3]) << 8) | (uint64_t(block[4]) << 16) |
                       (uint64_t(block[5]) << 24) | (uint64_t(block[6]) << 32) |
                       (uint64_t(block[7]) << 40);

      const uint16_t c0 = Le16(block + 8);
      const uint16_t c1 = Le16(block + 10);
      const uint8_t r0 = ((c0 >> 11) & 0x1F) * 255 / 31;
      const uint8_t g0 = ((c0 >> 5) & 0x3F) * 255 / 63;
      const uint8_t b0 = (c0 & 0x1F) * 255 / 31;
      const uint8_t r1 = ((c1 >> 11) & 0x1F) * 255 / 31;
      const uint8_t g1 = ((c1 >> 5) & 0x3F) * 255 / 63;
      const uint8_t b1 = (c1 & 0x1F) * 255 / 31;

      uint8_t colors[4][3];
      colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
      colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
      if (c0 > c1) {
        colors[2][0] = (2 * r0 + r1) / 3; colors[2][1] = (2 * g0 + g1) / 3; colors[2][2] = (2 * b0 + b1) / 3;
        colors[3][0] = (r0 + 2 * r1) / 3; colors[3][1] = (g0 + 2 * g1) / 3; colors[3][2] = (b0 + 2 * b1) / 3;
      } else {
        colors[2][0] = (r0 + r1) / 2; colors[2][1] = (g0 + g1) / 2; colors[2][2] = (b0 + b1) / 2;
        colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0;
      }

      const uint32_t cbits = Le32(block + 12);

      for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
          const int idx = py * 4 + px;
          const uint8_t a_idx = uint8_t((abits >> (3 * idx)) & 7);
          const uint8_t c_idx = uint8_t((cbits >> (2 * idx)) & 3);
          const int px_x = bx * 4 + px;
          const int px_y = by * 4 + py;
          uint8_t* dst = &out->rgba[(size_t(px_y) * w + px_x) * 4];
          dst[0] = colors[c_idx][0];
          dst[1] = colors[c_idx][1];
          dst[2] = colors[c_idx][2];
          dst[3] = alphas[a_idx];
        }
      }
    }
  }
  return true;
}

bool ReadDds(const std::filesystem::path& path, Image* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const size_t file_size = size_t(f.tellg());
  if (file_size < 128) return false;
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> buf(file_size);
  if (!f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(file_size))) return false;

  if (std::memcmp(buf.data(), "DDS ", 4) != 0) return false;
  const int height = int(Le32(&buf[12]));
  const int width = int(Le32(&buf[16]));
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;

  const uint32_t pf_flags = Le32(&buf[80]);
  const uint32_t fourcc = Le32(&buf[84]);

  size_t data_offset = 128;
  bool is_dx10 = false;
  uint32_t dxgi_format = 0;
  if ((pf_flags & 0x4) && fourcc == 0x30315844 /* DX10 */) {
    if (file_size < 148) return false;
    is_dx10 = true;
    dxgi_format = Le32(&buf[128]);
    data_offset = 148;
  }

  // RGBA8 UNORM
  if (is_dx10 && dxgi_format == 28 /* DXGI_FORMAT_R8G8B8A8_UNORM */) {
    const size_t needed = size_t(width) * height * 4;
    if (file_size < data_offset + needed) return false;
    out->width = width;
    out->height = height;
    out->rgba.resize(needed);
    std::memcpy(out->rgba.data(), buf.data() + data_offset, needed);
    return true;
  }

  // BC3 / DXT5
  if ((!is_dx10 && (pf_flags & 0x4) && fourcc == 0x35545844 /* DXT5 */) ||
      (is_dx10 && (dxgi_format == 77 || dxgi_format == 78 /* BC3 */))) {
    return DecodeBc3(buf.data() + data_offset, file_size - data_offset, width, height, out);
  }

  return false;
}

Image Halve(const Image& in) {
  Image out;
  out.width = std::max(in.width / 2, 1);
  out.height = std::max(in.height / 2, 1);
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      float sum[4] = {0, 0, 0, 0};
      int n = 0;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const int sx = std::min(x * 2 + dx, in.width - 1);
          const int sy = std::min(y * 2 + dy, in.height - 1);
          const uint8_t* p = &in.rgba[(size_t(sy) * in.width + sx) * 4];
          const float a = p[3] / 255.0f;
          sum[0] += p[0] * a;
          sum[1] += p[1] * a;
          sum[2] += p[2] * a;
          sum[3] += a;
          ++n;
        }
      }
      uint8_t* q = &out.rgba[(size_t(y) * out.width + x) * 4];
      const float a = sum[3] / float(n);
      for (int c = 0; c < 3; ++c) {
        q[c] = a > 1e-4f ? uint8_t(std::lround(std::min(255.0f, sum[c] / sum[3]))) : 0;
      }
      q[3] = uint8_t(std::lround(a * 255.0f));
    }
  }
  return out;
}

bool WriteDds(const std::filesystem::path& path, const Image& image) {
  if (image.width <= 0 || image.height <= 0 ||
      image.rgba.size() != size_t(image.width) * image.height * 4) {
    return false;
  }

  int mip_count = 1;
  int w = image.width, h = image.height;
  while (w > 1 || h > 1) {
    w = std::max(w / 2, 1);
    h = std::max(h / 2, 1);
    ++mip_count;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path tmp = path.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    uint32_t magic = 0x20534444;  // "DDS "
    uint32_t header[31] = {};
    header[0] = 124;  // header size
    header[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT | MIPMAPCOUNT
    header[2] = uint32_t(image.height);
    header[3] = uint32_t(image.width);
    header[4] = uint32_t(image.width * 4);
    header[5] = 1;  // depth
    header[6] = uint32_t(mip_count);

    header[18] = 32;          // pf size
    header[19] = 0x4;         // DDPF_FOURCC
    header[20] = 0x30315844;  // 'DX10'

    header[26] = 0x1000 | 0x400000 | 0x8;  // TEXTURE | MIPMAP | COMPLEX

    uint32_t dx10[5] = {
        28,  // DXGI_FORMAT_R8G8B8A8_UNORM
        3,   // D3D12_RESOURCE_DIMENSION_TEXTURE2D
        0,   // misc_flags
        1,   // array_size
        0,   // misc_flags2
    };

    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(header), sizeof(header));
    f.write(reinterpret_cast<const char*>(dx10), sizeof(dx10));

    Image level = image;
    for (;;) {
      f.write(reinterpret_cast<const char*>(level.rgba.data()),
              std::streamsize(level.rgba.size()));
      if (level.width == 1 && level.height == 1) break;
      level = Halve(level);
    }
    if (!f) return false;
  }
  std::filesystem::rename(tmp, path, ec);
  return !ec;
}

void ClearBox(Image& dst, int x0, int y0, int x1, int y1) {
  x0 = std::clamp(x0, 0, dst.width - 1);
  x1 = std::clamp(x1, 0, dst.width - 1);
  y0 = std::clamp(y0, 0, dst.height - 1);
  y1 = std::clamp(y1, 0, dst.height - 1);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      uint8_t* p = &dst.rgba[(size_t(y) * dst.width + x) * 4];
      p[0] = p[1] = p[2] = p[3] = 0;
    }
  }
}

// The part of an image with any opacity (the glyph images have a transparent margin).
Image CropToContent(const Image& src) {
  int x0 = src.width, y0 = src.height, x1 = -1, y1 = -1;
  for (int y = 0; y < src.height; ++y) {
    for (int x = 0; x < src.width; ++x) {
      if (src.rgba[(size_t(y) * src.width + x) * 4 + 3] > 8) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < x0) return src;
  Image out;
  out.width = x1 - x0 + 1;
  out.height = y1 - y0 + 1;
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(&out.rgba[size_t(y) * out.width * 4],
                &src.rgba[(size_t(y + y0) * src.width + x0) * 4], size_t(out.width) * 4);
  }
  return out;
}

void Blit(Image& dst, const Image& src, int dx, int dy, int dw, int dh);

// Replaces one button of an atlas exactly where and as large as it is drawn: its solid body (alpha
// over 120, connected to (cx, cy) inside box) and its soft edge and shadow (3 pixels round it) are
// cleared, and the glyph's visible part is fitted into the body's bounds, centred. Anything further
// out is left alone, unless clear_box: then everything in box goes.
void ReplaceButtonInPlace(Image& dst, const Image& glyph, const int box[4], int cx, int cy,
                          bool clear_box) {
  const int bx0 = std::clamp(box[0], 0, dst.width - 1), by0 = std::clamp(box[1], 0, dst.height - 1);
  const int bx1 = std::clamp(box[2], 0, dst.width - 1), by1 = std::clamp(box[3], 0, dst.height - 1);
  auto alpha = [&](int x, int y) { return dst.rgba[(size_t(y) * dst.width + x) * 4 + 3]; };
  if (cx < bx0 || cx > bx1 || cy < by0 || cy > by1 || alpha(cx, cy) <= 120) return;
  std::vector<uint8_t> body(size_t(dst.width) * dst.height, 0);
  std::vector<std::pair<int, int>> stack{{cx, cy}};
  body[size_t(cy) * dst.width + cx] = 1;
  int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
  while (!stack.empty()) {
    const auto [x, y] = stack.back();
    stack.pop_back();
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
    const int nx[4] = {x + 1, x - 1, x, x};
    const int ny[4] = {y, y, y + 1, y - 1};
    for (int i = 0; i < 4; ++i) {
      if (nx[i] < bx0 || nx[i] > bx1 || ny[i] < by0 || ny[i] > by1) continue;
      uint8_t& seen = body[size_t(ny[i]) * dst.width + nx[i]];
      if (seen || alpha(nx[i], ny[i]) <= 120) continue;
      seen = 1;
      stack.push_back({nx[i], ny[i]});
    }
  }
  constexpr int kEdge = 3;
  for (int y = std::max(by0, y0 - kEdge); y <= std::min(by1, y1 + kEdge); ++y) {
    for (int x = std::max(bx0, x0 - kEdge); x <= std::min(bx1, x1 + kEdge); ++x) {
      bool near_body = false;
      for (int dy = -kEdge; dy <= kEdge && !near_body; ++dy) {
        for (int dx = -kEdge; dx <= kEdge && !near_body; ++dx) {
          const int sx = x + dx, sy = y + dy;
          if (dx * dx + dy * dy > kEdge * kEdge || sx < 0 || sy < 0 || sx >= dst.width ||
              sy >= dst.height) {
            continue;
          }
          near_body = body[size_t(sy) * dst.width + sx] != 0;
        }
      }
      if (!near_body) continue;
      uint8_t* p = &dst.rgba[(size_t(y) * dst.width + x) * 4];
      p[0] = p[1] = p[2] = p[3] = 0;
    }
  }
  if (clear_box) ClearBox(dst, bx0, by0, bx1, by1);
  const Image content = CropToContent(glyph);
  const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  const float scale = std::min(float(bw) / content.width, float(bh) / content.height);
  const int w = std::max(1, int(std::lround(content.width * scale)));
  const int h = std::max(1, int(std::lround(content.height * scale)));
  Blit(dst, content, x0 + (bw - w) / 2, y0 + (bh - h) / 2, w, h);
}

void Blit(Image& dst, const Image& src, int dx, int dy, int dw, int dh) {
  if (dw <= 0 || dh <= 0 || src.width <= 0 || src.height <= 0) return;
  for (int y = 0; y < dh; ++y) {
    const int ty = dy + y;
    if (ty < 0 || ty >= dst.height) continue;
    for (int x = 0; x < dw; ++x) {
      const int tx = dx + x;
      if (tx < 0 || tx >= dst.width) continue;

      const float fx0 = float(x) * src.width / float(dw);
      const float fx1 = float(x + 1) * src.width / float(dw);
      const float fy0 = float(y) * src.height / float(dh);
      const float fy1 = float(y + 1) * src.height / float(dh);

      float acc[4] = {0, 0, 0, 0}, weight = 0;
      for (int py = int(std::floor(fy0)); py < int(std::ceil(fy1)) && py < src.height; ++py) {
        for (int px = int(std::floor(fx0)); px < int(std::ceil(fx1)) && px < src.width; ++px) {
          const float w = (std::min(fx1, px + 1.0f) - std::max(fx0, float(px))) *
                          (std::min(fy1, py + 1.0f) - std::max(fy0, float(py)));
          if (w <= 0) continue;
          const uint8_t* p = &src.rgba[(size_t(py) * src.width + px) * 4];
          const float a = p[3] / 255.0f;
          acc[0] += w * p[0] * a;
          acc[1] += w * p[1] * a;
          acc[2] += w * p[2] * a;
          acc[3] += w * a;
          weight += w;
        }
      }
      if (weight <= 0) continue;
      const float sa = acc[3] / weight;
      if (sa <= 1e-4f) continue;
      const float src_rgb[3] = {acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3]};
      uint8_t* q = &dst.rgba[(size_t(ty) * dst.width + tx) * 4];
      const float da = q[3] / 255.0f;
      const float oa = sa + da * (1.0f - sa);
      for (int c = 0; c < 3; ++c) {
        const float v = (src_rgb[c] * sa + q[c] * da * (1.0f - sa)) / oa;
        q[c] = uint8_t(std::lround(std::clamp(v, 0.0f, 255.0f)));
      }
      q[3] = uint8_t(std::lround(oa * 255.0f));
    }
  }
}

std::string FirstKey(const std::string& list) {
  const std::string first = list.substr(0, list.find(','));
  const size_t a = first.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const size_t b = first.find_last_not_of(" \t\r\n");
  return first.substr(a, b - a + 1);
}

std::string ToLower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::filesystem::path FindDirectGlyphFile(const std::filesystem::path& assets_dir,
                                          const std::string& style,
                                          const std::string& name) {
  const std::string lower = ToLower(name);
  if (style == "xbox") {
    if (lower == "a") return assets_dir / "xbox" / "xbox_button_color_a.png";
    if (lower == "b") return assets_dir / "xbox" / "xbox_button_color_b.png";
    if (lower == "x") return assets_dir / "xbox" / "xbox_button_color_x.png";
    if (lower == "y") return assets_dir / "xbox" / "xbox_button_color_y.png";
    if (lower == "lb") return assets_dir / "xbox" / "xbox_lb.png";
    if (lower == "rb") return assets_dir / "xbox" / "xbox_rb.png";
    if (lower == "lt") return assets_dir / "xbox" / "xbox_lt.png";
    if (lower == "rt") return assets_dir / "xbox" / "xbox_rt.png";
    if (lower == "ls" || lower == "l3" || lower == "lspress") return assets_dir / "xbox" / "xbox_ls.png";
    if (lower == "rs" || lower == "r3" || lower == "rspress") return assets_dir / "xbox" / "xbox_rs.png";
    if (lower == "stickmove") return assets_dir / "xbox" / "xbox_stick_l.png";
    if (lower == "back") return assets_dir / "xbox" / "xbox_button_back.png";
    if (lower == "start") return assets_dir / "xbox" / "xbox_button_start.png";
    if (lower == "dpadup" || lower == "dpad_up") return assets_dir / "xbox" / "xbox_dpad_up.png";
    if (lower == "dpaddown" || lower == "dpad_down") return assets_dir / "xbox" / "xbox_dpad_down.png";
    if (lower == "dpadleft" || lower == "dpad_left") return assets_dir / "xbox" / "xbox_dpad_left.png";
    if (lower == "dpadright" || lower == "dpad_right") return assets_dir / "xbox" / "xbox_dpad_right.png";
    if (lower == "dpadupdown" || lower == "dpad_up_down") return assets_dir / "xbox" / "xbox_dpad_up_down.png";
    if (lower == "dpadleftright" || lower == "dpad_left_right") return assets_dir / "xbox" / "xbox_dpad_left_right.png";
    if (lower == "dpadall" || lower == "dpad_all") return assets_dir / "xbox" / "xbox_dpad_all.png";
  } else if (style == "playstation") {
    if (lower == "a") return assets_dir / "playstation" / "playstation_button_color_cross.png";
    if (lower == "b") return assets_dir / "playstation" / "playstation_button_color_circle.png";
    if (lower == "x") return assets_dir / "playstation" / "playstation_button_color_square.png";
    if (lower == "y") return assets_dir / "playstation" / "playstation_button_color_triangle.png";
    if (lower == "lb" || lower == "l1") return assets_dir / "playstation" / "playstation_trigger_l1_alternative.png";
    if (lower == "rb" || lower == "r1") return assets_dir / "playstation" / "playstation_trigger_r1_alternative.png";
    if (lower == "lt" || lower == "l2") return assets_dir / "playstation" / "playstation_trigger_l2.png";
    if (lower == "rt" || lower == "r2") return assets_dir / "playstation" / "playstation_trigger_r2.png";
    if (lower == "ls" || lower == "l3" || lower == "lspress") return assets_dir / "playstation" / "playstation_button_l3.png";
    if (lower == "rs" || lower == "r3" || lower == "rspress") return assets_dir / "playstation" / "playstation_button_r3.png";
    if (lower == "stickmove") return assets_dir / "playstation" / "playstation_stick_l.png";
    if (lower == "back" || lower == "create" || lower == "share") return assets_dir / "playstation" / "playstation5_button_create.png";
    if (lower == "start" || lower == "options") return assets_dir / "playstation" / "playstation5_button_options.png";
    if (lower == "dpadup" || lower == "dpad_up") return assets_dir / "playstation" / "playstation_dpad_up.png";
    if (lower == "dpaddown" || lower == "dpad_down") return assets_dir / "playstation" / "playstation_dpad_down.png";
    if (lower == "dpadleft" || lower == "dpad_left") return assets_dir / "playstation" / "playstation_dpad_left.png";
    if (lower == "dpadright" || lower == "dpad_right") return assets_dir / "playstation" / "playstation_dpad_right.png";
    if (lower == "dpadupdown" || lower == "dpad_up_down") return assets_dir / "playstation" / "playstation_dpad_up_down.png";
    if (lower == "dpadleftright" || lower == "dpad_left_right") return assets_dir / "playstation" / "playstation_dpad_left_right.png";
    if (lower == "dpadall" || lower == "dpad_all") return assets_dir / "playstation" / "playstation_dpad_all.png";
  } else if (style == "keyboard") {
    if (lower == "lmb" || lower == "mouse1" || lower == "mouse_left")
      return assets_dir / "keyboard" / "mouse_left.png";
    if (lower == "rmb" || lower == "mouse2" || lower == "mouse_right")
      return assets_dir / "keyboard" / "mouse_right.png";
    if (lower == "mmb" || lower == "mouse3" || lower == "mouse_middle")
      return assets_dir / "keyboard" / "mouse_scroll.png";
    if (lower == "mouse4" || lower == "mouse5" || lower == "mouse_side")
      return assets_dir / "keyboard" / "mouse_side.png";
    if (lower == "space") return assets_dir / "keyboard" / "keyboard_space.png";
    if (lower == "shift" || lower == "lshift" || lower == "rshift")
      return assets_dir / "keyboard" / "keyboard_shift.png";
    if (lower == "ctrl" || lower == "control" || lower == "lctrl" || lower == "rctrl")
      return assets_dir / "keyboard" / "keyboard_ctrl.png";
    if (lower == "alt" || lower == "lalt" || lower == "ralt")
      return assets_dir / "keyboard" / "keyboard_alt.png";
    if (lower == "tab") return assets_dir / "keyboard" / "keyboard_tab.png";
    if (lower == "return" || lower == "enter")
      return assets_dir / "keyboard" / "keyboard_enter.png";
    if (lower == "escape" || lower == "esc")
      return assets_dir / "keyboard" / "keyboard_escape.png";
    if (lower == "backspace")
      return assets_dir / "keyboard" / "keyboard_backspace.png";
    if (lower == "capslock")
      return assets_dir / "keyboard" / "keyboard_capslock.png";
    if (lower == "up") return assets_dir / "keyboard" / "keyboard_arrow_up.png";
    if (lower == "down") return assets_dir / "keyboard" / "keyboard_arrow_down.png";
    if (lower == "left") return assets_dir / "keyboard" / "keyboard_arrow_left.png";
    if (lower == "right") return assets_dir / "keyboard" / "keyboard_arrow_right.png";

    if (lower.size() == 1) {
      char c = lower[0];
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        std::string fn = "keyboard_";
        fn += c;
        fn += ".png";
        return assets_dir / "keyboard" / fn;
      }
    }

    std::filesystem::path extra = assets_dir / "keyboard" / ("keyboard_" + lower + ".png");
    std::error_code ec;
    if (std::filesystem::exists(extra, ec)) {
      return extra;
    }
  }
  return {};
}

std::filesystem::path FindGlyphFile(const std::filesystem::path& assets_dir,
                                    const std::string& variant, const std::string& button) {
  if (variant == "xbox" || variant == "playstation") {
    return FindDirectGlyphFile(assets_dir, variant, button);
  }

  if (variant == "keyboard") {
    std::string cvar;
    if (button == "A") cvar = "keybind_a";
    else if (button == "B") cvar = "keybind_b";
    else if (button == "X") cvar = "keybind_x";
    else if (button == "Y") cvar = "keybind_y";
    else if (button == "LB") cvar = "keybind_left_shoulder";
    else if (button == "RB") cvar = "keybind_right_shoulder";
    else if (button == "LT") cvar = "keybind_left_trigger";
    else if (button == "RT") cvar = "keybind_right_trigger";
    else if (button == "LS" || button == "LSPress") cvar = "keybind_lstick_press";
    else if (button == "RS" || button == "RSPress") cvar = "keybind_rstick_press";
    else if (button == "Back") cvar = "keybind_back";
    else if (button == "Start") cvar = "keybind_start";
    else if (button == "DpadUp") cvar = "keybind_dpad_up";
    else if (button == "DpadDown") cvar = "keybind_dpad_down";
    else if (button == "DpadLeft") cvar = "keybind_dpad_left";
    else if (button == "DpadRight") cvar = "keybind_dpad_right";
    else if (button == "StickMove") return assets_dir / "keyboard" / "mouse_move.png";
    else if (button == "DpadUpDown") return assets_dir / "keyboard" / "keyboard_arrow_up.png";
    else if (button == "DpadLeftRight") return assets_dir / "keyboard" / "keyboard_arrow_right.png";
    else if (button == "DpadAll") return assets_dir / "keyboard" / "keyboard_arrow_up.png";

    std::string key;
    if (!cvar.empty()) {
      key = ToLower(FirstKey(rex::cvar::Query<std::string>(cvar.c_str())));
      if (key.empty()) {
        if (button == "X") key = "e";
        else if (button == "A") key = "space";
        else if (button == "B") key = "f";
        else if (button == "Y") key = "q";
        else if (button == "LB") key = "c";
        else if (button == "RB") key = "r";
        else if (button == "LT") key = "rmb";
        else if (button == "RT") key = "lmb";
        else if (button == "RS" || button == "RSPress") key = "z";
        else if (button == "LS" || button == "LSPress") key = "shift";
        else if (button == "Back") key = "tab";
        else if (button == "Start") key = "return";
        else if (button == "DpadUp") key = "4";
        else if (button == "DpadDown") key = "3";
        else if (button == "DpadLeft") key = "1";
        else if (button == "DpadRight") key = "2";
      }
    } else {
      key = ToLower(button);
    }

    return FindDirectGlyphFile(assets_dir, "keyboard", key);
  }
  return {};
}

std::atomic<uint64_t> g_last_kbm_activity_ms{0};

}  // namespace

void NotifyKeyboardMouseInput() {
  g_last_kbm_activity_ms.store(rex::chrono::Clock::QueryHostUptimeMillis(),
                               std::memory_order_relaxed);
}

namespace {

class ButtonGlyphWindowListener : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent&) override { NotifyKeyboardMouseInput(); }
  void OnMouseDown(rex::ui::MouseEvent&) override { NotifyKeyboardMouseInput(); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    // Only count mouse motion past a few pixels so resting hand doesn't flip glyphs
    if (std::abs(e.dx()) > 3 || std::abs(e.dy()) > 3) {
      NotifyKeyboardMouseInput();
    }
  }
  void OnMouseWheel(rex::ui::MouseEvent&) override { NotifyKeyboardMouseInput(); }
};

ButtonGlyphWindowListener g_window_listener;

Image g_base512;
Image g_base256;
Image g_base_duel;

std::unordered_map<std::string, Image> g_glyph_cache;

bool LoadPng(const std::filesystem::path& path, Image* out) {
  auto it = g_glyph_cache.find(path.string());
  if (it != g_glyph_cache.end()) {
    *out = it->second;
    return true;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const size_t size = size_t(f.tellg());
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(size);
  if (!f.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))) return false;

  int w = 0, h = 0;
  std::vector<uint8_t> decoded = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), w, h);
  if (decoded.empty() || w <= 0 || h <= 0) return false;

  out->width = w;
  out->height = h;
  out->rgba = std::move(decoded);
  g_glyph_cache[path.string()] = *out;
  return true;
}

std::filesystem::path TexturesRoot() {
  const std::string override = rex::cvar::Query<std::string>("texture_pack_dir");
  return override.empty() ? rex::filesystem::GetExecutableFolder() / "textures"
                          : std::filesystem::path(override);
}

std::filesystem::path TitleDir() {
  return TexturesRoot() / "4D5308AB";
}

std::filesystem::path AssetsDir() {
  return rex::filesystem::GetExecutableFolder() / "assets" / "glyphs";
}

// The player's own atlases, captured by the GPU plugin the first time each one is drawn
// (texture_dump_hashes): the 256x256 one in the first menu, the 512x512 one only in a level.
// Each is used as soon as it exists, so the menus switch before a level has been loaded.
bool g_have512 = false;
bool g_have256 = false;
bool g_have_duel = false;  // only once a chainsaw duel has been seen

bool LoadBaseIfDumped(bool* have, Image* image, const char* hash, const char* size) {
  if (*have) return false;
  const std::filesystem::path dump_dir = TitleDir() / "dump";
  std::filesystem::path p = dump_dir / fmt::format("{}_bc3_{}.dds", hash, size);
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) p = dump_dir / fmt::format("{}.dds", hash);
  if (!std::filesystem::exists(p, ec) || !ReadDds(p, image)) return false;
  *have = true;
  REXLOG_INFO("ButtonGlyphs: base atlas {} loaded from {}", hash, p.string());
  return true;
}

// Loads the atlases that have been captured since the last call; true when one was added.
bool LoadNewBases() {
  bool added = LoadBaseIfDumped(&g_have512, &g_base512, "a000624a174e08ba", "512x512");
  added |= LoadBaseIfDumped(&g_have256, &g_base256, "ae5c68a9d7f7398c", "256x256");
  added |= LoadBaseIfDumped(&g_have_duel, &g_base_duel, "30d01cc6c7ff5cd3", "256x256");
  return added;
}

bool ComposeVariant(const std::string& variant_dir_name, const std::string& style) {
  if (!g_have512 && !g_have256 && !g_have_duel) return false;
  const std::filesystem::path assets = AssetsDir();
  const std::filesystem::path out_dir = TitleDir() / variant_dir_name;

  Image comp512 = g_base512;
  for (const auto& sp : kSprites512) {
    if (!g_have512) break;
    ClearBox(comp512, sp.box[0], sp.box[1], sp.box[2], sp.box[3]);
    std::filesystem::path gf = FindGlyphFile(assets, style, sp.button);
    Image glyph;
    if (!gf.empty() && LoadPng(gf, &glyph)) {
      Blit(comp512, glyph, sp.center[0] - sp.size / 2, sp.center[1] - sp.size / 2, sp.size,
           sp.size);
    }
  }

  Image comp256 = g_base256;
  for (const auto& sp : kSprites256) {
    if (!g_have256) break;
    ClearBox(comp256, sp.box[0], sp.box[1], sp.box[2], sp.box[3]);
    std::filesystem::path gf = FindGlyphFile(assets, style, sp.button);
    Image glyph;
    if (!gf.empty() && LoadPng(gf, &glyph)) {
      Blit(comp256, glyph, sp.center[0] - sp.size / 2, sp.center[1] - sp.size / 2, sp.size,
           sp.size);
    }
  }

  Image comp_duel = g_base_duel;
  for (const auto& sp : kSpritesDuel) {
    if (!g_have_duel) break;
    std::filesystem::path gf = FindGlyphFile(assets, style, sp.button);
    Image glyph;
    if (!gf.empty() && LoadPng(gf, &glyph)) {
      ReplaceButtonInPlace(comp_duel, glyph, sp.box, sp.center[0], sp.center[1], sp.size == 1);
    }
  }

  char name512[32], name256[32], name_duel[32];
  std::snprintf(name512, sizeof(name512), "%016llx.dds", static_cast<unsigned long long>(kAtlas512Hash));
  std::snprintf(name256, sizeof(name256), "%016llx.dds", static_cast<unsigned long long>(kAtlas256Hash));
  std::snprintf(name_duel, sizeof(name_duel), "%016llx.dds",
                static_cast<unsigned long long>(kAtlasDuelHash));

  char name256_menu[32];
  std::snprintf(name256_menu, sizeof(name256_menu), "%016llx.dds",
                static_cast<unsigned long long>(kAtlas256MenuHash));

  const bool ok1 = !g_have512 || WriteDds(out_dir / name512, comp512);
  const bool ok2 = !g_have256 || (WriteDds(out_dir / name256, comp256) &&
                                  WriteDds(out_dir / name256_menu, comp256));
  const bool ok3 = !g_have_duel || WriteDds(out_dir / name_duel, comp_duel);
  return ok1 && ok2 && ok3;
}

bool IsDdsValid(const std::filesystem::path& path, size_t expected_size) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(path, ec);
  return !ec && sz == expected_size;
}

void EnsureVariantOnDisk(const std::string& variant_dir_name, const std::string& style) {
  const std::filesystem::path dir = TitleDir() / variant_dir_name;
  char name512[32], name256[32];
  std::snprintf(name512, sizeof(name512), "%016llx.dds", static_cast<unsigned long long>(kAtlas512Hash));
  std::snprintf(name256, sizeof(name256), "%016llx.dds", static_cast<unsigned long long>(kAtlas256Hash));
  const std::filesystem::path p512 = dir / name512;
  const std::filesystem::path p256 = dir / name256;
  char name256_menu[32];
  std::snprintf(name256_menu, sizeof(name256_menu), "%016llx.dds",
                static_cast<unsigned long long>(kAtlas256MenuHash));
  const std::filesystem::path p256_menu = dir / name256_menu;
  char name_duel[32];
  std::snprintf(name_duel, sizeof(name_duel), "%016llx.dds",
                static_cast<unsigned long long>(kAtlasDuelHash));
  const std::filesystem::path p_duel = dir / name_duel;
  // A variant is composed again when the cell map or any glyph image it uses changes: the stamp
  // holds a layout version plus every cell's box and image file name.
  std::string want = fmt::format("9|512={}|256={}|duel={}", g_have512, g_have256, g_have_duel);
  const std::filesystem::path assets = AssetsDir();
  auto add_cells = [&](const auto& sprites) {
    for (const SpriteDef& sp : sprites) {
      want += fmt::format("|{},{},{},{}={}", sp.box[0], sp.box[1], sp.box[2], sp.box[3],
                          FindGlyphFile(assets, style, sp.button).filename().string());
    }
  };
  add_cells(kSprites512);
  add_cells(kSprites256);
  add_cells(kSpritesDuel);
  const std::filesystem::path stamp = dir / "layout.txt";
  std::string have;
  if (FILE* f = std::fopen(stamp.string().c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) have.append(buf, n);
    std::fclose(f);
  }
  if (have != want || (g_have512 && !IsDdsValid(p512, kExpectedSize512)) ||
      (g_have256 && (!IsDdsValid(p256, kExpectedSize256) ||
                     !IsDdsValid(p256_menu, kExpectedSize256))) ||
      (g_have_duel && !IsDdsValid(p_duel, kExpectedSize256))) {
    if (ComposeVariant(variant_dir_name, style)) {
      if (FILE* f = std::fopen(stamp.string().c_str(), "wb")) {
        std::fwrite(want.data(), 1, want.size(), f);
        std::fclose(f);
      }
    }
  }
}

std::string KeyboardSignature() {
  std::string sig;
  const char* cvars[] = {
      "keybind_x", "keybind_a", "keybind_b", "keybind_y",
      "keybind_left_shoulder", "keybind_right_shoulder",
      "keybind_left_trigger", "keybind_right_trigger",
      "keybind_lstick_press", "keybind_rstick_press",
      "keybind_back", "keybind_start",
      "keybind_dpad_up", "keybind_dpad_down",
      "keybind_dpad_left", "keybind_dpad_right"
  };
  for (const char* c : cvars) {
    sig += c;
    sig += '=';
    sig += rex::cvar::Query<std::string>(c);
    sig += ';';
  }
  return sig;
}

// Background worker state
std::atomic<bool> g_worker_running{false};
std::atomic<bool> g_variants_ready{false};
// Bumped when an atlas is added to the composed variants: the active variant is then reloaded
// (the GPU plugin scans a variant folder only when the variant name changes).
std::atomic<int> g_base_generation{0};
std::atomic<int> g_ready_kbd_slot{0};  // 0 -> replace_kbdA, 1 -> replace_kbdB
std::string g_composed_kbd_sig;
std::thread g_worker_thread;
std::mutex g_worker_mutex;
std::condition_variable g_worker_cv;

void ComposeAllVariants() {
  EnsureVariantOnDisk("replace_xbox", "xbox");
  EnsureVariantOnDisk("replace_playstation", "playstation");
}

void WorkerThreadFunc() {
  std::string last_sig;
  while (g_worker_running) {
    if (LoadNewBases()) {
      // A new atlas: compose every variant again, the keyboard one into the slot not in use.
      ComposeAllVariants();
      const std::string sig = KeyboardSignature();
      const bool first = !g_variants_ready.load(std::memory_order_acquire);
      const int slot = first ? 0 : 1 - g_ready_kbd_slot.load(std::memory_order_relaxed);
      if (ComposeVariant(slot == 0 ? "replace_kbdA" : "replace_kbdB", "keyboard")) {
        last_sig = sig;
        g_composed_kbd_sig = sig;
        g_ready_kbd_slot.store(slot, std::memory_order_release);
      }
      g_base_generation.fetch_add(1, std::memory_order_acq_rel);
      g_variants_ready.store(true, std::memory_order_release);
      REXLOG_INFO("ButtonGlyphs: variants composed (512x512: {}, 256x256: {}, duel: {})", g_have512,
                  g_have256, g_have_duel);
    } else if (g_variants_ready.load(std::memory_order_acquire)) {
      // Key binding changes, composed off the input thread into the slot not in use.
      const std::string current_sig = KeyboardSignature();
      if (current_sig != last_sig) {
        const int next_slot = 1 - g_ready_kbd_slot.load(std::memory_order_relaxed);
        const std::string slot_name = next_slot == 0 ? "replace_kbdA" : "replace_kbdB";
        if (ComposeVariant(slot_name, "keyboard")) {
          last_sig = current_sig;
          g_composed_kbd_sig = current_sig;
          g_ready_kbd_slot.store(next_slot, std::memory_order_release);
          REXLOG_INFO("ButtonGlyphs: off-thread re-composed keyboard atlas into {}", slot_name);
        }
      }
    }
    std::unique_lock<std::mutex> lock(g_worker_mutex);
    g_worker_cv.wait_for(lock, std::chrono::milliseconds(200),
                         [] { return !g_worker_running.load(); });
  }
}

std::string g_active_variant = "initial";
std::chrono::steady_clock::time_point g_next_poll{};

}  // namespace

// Texture replacement plus a one-time dump of the player's own two prompt atlases. Called before
// setup and again after it: the settings belong to the GPU plugin, and setting one before the
// plugin has registered it is rejected (a fresh install then never captured the atlases).
static void EnableAtlasCapture(const char* when) {
  const bool replace = rex::cvar::SetFlagByName("texture_replace", "true");
  const bool dump = rex::cvar::SetFlagByName("texture_dump_hashes", kAtlasDumpHashes);
  REXLOG_INFO("ButtonGlyphs: texture capture settings at {}: replace={} dump_hashes={}", when,
              replace, dump);
}

void PrepareButtonGlyphs() {
  const std::string mode = rex::cvar::Query<std::string>("gow3_button_prompts");
  if (mode == "original") {
    // Leave texture_replace off so runtime does not hash guest texture uploads
    return;
  }
#ifdef _WIN32
  char* env_buf = nullptr;
  size_t env_len = 0;
  if (_dupenv_s(&env_buf, &env_len, "REX_TEXTURE_REPLACE") == 0) {
    if (!env_buf) {
      _putenv_s("REX_TEXTURE_REPLACE", "true");
    } else {
      std::free(env_buf);
    }
  }
#endif
  EnableAtlasCapture("pre-setup");
}

void InstallButtonGlyphs(rex::ui::Window* window) {
  if (rex::cvar::Query<std::string>("gow3_button_prompts") != "original") {
    EnableAtlasCapture("post-setup");
  }
  if (window) {
    window->AddInputListener(&g_window_listener, 1);
  }
  g_worker_running = true;
  g_worker_thread = std::thread(WorkerThreadFunc);
  REXLOG_INFO("ButtonGlyphs: installed input listener and started background worker thread");
}

void ShutdownButtonGlyphs() {
  g_worker_running = false;
  g_worker_cv.notify_all();
  if (g_worker_thread.joinable()) {
    g_worker_thread.join();
  }
}

void PollButtonPrompts() {
  const auto now = std::chrono::steady_clock::now();
  if (now < g_next_poll) return;
  g_next_poll = now + std::chrono::milliseconds(50);

  if (!g_variants_ready.load(std::memory_order_acquire)) {
    return;
  }

  uint64_t pad_time = 0;
  int pad_type = 0;
#ifdef _WIN32
  typedef uint64_t (*GetPadActivityFn)(int*);
  static GetPadActivityFn s_get_pad_activity = []() -> GetPadActivityFn {
    HMODULE mod = GetModuleHandleA("rexruntime.dll");
    if (!mod) {
      mod = LoadLibraryA("rexruntime.dll");
    }
    return mod ? reinterpret_cast<GetPadActivityFn>(GetProcAddress(mod, "rex_input_get_last_physical_activity")) : nullptr;
  }();
  if (s_get_pad_activity) {
    pad_time = s_get_pad_activity(&pad_type);
  }
#endif
  const uint64_t kbm_time = g_last_kbm_activity_ms.load(std::memory_order_relaxed);

  const std::string mode = rex::cvar::Query<std::string>("gow3_button_prompts");
  std::string target_style = "original";

  if (mode == "original") {
    target_style = "original";
  } else if (mode == "xbox") {
    target_style = "xbox";
  } else if (mode == "playstation") {
    target_style = "playstation";
  } else if (mode == "keyboard") {
    target_style = "keyboard";
  } else {
    // "auto" mode
    if (pad_time > 0 && pad_time >= kbm_time) {
      // SDL types PS3/PS4/PS5 are 4, 5, 6
      const bool is_playstation = (pad_type == 4 || pad_type == 5 || pad_type == 6);
      target_style = is_playstation ? "playstation" : "xbox";
    } else {
      target_style = "keyboard";
    }
  }

  std::string target_variant;
  if (target_style == "original") {
    target_variant = "";
  } else if (target_style == "xbox") {
    target_variant = "xbox";
  } else if (target_style == "playstation") {
    target_variant = "playstation";
  } else if (target_style == "keyboard") {
    int slot = g_ready_kbd_slot.load(std::memory_order_acquire);
    target_variant = (slot == 0) ? "kbdA" : "kbdB";
    if (KeyboardSignature() != g_composed_kbd_sig) {
      g_worker_cv.notify_one();
    }
  }

  // An atlas was added to the variant folders: drop the variant for one poll so the GPU plugin
  // scans the folder again when it comes back.
  static int seen_generation = 0;
  const int generation = g_base_generation.load(std::memory_order_acquire);
  if (generation != seen_generation) {
    seen_generation = generation;
    if (!g_active_variant.empty() && g_active_variant != "initial" &&
        target_variant == g_active_variant) {
      g_active_variant = "";
      rex::cvar::SetFlagByName("texture_replace_variant", "");
      return;
    }
  }

  if (target_variant != g_active_variant) {
    g_active_variant = target_variant;
    rex::cvar::SetFlagByName("texture_replace_variant", target_variant);
    REXLOG_INFO("ButtonGlyphs: variant set to '{}' (mode '{}')", target_variant, mode);
  }
}

std::filesystem::path FindGlyphFile(const std::string& style, const std::string& button_or_key) {
  return FindDirectGlyphFile(AssetsDir(), style, button_or_key);
}

std::string GetActiveControllerGlyphStyle() {
  const std::string mode = rex::cvar::Query<std::string>("gow3_button_prompts");
  if (mode == "playstation") {
    return "playstation";
  }
  if (mode == "xbox") {
    return "xbox";
  }
  if (mode == "auto") {
    int pad_type = 0;
#ifdef _WIN32
    typedef uint64_t (*GetPadActivityFn)(int*);
    static GetPadActivityFn s_get_pad_activity = []() -> GetPadActivityFn {
      HMODULE mod = GetModuleHandleA("rexruntime.dll");
      if (!mod) {
        mod = LoadLibraryA("rexruntime.dll");
      }
      return mod ? reinterpret_cast<GetPadActivityFn>(
                       GetProcAddress(mod, "rex_input_get_last_physical_activity"))
                 : nullptr;
    }();
    if (s_get_pad_activity) {
      (void)s_get_pad_activity(&pad_type);
    }
#endif
    if (pad_type == 4 || pad_type == 5 || pad_type == 6) {
      return "playstation";
    }
  }
  return "xbox";
}

void NotifyBindingsChanged() {
  g_worker_cv.notify_all();
}

}  // namespace rex::glue
