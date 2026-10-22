# Third-party notices

## gow2-recomp (Gears of War 2 PC port)

- Project: [santiquiroz/gears-of-war-2-pc-port](https://github.com/santiquiroz/gears-of-war-2-pc-port)
- Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
- License: BSD 3-Clause (text below)

Files derived from it:

- `tools/harvest_hints.py`: adapted from its `tools/harvest_hints.py` (file names changed for gow3).
- `scripts/build-sdk.ps1`, `scripts/sdk-source.ps1`, `scripts/sdk-symlinks.ps1`, `scripts/ngx-scan.ps1` and `sdk/patches/0002`-`0029`: copied from it.
- `scripts/build.ps1`, `scripts/harvest-loop.ps1`: modelled on its `scripts/build.ps1` and `scripts/run-harvest-loop.sh`.

## gears-judgement-recomp

- Project: [OverkillLabs3/gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp)
- Copyright (c) 2026, the contributors to this project
- License: BSD 3-Clause (text below)

Files derived from it (by way of the gow2-recomp copy):

- `tools/patch_switch_traps.py`: copied, file names changed for gow3.
- `src/gow3_xam_input_guard.cpp`, `src/gow3_xam_input_guard.h`: copied from gow2-recomp's `src/gow2_xam_input_guard.*` (adapted from gears-judgement-recomp); its test-input hook removed.
- `src/gow3_utf8_guard.cpp`, `src/gow3_utf8_guard.h`: copied from gow2-recomp's `src/gow2_utf8_guard.*` (themselves adapted from gears-judgement-recomp), names changed for gow3.

## BSD 3-Clause License text (applies to both projects above, each under its own copyright line)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Xenia (canary)

- Project: [xenia-canary/xenia-canary](https://github.com/xenia-canary/xenia-canary), BSD 3-Clause.
- `sdk/patches/0030-scalar-swizzle-three-source.patch` ports its commit 92ada8ebc0 to the ReXGlue SDK.
