# DESIGN.md

## Project Overview

**Name:** who  
**Goal:** A pure-terminal visualizer that reacts to real-time audio, creating colorful illusive patterns. Lean, fast, and extendable without over-engineering.

---

## Stack & Tools

| Component        | Choice                                                     | Rationale                                               |
| ---------------- | ---------------------------------------------------------- | ------------------------------------------------------- |
| **Language**     | Modern C++ (C++20)                                         | Performance, control, and lean footprint                |
| **Build System** | CMake                                                      | Standardized build and easy extension                   |
| **Audio Input**  | [miniaudio](https://github.com/mackron/miniaudio)          | Single-header, cross-platform, minimal latency          |
| **FFT**          | [kissfft](https://github.com/mborgerding/kissfft) or FFTW3 | Tiny and fast (kissfft); FFTW3 optional for performance |
| **Renderer**     | [notcurses](https://github.com/dankamongmen/notcurses)     | Truecolor terminal graphics with smooth animation       |
| **Threading**    | std::thread + atomic/ring buffer                           | Clear separation between capture, DSP, and render       |

---

## Architecture Overview

```
(miniaudio callback)
     ↓
RingBuffer<float>
     ↓
DSP Engine (FFT, smoothing, gain)
     ↓
Visual Mapper (brightness, hue logic)
     ↓
Renderer (notcurses)
```

### Audio Input Sources

- **Live Capture (default):** Uses `ma_device` to record stereo 48 kHz input from the system microphone, writing directly into a lock-free ring buffer.
- **File Stream (`--file <path>`):** Uses `ma_decoder` to read compressed/PCM assets, downmixes to mono, resamples to 48 kHz with `ma_resampler`, and paces chunks to emulate real-time capture before feeding the same ring buffer.

### Thread Roles

- **Audio Thread:** Captures frames, writes to a lock-free buffer.
- **DSP Thread:** Reads samples, runs FFT, computes band energies and normalization.
- **Render Thread:** Pulls band data, generates grid visuals, and outputs to terminal.

---

## Core Loop Logic

**Audio:**

- Callback size: 256 frames @ 48kHz
- FFT window: 1024 samples (hop 512)
- Hann window + normalization
- Bands: 16 log-spaced (20 Hz–16 kHz)
- Smoothing: EMA (α ≈ 0.25)

**Render:**

- Target FPS: 60
- Use notcurses for flicker-free double-buffering
- Diff only changed cells for efficiency

---

## Visual Mapping Model

### Grid

- Default: 16×16, adjustable at runtime (8–32×32)
- Each cell = a color square with gap spacing

### Color & Brightness

| Parameter          | Source                     | Meaning                      |
| ------------------ | -------------------------- | ---------------------------- |
| **Brightness (L)** | Band energy + wave + noise | Light intensity per cell     |
| **Hue (H)**        | Band index → color wheel   | Tone across frequencies      |
| **Saturation (S)** | Spectral flux (transients) | Boosts during beats          |
| **AGC (gain)**     | RMS normalization          | Prevents clipping or dimming |

**Formula:**

```
L = clamp(k1*E_band + k2*sin(ωt + φ*c) + k3*noise(r,c,t), 0, 1)
H = 360 * band_index / num_bands
S = 0.4 + 0.6 * normalized_flux
```

---

## Modes (Foundation Placeholders)

1. **Mode A (Band Rows):** Each row = frequency band, columns = wave phase.
2. **Mode B (Radial):** Center = bass, edges = treble.
3. **Mode C (Trail):** Columns show temporal history (spectrogram-like).

---

## Control Keys

| Key   | Action             |
| ----- | ------------------ |
| ↑ / ↓ | Adjust rows        |
| ← / → | Adjust columns     |
| m     | Switch mode        |
| q     | Quit               |
| [ / ] | Adjust sensitivity |

---

## Problems to Address

### 1. Real-Time Audio Handling

- Latency control via small callback size.
- Ring buffer ensures decoupling.
- Thread-safe, no blocking or heap allocations.

### 2. Visualization Mapping

- Mapping of FFT bands to grid positions.
- Dynamic normalization (AGC).
- Transition smoothing for fluid visuals.

### 3. Performance and Stability

- Avoid `std::cout` in hot loops.
- Diff rendering to reduce redraw load.
- Proper cleanup on exit (restore terminal).

---

## Next Steps

1. **Step 1:** Implement the static grid + animation timer (no audio).
2. **Step 2:** Integrate audio capture → print FFT magnitude debug.
3. **Step 3:** Wire in band smoothing + brightness map.
4. **Step 4:** Add runtime controls and mode toggles.
5. **Step 5:** Optimize rendering + diffing.

---

## Outcome

A lean, portable, terminal-native "toy" visualizer ready to expand — strong foundations, no unnecessary complexity.
