# who

A lean, modern C++ terminal-based audio visualizer. The current milestone includes a working notcurses renderer, real-time audio capture, FFT-based band analysis, and an optional file-streaming path for environments without live recording capability.

## Prerequisites

Ensure the following tools are available on your system:

- A C++20-compatible compiler (e.g., `g++-10` or newer, `clang++-12` or newer)
- [CMake](https://cmake.org/) 3.16 or later
- [notcurses](https://github.com/dankamongmen/notcurses) development libraries
  - Ubuntu/Debian: `sudo apt install libnotcurses-dev`
  - macOS (Homebrew): `brew install notcurses`

The `external/` directory already includes the single-header/minimal sources for `miniaudio` and `kissfft` used in later phases.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

After a successful build, run the executable from the repository root:

```bash
./build/who [--config path/to/who.toml] [--file path/to/audio.wav] [--system] [--mic] [--device "name"]
```

Running without flags opens the real-time capture path (requires microphone permissions). Supplying `--file` (or `-f`) streams audio from disk through the same DSP chain. Supported formats depend on miniaudio's decoder (WAV/MP3/FLAC and more). The file path option downmixes to mono, resamples to 48 kHz, and feeds the visualizer at real-time speed so you can test the visualization without capture hardware. Use `--config` (or `-c`) to load an alternate TOML configuration. The new capture switches behave as follows:

- `--system`: Request loopback/system audio capture (platform specific requirements below).
- `--mic`: Force microphone capture even if the configuration enables system capture.
- `--device "name"`: Lock capture to a specific device label reported by miniaudio (case-insensitive substring match). Combine with `--system` when you want a non-default loopback/monitor source.

You can set the same preferences persistently through `[audio.capture]` in `who.toml` (`device = "..."`, `system = true`).

### System audio capture

To visualise only what the system is playing (Spotify, YouTube, games, etc.) configure per platform:

- **Windows**: `--system` uses WASAPI loopback on the default playback device—no extra drivers needed.
- **macOS**:
  1. Install the free virtual driver: `brew install --cask blackhole-2ch`.
  2. In *Audio MIDI Setup*, create a *Multi-Output Device* that contains both **BlackHole 2ch** and your speakers.
  3. Set the Multi-Output Device as the system output, then run `./build/who --system` (miniaudio will expose “BlackHole” as an input device).
  > For advanced setup with specific terminals like `cool-retro-term`, see `MacOS.md`.
- **Linux (PulseAudio/PipeWire)**: `--system` auto-selects the default sink monitor (name ends with `.monitor`). To pick another source, list them via `pactl list sources short` and pass `--device <monitor name>`.

If the helper cannot locate the required loopback/monitor device, the program prints guidance on how to fix the environment.

## Controls

Interact with the visualizer while it is running:

- `q`/`Q`: Quit the program immediately.
- `m`/`M`: Cycle through the visualization modes (Bands → Radial → Trails → Digital Pulse → ASCII Flux → Bands).
- `p`/`P`: Cycle through the color designs (Rainbow → Warm/Cool → Digital Amber → Digital Cyan → Digital Violet → …).
- Arrow keys: Adjust grid rows (Up/Down) and columns (Left/Right) between 8 and 32 cells.
- `[` / `]`: Decrease or increase audio sensitivity to tune brightness response.

## Configuration & Plug-ins

Phase 8 introduces a comprehensive `who.toml` manifest checked at startup (the repository ships with a ready-to-edit version in the project root). The configuration controls:

- **Audio**: capture enablement, sample rate, channels, ring buffer sizing, optional default file playback, and gain staging.
- **DSP**: FFT size, hop size, band aggregation, window selection, smoothing constants, and beat detector sensitivity.
- **Visuals**: default grid geometry, sensitivity limits, palette/mode defaults, and target frame rate.
- **Runtime**: toggles for on-screen metrics, grid resizing, and beat-driven flashes.
- **Plug-ins**: autoloaded module IDs and the discovery directory for future dynamic modules.

Override settings per environment by passing `--config /path/to/override.toml`. Unknown keys are ignored with a warning, and malformed values fall back to the built-in defaults. The bundled `beat-flash-debug` plug-in is active by default and appends beat-detection diagnostics to `plugins/beat-flash-debug.log` (or `./beat-flash-debug.log` if the directory cannot be created); disable it by removing it from `plugins.autoload` or setting `runtime.beat_flash = false`.

### Phase 9 – Digital Specialisation

Phase 9 introduces a monochrome "Digital Pulse" mode designed for electronic music with emphatic beats. The renderer focuses on
a single colour per design and encodes energy solely as brightness, producing a crisp, LED-like grid with no sinusoidal waves or
rainbow gradients. The bundled `who.toml` now defaults to this mode with the **Digital Amber** design; set `visual.mode = "digital"`
and adjust `visual.palette` to pin any of the variants:

- **Digital Amber** – A warm amber glow that snaps to high intensity whenever the beat detector fires, evoking classic VU tubes.
- **Digital Cyan** – An icy cyan sweep with a discrete column scanner accent for a synth-lab aesthetic.
- **Digital Violet** – A saturated violet lattice that alternates in a checker cadence for a punchy, club-ready pulse.

Switch designs at runtime with the `p` key or edit the `who.toml` values above to set a persistent default.

### Phase 10 – ASCII Flux Cartography

Phase 10 expands the renderer with **ASCII Flux**, a densely textured mode inspired by notcurses-based map demos such as MapSCII.
Instead of colouring solid blocks, the grid is tessellated into shimmering ASCII glyphs that react to both band energy and
high-frequency detail. Beat events push the pattern into brighter characters while time-varying jitter keeps the output busy and
alive. The mode respects the existing colour palettes—warm gradients remain smooth, while digital palettes yield crisp monochrome
glows—and fully reuses the audio/DSP pipeline so no extra setup is required. Select it at runtime via the `m` key or pin it in
configuration with `visual.mode = "ascii"`.
