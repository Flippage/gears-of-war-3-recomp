// gow3 - ReXGlue Recompiled Project
//
// Dev tool: logs the guest's save/content, device and sign-in XAM calls (GOW3_XAM_TRACE=1).
// See gow3_xam_trace.cpp.

#pragma once

namespace rex::glue {

// No-op unless GOW3_XAM_TRACE=1. Call before any guest code runs.
void InstallXamTrace();

}  // namespace rex::glue
