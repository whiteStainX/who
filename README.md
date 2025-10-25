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
./build/who [--config path/to/who.toml] [--file path/to/audio.wav]
```

Running without flags opens the real-time capture path (requires microphone permissions). Supplying `--file` (or `-f`) streams audio from disk through the same DSP chain. Supported formats depend on miniaudio's decoder (WAV/MP3/FLAC and more). The file path option downmixes to mono, resamples to 48 kHz, and feeds the visualizer at real-time speed so you can test the visualization without capture hardware. Use `--config` (or `-c`) to load an alternate TOML configuration.

## Controls

Interact with the visualizer while it is running:

- `q`/`Q`: Quit the program immediately.
- `m`/`M`: Cycle through the visualization modes (Bands → Radial → Trails → Digital Pulse → …).
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
