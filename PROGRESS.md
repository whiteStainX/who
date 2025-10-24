# PROGRESS

## Phase 0 – Setup ✅
- [x] Repository initialized with appropriate `.gitignore` rules for CMake and common toolchains.
- [x] `CMakeLists.txt` configures the `who` executable, integrates kissfft sources, and locates notcurses.
- [x] Vendor drops for `miniaudio` and `kissfft` added under `external/`.
- [x] Minimal notcurses-based `main.cpp` builds, opens a blank terminal window, and exits cleanly.

## Next Focus
- Phase 1: implement the static grid renderer and animation loop (no audio input yet).
