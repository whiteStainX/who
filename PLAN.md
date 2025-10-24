# PLAN.md

## Project: who

**Goal:** Build a lean, modern C++ terminal-based music visualizer that reacts to audio in real time using colored squares arranged in a grid. The foundation must be solid, modular, and easily extensible. The first iterations should be minimal yet “playable.”

---

## Phase Overview

| Phase | Name                  | Objective                                            | Output                            |
| ----- | --------------------- | ---------------------------------------------------- | --------------------------------- |
| **0** | Setup                 | Prepare repo, CMake structure, dependencies          | Empty skeleton builds cleanly     |
| **1** | Core Loop Prototype   | Create static grid rendering (no audio)              | Animated grid placeholder         |
| **2** | Audio Input           | Integrate miniaudio and verify capture stream        | Print FFT magnitudes to terminal  |
| **3** | DSP Engine            | Implement FFT and band smoothing                     | Band energy array ready per frame |
| **4** | Visual Mapping        | Map band energies to colors & brightness             | Animated reactive grid            |
| **5** | Runtime Controls      | Keyboard controls for grid size/modes                | Interactive terminal control      |
| **6** | Optimization & Refine | Diff rendering, performance tuning                   | Stable 60 FPS                     |
| **7** | Extensions            | Add palettes, beat detection, mode cycling           | “Playable toy”                    |
| **8** | Scaling               | Move toward polished version (config files, plugins) | Ready for future development      |

---

## Repository Structure

```
who/
├── CMakeLists.txt
├── README.md
├── PLAN.md
├── DESIGN.md
├── external/
│   ├── miniaudio/
│   │   └── miniaudio.h
│   ├── kissfft/
│   │   ├── kiss_fft.h
│   │   └── kiss_fft.c
│   └── notcurses/ (installed via system pkg)
├── src/
│   ├── main.cpp
│   ├── audio.cpp
│   ├── audio.h
│   ├── dsp.cpp
│   ├── dsp.h
│   ├── visual.cpp
│   ├── visual.h
│   ├── render.cpp
│   ├── render.h
│   ├── ringbuffer.h
│   └── util.cpp / util.h
└── build/ (generated)
```

**Build system:**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./who
```

---

## Phase Details

### **Phase 0 – Setup**

**Goal:** Get repo ready and confirm toolchain.  
**Tasks:**

1. Initialize git repo with `.gitignore` for C++, CMake, and macOS.
2. Add CMakeLists.txt with target `who`.
3. Add dependencies:
   - Copy `miniaudio.h` into `external/`.
   - Copy `kissfft` minimal C sources.
   - Install `notcurses` via Homebrew or apt.
4. Confirm a “Hello Terminal” compiles and runs.

**Output:** Clean build producing an empty terminal window.

---

### **Phase 1 – Core Loop Prototype**

**Goal:** Render a static grid that animates over time without audio.  
**Tasks:**

- Initialize notcurses.
- Draw a configurable grid (e.g. 16×16).
- Implement timing loop (60 FPS).
- Use pseudo-random noise or sine to animate brightness.
- Cleanly handle resize & quit (`q` key).

**Output:** Visually stable grid animation.

---

### **Phase 2 – Audio Input**

**Goal:** Connect to microphone and confirm real-time audio capture.  
**Tasks:**

- Integrate `miniaudio`.
- Capture stream (float 32-bit, 48 kHz).
- Push samples into a single-producer-single-consumer ring buffer.
- Print RMS and peak debug to confirm live capture.

**Output:** Numeric data from live audio.

---

### **Phase 3 – DSP Engine**

**Goal:** Convert captured samples into frequency domain data.  
**Tasks:**

- Implement FFT with `kissfft`.
- Apply Hann window.
- Aggregate into 16 log-spaced frequency bands.
- Apply exponential moving average (EMA) for smoothing.
- Normalize per-frame band energies (AGC).

**Output:** `Bands16` struct with normalized values per frame.

---

### **Phase 4 – Visual Mapping**

**Goal:** Connect DSP output to visual grid.  
**Tasks:**

- Map each band to one or multiple rows.
- Compute per-cell brightness based on band energy + wave motion.
- Assign hues by band index.
- Convert HSL → RGB for display.
- Render via notcurses at 30–60 FPS.

**Output:** Real-time color animation responding to audio.

---

### **Phase 5 – Runtime Controls**

**Goal:** Add keyboard interaction.  
**Tasks:**

- Arrow keys adjust grid size.
- `m` cycles visual modes.
- `[ / ]` modify sensitivity (AGC target).
- `q` quits gracefully.

**Output:** Interactive visualizer.

---

### **Phase 6 – Optimization & Refinement**

**Goal:** Make rendering efficient and stable.  
**Tasks:**

- Implement double-buffer diff rendering.
- Replace math-heavy noise with precomputed sine table.
- Tune timing loop to avoid drift.
- Reduce terminal writes by batching.

**Output:** Stable ~60 FPS with low CPU usage.

---

### **Phase 7 – Extensions (Toy Complete)**

**Goal:** Reach the “toy playable” milestone.  
**Tasks:**

- Add color palettes (rainbow, warm-cool).
- Add simple beat pop (flux threshold).
- Add radial and trail modes (basic versions).
- Smooth transitions between color states.

**Output:** Playable, minimal, beautiful foundation.

---

### **Phase 8 – Scaling**

**Goal:** Prepare for future serious expansion.  
**Ideas:**

- Config file (`who.toml`) for persistent settings.
- Plugin-like system for custom visual modes.
- Cross-platform (Windows, macOS, Linux).
- Audio file input (offline visualization).

**Output:** Extensible framework for terminal art.

---

## Development Guidelines

- Keep **no external dependencies** beyond the three core libs.
- Maintain **clean separation** between capture, DSP, and render.
- Zero heap allocation inside loops (use preallocated buffers).
- Every frame must complete under 16ms (for 60 FPS).
- Keep a **toggle for debug logging** (`--debug`).

---

## Git Workflow

| Branch            | Purpose                      |
| ----------------- | ---------------------------- |
| `main`            | Stable, tagged versions      |
| `dev`             | Active development           |
| `feature/<topic>` | Short-lived feature branches |
| `hotfix/<issue>`  | Urgent patches               |

**Standard workflow:**

```bash
git checkout -b feature/grid
# develop
git commit -m "Add grid rendering prototype"
git push origin feature/grid
git merge feature/grid --no-ff
```

---

## Milestones & Deliverables

| Milestone | Description                        | ETA     |
| --------- | ---------------------------------- | ------- |
| **M0**    | Project scaffolding builds cleanly | Day 1   |
| **M1**    | Static grid demo                   | Day 2   |
| **M2**    | Live audio capture verified        | Day 3–4 |
| **M3**    | Audio-reactive colors              | Day 6   |
| **M4**    | Interactive controls               | Day 7–8 |
| **M5**    | Toy playable & optimized           | Day 10  |

---

## Long-Term Vision

who should feel like a **drum pad come alive in the terminal**, responding smoothly to any audio source. From this foundation, future expansions (spectral effects, physics-inspired motion, multi-view layouts) will build naturally—without rewriting the core loop.

---
