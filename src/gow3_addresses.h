// gow3 - ReXGlue Recompiled Project
//
// Game addresses our code uses, per executable: the disc's (base, 0.0.0.11) and Title Update 6's
// (GOW3_TU6, built with scripts\build.ps1 -Game tu6). TU6 relinks the whole executable, so every
// address moves; code addresses were found with tools\map_tu6_addresses.py and checked by hand.
// TU6 data addresses come from its loaded image (GOW3_DUMP_IMAGE): strings and vtables by content,
// globals through the instructions that build them in matched functions. The post-codegen patch
// sites are in tools\patch_game_patches.py (TU6_ADDRESSES).

#pragma once

#include <cstdint>

namespace rex::glue::addr {

#if defined(GOW3_TU6)
inline constexpr bool kTu6 = true;
// Mouse aim (gow3_mouse_aim.cpp).
inline constexpr uint32_t kInputAxisFn = 0x8274CC98;        // UPlayerInput::InputAxis
inline constexpr uint32_t kControllerAxisFn = 0x82625960;   // controller axis handler
inline constexpr uint32_t kViewportAxisFn = 0x82625EC8;     // viewport client InputAxis
inline constexpr uint32_t kInputAxisSlot = 0x82189F1C;      // vtable 82189B50 entry 243, as on the disc
inline constexpr uint32_t kControllerAxisSlot = 0x82162264; // vtable entry of kControllerAxisFn
inline constexpr uint32_t kMouseXName = 0x83454898;         // FName "MouseX" global
inline constexpr uint32_t kMouseYName = 0x834548A0;         // FName "MouseY" global
inline constexpr uint32_t kMouseXString = 0x821471D0;       // L"MouseX"
inline constexpr uint32_t kEnginePtr = 0x8343987C;          // GEngine
// Unlocks (gow3_unlocks.cpp).
inline constexpr uint32_t kExecGetProfileSettingValueInt = 0x8252ABC8;
inline constexpr uint32_t kGearProfileSettingsVtable = 0x82245848;
// Coalesced hash table (gow3_coalesced.cpp).
inline constexpr uint32_t kHashSecBegin = 0x83610000;
inline constexpr uint32_t kHashSecEnd = 0x836107AF;
// Loaded image (the GOW3_DUMP_IMAGE dev tool).
inline constexpr uint32_t kImageBase = 0x82000000;
inline constexpr uint32_t kImageEnd = 0x83620000;
#else
inline constexpr bool kTu6 = false;
inline constexpr uint32_t kInputAxisFn = 0x8273B518;
inline constexpr uint32_t kControllerAxisFn = 0x826149D0;
inline constexpr uint32_t kViewportAxisFn = 0x82614F38;
inline constexpr uint32_t kInputAxisSlot = 0x821897A4;
inline constexpr uint32_t kControllerAxisSlot = 0x82161A44;
inline constexpr uint32_t kMouseXName = 0x834440C8;
inline constexpr uint32_t kMouseYName = 0x834440D0;
inline constexpr uint32_t kMouseXString = 0x82146AA8;
inline constexpr uint32_t kEnginePtr = 0x834290BC;
inline constexpr uint32_t kExecGetProfileSettingValueInt = 0x8251A2B0;
inline constexpr uint32_t kGearProfileSettingsVtable = 0x82247140;
inline constexpr uint32_t kHashSecBegin = 0x835A0000;
inline constexpr uint32_t kHashSecEnd = 0x835A07AF;
inline constexpr uint32_t kImageBase = 0x82000000;
inline constexpr uint32_t kImageEnd = 0x835B0000;
#endif

}  // namespace rex::glue::addr
