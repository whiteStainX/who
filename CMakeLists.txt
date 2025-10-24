cmake_minimum_required(VERSION 3.16)
project(who LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find notcurses
find_package(Notcurses REQUIRED)

# Add our source files
add_executable(who src/main.cpp)

# Include kissfft sources
target_sources(who PRIVATE
    external/kissfft/kiss_fft.c
)

# Include directories
target_include_directories(who PRIVATE
    src
    external/miniaudio
    external/kissfft
)

# Link libraries
target_link_libraries(who PRIVATE Notcurses::notcurses)
