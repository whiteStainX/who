#pragma once

#include <vector>

#include <notcurses/notcurses.h>

#include "audio_engine.h"

namespace who {

void draw_grid(notcurses* nc,
               int grid_rows,
               int grid_cols,
               float time_s,
               const AudioMetrics& metrics,
               const std::vector<float>& bands,
               bool file_stream);

} // namespace who

