# who

A lean, modern C++ terminal-based audio visualizer. The current milestone focuses on establishing the build skeleton so future p
hases can layer in rendering, audio capture, and DSP features.

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
./build/who
```

The Phase 0 program simply opens and closes a blank notcurses-managed terminal window, verifying that the toolchain and link ste
ps are configured correctly.
