cmake_minimum_required(VERSION 3.16)
project(who LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- notcurses via pkg-config ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES QUIET IMPORTED_TARGET notcurses)
if (NOT NOTCURSES_FOUND)
  pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses-core)
endif()

# --- sources ---
add_executable(who
  src/main.cpp
  src/dsp.cpp
  external/kissfft/kiss_fft.c
)

# --- includes ---
target_include_directories(who PRIVATE
  src
  external/miniaudio
  external/kissfft
)

# --- link notcurses (and its transitive deps) ---
target_link_libraries(who PRIVATE PkgConfig::NOTCURSES)
