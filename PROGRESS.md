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

## Phase 3 – DSP Engine ✅

- [x] Accumulate captured audio into Hann-windowed frames with 50% overlap.
- [x] Execute kissfft on each frame to obtain complex spectra.
- [x] Collapse bins into 16 logarithmically spaced bands with attack/release smoothing.
- [x] Surface the real-time band meter inside the renderer for verification.

## Phase 3.5 – File Streaming Input ✅

- [x] Added an optional file-backed audio source using `ma_decoder` to feed the capture ring buffer.
- [x] Downmixed decoded frames to mono, resampled mismatched rates to 48 kHz, and paced delivery to mimic live capture.
- [x] Integrated the streaming path with existing metrics and visualization overlays.

## Next Focus

- Phase 4: map smoothed band energies to the grid colors and brightness for a reactive display.
