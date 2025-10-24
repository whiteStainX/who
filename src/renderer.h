#pragma once

#include <vector>

#include <notcurses/notcurses.h>

#include "audio_engine.h"

namespace who {

enum class VisualizationMode {
    Bands,
    Radial,
    Trails,
};

void draw_grid(notcurses* nc,
               int grid_rows,
               int grid_cols,
               float time_s,
               VisualizationMode mode,
               float sensitivity,
               const AudioMetrics& metrics,
               const std::vector<float>& bands,
               bool file_stream);

const char* mode_name(VisualizationMode mode);

} // namespace who

