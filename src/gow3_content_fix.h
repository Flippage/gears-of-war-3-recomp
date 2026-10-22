// gow3 - ReXGlue Recompiled Project
//
// Keeps the save-device root names the guest passes to XamContentCreateEx / XamContentClose
// alive until the asynchronous call reads them. See gow3_content_fix.cpp.

#pragma once

namespace rex::glue {

// Hooks the exports. Call before any guest code runs (OnPreSetup).
void InstallContentFix();
// Allocates the guest buffers. Call once guest memory exists (OnPostLoadXexImage).
void AllocateContentFixRing();

}  // namespace rex::glue
