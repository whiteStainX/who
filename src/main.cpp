#include <algorithm>
#include <chrono>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <notcurses/notcurses.h>

namespace {
struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

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

RGB hsl_to_rgb(float h, float s, float l) {
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

    return RGB{static_cast<uint8_t>(std::round(r * 255.0f)),
               static_cast<uint8_t>(std::round(g * 255.0f)),
               static_cast<uint8_t>(std::round(b * 255.0f))};
}

void draw_grid(notcurses* nc, int grid_rows, int grid_cols, float time_s) {
    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(stdplane, &plane_rows, &plane_cols);

    const int cell_h = std::max(1, (int)plane_rows / grid_rows);
    const int cell_w = std::max(2, (int)plane_cols / grid_cols);
    const int grid_height = cell_h * grid_rows;
    const int grid_width = cell_w * grid_cols;

    const int offset_y = std::max(0, ((int)plane_rows - grid_height) / 2);
    const int offset_x = std::max(0, ((int)plane_cols - grid_width) / 2);

    ncplane_erase(stdplane);
    ncplane_set_fg_default(stdplane);

    const std::string cell_fill(cell_w, ' ');

    for (int r = 0; r < grid_rows; ++r) {
        for (int c = 0; c < grid_cols; ++c) {
            const float base_hue = static_cast<float>(c) / static_cast<float>(grid_cols);
            const float wave = std::sin(time_s * 1.5f + r * 0.35f + c * 0.2f);
            const float shimmer = std::sin(time_s * 0.8f + c * 0.45f);
            const float brightness = 0.45f + 0.4f * wave;
            const float saturation = 0.5f + 0.4f * shimmer;
            const RGB color = hsl_to_rgb(base_hue, saturation, brightness);

            for (int dy = 0; dy < cell_h; ++dy) {
                const int y = offset_y + r * cell_h + dy;
                if (y >= plane_rows) {
                    continue;
                }
                const int x = offset_x + c * cell_w;
                if (x >= plane_cols) {
                    continue;
                }

                ncplane_set_bg_rgb8(stdplane, color.r, color.g, color.b);
                ncplane_putstr_yx(stdplane, y, x, cell_fill.c_str());
            }
        }
    }
}

} // namespace

int main() {
    std::setlocale(LC_ALL, "");

    notcurses_options opts{};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    notcurses* nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses" << std::endl;
        return 1;
    }

    constexpr int grid_rows = 16;
    constexpr int grid_cols = 16;
    constexpr std::chrono::duration<double> frame_time(1.0 / 60.0);

    bool running = true;
    const auto start_time = std::chrono::steady_clock::now();

    while (running) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start_time;
        const float time_s = std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count();

        draw_grid(nc, grid_rows, grid_cols, time_s);

        if (notcurses_render(nc) != 0) {
            std::cerr << "Failed to render frame" << std::endl;
            break;
        }

        ncinput input{};
        const timespec ts{0, 0};
        uint32_t key = 0;
        while ((key = notcurses_get(nc, &ts, &input)) != 0) {
            if (key == (uint32_t)-1) {
                running = false;
                break;
            }
            if (key == 'q' || key == 'Q') {
                running = false;
                break;
            }
            if (key == NCKEY_RESIZE) {
                // Force redraw with updated dimensions on next loop iteration.
                break;
            }
        }

        const auto frame_end = std::chrono::steady_clock::now();
        if (frame_end - now < frame_time) {
            std::this_thread::sleep_for(frame_time - (frame_end - now));
        }
    }

    if (notcurses_stop(nc) != 0) {
        std::cerr << "Failed to stop notcurses cleanly" << std::endl;
        return 1;
    }

    return 0;
}
