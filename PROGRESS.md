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

## Phase 4 – Reactive Visual Mapping ✅

- [x] Projected log-spaced band energies onto grid rows so each stripe responds to its frequency range.
- [x] Applied adaptive gain control and logarithmic scaling to keep brightness responsive across input levels.
- [x] Blended temporal waves with per-band saturation for fluid, audio-driven color motion.

## Phase 4.5 – Loop Modularization ✅

- [x] Extracted audio capture/stream control into `AudioEngine` so input handling is isolated from the main loop.
- [x] Moved rendering math into a dedicated `renderer` module for cleaner visual logic ownership.
- [x] Slimmed `main.cpp` to configuration and orchestration responsibilities to keep future phases maintainable.

## Phase 5 – Runtime Controls & Modes ✅

- [x] Hooked arrow-key handlers to resize the grid dynamically between 8×8 and 32×32 without restarting.
- [x] Added a mode toggle cycling across band stripes, radial bloom, and time-trail visualizations.
- [x] Introduced adjustable sensitivity to fine-tune brightness response for quiet or loud sources.

## Phase 6 – Optimization & Refinement ✅

- [x] Added a persistent grid cache that only redraws cells whose colors change, slashing terminal writes per frame.
- [x] Reused precomputed fill buffers and guaranteed a minimum draw height so tight layouts still render crisply.
- [x] Cleared and repainted overlay lines incrementally to avoid stale text while keeping the main grid diff-based.

## Phase 7 – Extensions ✅

- [x] Introduced selectable rainbow and warm/cool palettes with smoothing to ease transitions between color states.
- [x] Implemented a spectral-flux beat detector that drives reactive brightness pops and on-screen metrics.
- [x] Enhanced radial and trail visual modes with palette-aware hues and beat-influenced motion.

## Next Focus

- Phase 8: configuration scaffolding and plug-in hooks to prepare for scaling.
