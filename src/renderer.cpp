#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace who {
namespace {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct CellState {
    float smooth_r{0.0f};
    float smooth_g{0.0f};
    float smooth_b{0.0f};
    Rgb color{0, 0, 0};
    bool valid{false};
};

struct GridCache {
    int rows{0};
    int cols{0};
    int cell_h{0};
    int cell_w{0};
    int offset_y{0};
    int offset_x{0};
    std::vector<CellState> cells;
    std::string fill;
};

GridCache& grid_cache() {
    static GridCache cache;
    return cache;
}

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0f) {
        t += 1.0f;
    }
    if (t > 1.0f) {
        t -= 1.0f;
    }
    if (t < 1.0f / 6.0f) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 1.0f / 2.0f) {
        return q;
    }
    if (t < 2.0f / 3.0f) {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    }
    return p;
}

Rgb hsl_to_rgb(float h, float s, float l) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) {
        h += 1.0f;
    }
    s = clamp01(s);
    l = clamp01(l);

    float r;
    float g;
    float b;

    if (s == 0.0f) {
        r = g = b = l;
    } else {
        const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        const float p = 2.0f * l - q;
        r = hue_to_rgb(p, q, h + 1.0f / 3.0f);
        g = hue_to_rgb(p, q, h);
        b = hue_to_rgb(p, q, h - 1.0f / 3.0f);
    }

    return Rgb{static_cast<uint8_t>(std::round(r * 255.0f)),
               static_cast<uint8_t>(std::round(g * 255.0f)),
               static_cast<uint8_t>(std::round(b * 255.0f))};
}

std::string format_band_meter(const std::vector<float>& bands) {
    static const std::string glyphs = " .:-=+*#%@";
    if (bands.empty()) {
        return "Bands (unavailable)";
    }

    std::string line = "Bands ";
    for (float energy : bands) {
        const float scaled = std::log10(1.0f + std::max(energy, 0.0f) * 9.0f) / std::log10(10.0f);
        const float normalized = clamp01(scaled);
        const float position = normalized * static_cast<float>(glyphs.size() - 1);
        const int idx = static_cast<int>(std::round(position));
        line.push_back(glyphs[idx]);
    }
    return line;
}

} // namespace

const char* mode_name(VisualizationMode mode) {
    switch (mode) {
    case VisualizationMode::Bands:
        return "Bands";
    case VisualizationMode::Radial:
        return "Radial";
    case VisualizationMode::Trails:
        return "Trails";
    default:
        return "Unknown";
    }
}

const char* palette_name(ColorPalette palette) {
    switch (palette) {
    case ColorPalette::Rainbow:
        return "Rainbow";
    case ColorPalette::WarmCool:
        return "Warm/Cool";
    default:
        return "Unknown";
    }
}

