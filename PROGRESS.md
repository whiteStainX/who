# PROGRESS

## Phase 0 – Setup ✅

- [x] Repository initialized with appropriate `.gitignore` rules for CMake and common toolchains.
- [x] `CMakeLists.txt` configures the `who` executable, integrates kissfft sources, and locates notcurses.
- [x] Vendor drops for `miniaudio` and `kissfft` added under `external/`.
- [x] Minimal notcurses-based `main.cpp` builds, opens a blank terminal window, and exits cleanly.

## Phase 1 – Core Loop Prototype ✅

- [x] Initialize notcurses and render a centered 16×16 grid using background-color blocks.
- [x] Animate brightness and hue over time with smooth sine-based motion.
- [x] Maintain a steady ~60 FPS loop with sleep-based pacing.
- [x] Poll input to quit on `q`/`Q` and respond immediately to terminal resize events.

## Phase 1.5 – Visual Refinements ✅

- [x] Add consistent gaps between grid cells to match the reference styling.
- [x] Enforce square cell geometry that adapts to the terminal's limiting dimension.

## Phase 2 – Audio Input ✅

- [x] Integrate `miniaudio` capture at 48 kHz stereo.
- [x] Stream samples into a single-producer/single-consumer ring buffer with drop tracking.
- [x] Consume audio in the render loop to compute RMS/peak metrics and display live debug info.

## Next Focus

- Phase 3: build the DSP engine that windows buffered audio, runs FFT via kissfft, and produces smoothed band energies for the renderer.
