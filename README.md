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
./build/who [--file path/to/audio.wav]
```

Running without flags opens the real-time capture path (requires microphone permissions). Supplying `--file` (or `-f`) streams audio from disk through the same DSP chain. Supported formats depend on miniaudio's decoder (WAV/MP3/FLAC and more). The file path option downmixes to mono, resamples to 48 kHz, and feeds the visualizer at real-time speed so you can test the visualization without capture hardware.