void draw_grid(notcurses* nc,
               int grid_rows,
               int grid_cols,
               float time_s,
               VisualizationMode mode,
               ColorPalette palette,
               float sensitivity,
               const AudioMetrics& metrics,
               const std::vector<float>& bands,
               float beat_strength,
               bool file_stream,
               bool show_metrics) {
    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(stdplane, &plane_rows, &plane_cols);

    const int cell_h_from_rows = static_cast<int>(plane_rows) / grid_rows;
    const int cell_h_from_cols = static_cast<int>(plane_cols) / (grid_cols * 2);
    const int cell_h = std::max(1, std::min(cell_h_from_rows, cell_h_from_cols));
    const int cell_w = cell_h * 2;

    const int grid_height = cell_h * grid_rows;
    const int grid_width = cell_w * grid_cols;

    const int offset_y = std::max(0, (static_cast<int>(plane_rows) - grid_height) / 2);
    const int offset_x = std::max(0, (static_cast<int>(plane_cols) - grid_width) / 2);

    GridCache& cache = grid_cache();
    const bool geometry_changed = cache.rows != grid_rows || cache.cols != grid_cols || cache.cell_h != cell_h ||
                                  cache.cell_w != cell_w || cache.offset_y != offset_y || cache.offset_x != offset_x;

    if (geometry_changed) {
        ncplane_erase(stdplane);
        cache.rows = grid_rows;
        cache.cols = grid_cols;
        cache.cell_h = cell_h;
        cache.cell_w = cell_w;
        cache.offset_y = offset_y;
        cache.offset_x = offset_x;
        cache.cells.assign(static_cast<std::size_t>(grid_rows * grid_cols), CellState{});
    } else if (cache.cells.size() != static_cast<std::size_t>(grid_rows * grid_cols)) {
        cache.cells.assign(static_cast<std::size_t>(grid_rows * grid_cols), CellState{});
    }

    ncplane_set_fg_default(stdplane);

    const int v_gap = 1;
    const int h_gap = 2;
    const int fill_w = std::max(1, cell_w - h_gap);
    if (static_cast<int>(cache.fill.size()) != fill_w) {
        cache.fill.assign(static_cast<std::size_t>(fill_w), ' ');
    }
    const std::string& cell_fill = cache.fill;
    const int draw_height = std::max(1, cell_h - v_gap);

    const std::size_t band_count = bands.size();
    float max_band_energy = 0.0f;
    float mean_band_energy = 0.0f;
    if (band_count > 0) {
        for (float energy : bands) {
            max_band_energy = std::max(max_band_energy, energy);
            mean_band_energy += energy;
        }
        mean_band_energy /= static_cast<float>(band_count);
    }

    const float reference_energy = std::max(max_band_energy, mean_band_energy * 1.5f);
    const float user_gain = std::max(0.1f, sensitivity);
    const float gain = reference_energy > 0.0f ? user_gain / reference_energy : user_gain;
    const float log_denom = std::log1p(9.0f);

    auto normalize_energy = [&](float energy) {
        const float scaled = std::log1p(std::max(energy, 0.0f) * gain * 9.0f);
        return clamp01(log_denom > 0.0f ? scaled / log_denom : 0.0f);
    };

    const float center_row = (grid_rows - 1) / 2.0f;
    const float center_col = (grid_cols - 1) / 2.0f;
    const float max_radius = std::max(1.0f, std::sqrt(center_row * center_row + center_col * center_col));
    constexpr float inv_two_pi = 0.15915494309189535f; // 1 / (2π)

    const bool full_refresh = geometry_changed;

    const float beat_flash = clamp01(beat_strength);

    for (int r = 0; r < grid_rows; ++r) {
        for (int c = 0; c < grid_cols; ++c) {
            std::size_t band_index = 0;
            float band_mix = 0.0f;
            if (band_count > 0) {
                switch (mode) {
                case VisualizationMode::Bands: {
                    const float band_t = static_cast<float>(r) / static_cast<float>(grid_rows);
                    band_index = std::min<std::size_t>(band_count - 1,
                                                        static_cast<std::size_t>(band_t * static_cast<float>(band_count)));
                    band_mix = static_cast<float>(band_index) / std::max<std::size_t>(1, band_count - 1);
                    break;
                }
                case VisualizationMode::Radial: {
                    const float dr = static_cast<float>(r) - center_row;
                    const float dc = static_cast<float>(c) - center_col;
                    const float radius = std::sqrt(dr * dr + dc * dc);
                    const float normalized = clamp01(radius / max_radius);
                    band_index = std::min<std::size_t>(band_count - 1,
                                                        static_cast<std::size_t>(normalized * static_cast<float>(band_count)));
                    band_mix = normalized;
                    break;
                }
                case VisualizationMode::Trails: {
                    const float column_phase = static_cast<float>(c) / std::max(1, grid_cols - 1);
                    float trail_phase = std::fmod(time_s * 0.35f + column_phase, 1.0f);
                    if (trail_phase < 0.0f) {
                        trail_phase += 1.0f;
                    }
                    band_index = std::min<std::size_t>(band_count - 1,
                                                        static_cast<std::size_t>(trail_phase * static_cast<float>(band_count)));
                    band_mix = trail_phase;
                    break;
                }
                }
            }

            const float band_energy = (band_index < band_count) ? bands[band_index] : 0.0f;
            const float energy_level = normalize_energy(band_energy);

            const float column_phase = static_cast<float>(c) / std::max(1, grid_cols - 1);
            const float time_wave = std::sin(time_s * 1.3f + column_phase * 3.0f);
            const float shimmer = std::sin(time_s * 0.9f + r * 0.35f + c * 0.22f);

            float base_hue = column_phase;
            if (band_count > 0) {
                switch (mode) {
                case VisualizationMode::Bands:
                    base_hue = static_cast<float>(band_index) / static_cast<float>(band_count);
                    break;
                case VisualizationMode::Radial: {
                    const float dr = static_cast<float>(r) - center_row;
                    const float dc = static_cast<float>(c) - center_col;
                    const float angle = std::atan2(dr, dc);
                    base_hue = std::fmod(angle * inv_two_pi + 1.0f, 1.0f);
                    break;
                }
                case VisualizationMode::Trails:
                    base_hue = band_mix;
                    break;
                }
            }

            const float hue_shift = std::fmod(time_s * 0.05f + column_phase * 0.15f, 1.0f);

            float hue = std::fmod(base_hue + hue_shift, 1.0f);
            float saturation = clamp01(0.55f + energy_level * 0.4f + shimmer * 0.05f);
            float brightness = clamp01(0.12f + energy_level * (0.82f + beat_flash * 0.35f) + time_wave * 0.12f +
                                       beat_flash * 0.12f);

            if (palette == ColorPalette::WarmCool) {
                const float warm_cool_base = clamp01(band_mix);
                const float warm_cool_hue = std::fmod(0.58f - warm_cool_base * 0.42f + shimmer * 0.02f, 1.0f);
                hue = std::fmod(warm_cool_hue + beat_flash * 0.05f, 1.0f);
                saturation = clamp01(0.45f + energy_level * 0.35f + shimmer * 0.08f);
                brightness = clamp01(0.18f + energy_level * (0.75f + beat_flash * 0.45f) + time_wave * 0.08f +
                                      beat_flash * 0.18f);
            }

            const Rgb target_color = hsl_to_rgb(hue, saturation, brightness);
            const float target_r = clamp01(static_cast<float>(target_color.r) / 255.0f);
            const float target_g = clamp01(static_cast<float>(target_color.g) / 255.0f);
            const float target_b = clamp01(static_cast<float>(target_color.b) / 255.0f);

            const std::size_t cell_index = static_cast<std::size_t>(r * grid_cols + c);
            if (cell_index >= cache.cells.size()) {
                continue;
            }

            CellState& state = cache.cells[cell_index];
            if (!state.valid || full_refresh) {
                state.smooth_r = target_r;
                state.smooth_g = target_g;
                state.smooth_b = target_b;
            } else {
                constexpr float smoothing = 0.22f;
                state.smooth_r += (target_r - state.smooth_r) * smoothing;
                state.smooth_g += (target_g - state.smooth_g) * smoothing;
                state.smooth_b += (target_b - state.smooth_b) * smoothing;
            }

            const Rgb color{static_cast<uint8_t>(std::round(clamp01(state.smooth_r) * 255.0f)),
                            static_cast<uint8_t>(std::round(clamp01(state.smooth_g) * 255.0f)),
                            static_cast<uint8_t>(std::round(clamp01(state.smooth_b) * 255.0f))};

            bool needs_update = full_refresh || !state.valid || state.color.r != color.r || state.color.g != color.g ||
                                state.color.b != color.b;

            if (!needs_update) {
                continue;
            }

            for (int dy = 0; dy < draw_height; ++dy) {
                const int y = offset_y + r * cell_h + dy;
                if (y >= static_cast<int>(plane_rows)) {
                    continue;
                }
                const int x = offset_x + c * cell_w;
                if (x >= static_cast<int>(plane_cols)) {
                    continue;
                }

                ncplane_set_bg_rgb8(stdplane, color.r, color.g, color.b);
                ncplane_putstr_yx(stdplane, y, x, cell_fill.c_str());
            }

            state.color = color;
            state.valid = true;
        }
    }

    const int overlay_y = std::min(static_cast<int>(plane_rows) - 1, offset_y + grid_height);
    const int overlay_x = offset_x;
    auto clear_overlay_line = [&](int y) {
        if (y >= static_cast<int>(plane_rows)) {
            return;
        }
        const int width = std::max(0, static_cast<int>(plane_cols) - overlay_x);
        if (width <= 0) {
            return;
        }
        ncplane_set_fg_default(stdplane);
        ncplane_set_bg_default(stdplane);
        ncplane_printf_yx(stdplane, y, overlay_x, "%*s", width, "");
    };

    if (!show_metrics) {
        clear_overlay_line(overlay_y);
        clear_overlay_line(overlay_y + 1);
        clear_overlay_line(overlay_y + 2);
        return;
    }

    clear_overlay_line(overlay_y);
    ncplane_set_fg_rgb8(stdplane, 200, 200, 200);
    ncplane_set_bg_default(stdplane);
    ncplane_printf_yx(stdplane, overlay_y, overlay_x,
                      "Audio %s | Mode: %s | Palette: %s | Grid: %dx%d | Sens: %.2f",
                      metrics.active ? (file_stream ? "file" : "capturing") : "inactive",
                      mode_name(mode),
                      palette_name(palette),
                      grid_rows,
                      grid_cols,
                      sensitivity);

    if (overlay_y + 1 < static_cast<int>(plane_rows)) {
        clear_overlay_line(overlay_y + 1);
        ncplane_set_fg_rgb8(stdplane, 200, 200, 200);
        ncplane_set_bg_default(stdplane);
        ncplane_printf_yx(stdplane, overlay_y + 1, overlay_x,
                          "RMS: %.3f | Peak: %.3f | Dropped: %zu | Beat: %.2f",
                          metrics.rms,
                          metrics.peak,
                          metrics.dropped,
                          beat_flash);
    }

    if (overlay_y + 2 < static_cast<int>(plane_rows)) {
        clear_overlay_line(overlay_y + 2);
        ncplane_set_fg_rgb8(stdplane, 200, 200, 200);
        ncplane_set_bg_default(stdplane);
        const std::string band_meter = format_band_meter(bands);
        ncplane_printf_yx(stdplane, overlay_y + 2, overlay_x, "%s", band_meter.c_str());
    }
}

} // namespace who

