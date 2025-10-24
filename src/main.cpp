#include <algorithm>
#include <chrono>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "audio_engine.h"
#include "dsp.h"
#include "renderer.h"

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    std::string file_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--file" || arg == "-f") && i + 1 < argc) {
            file_path = argv[i + 1];
            ++i;
        }
    }

    constexpr ma_uint32 sample_rate = 48000;
    const bool use_file_stream = !file_path.empty();
    const ma_uint32 channels = use_file_stream ? 1 : 2;
    constexpr std::size_t ring_frames = 8192;
    who::AudioEngine audio(sample_rate, channels, ring_frames, file_path);
    const bool audio_active = audio.start();

    constexpr std::size_t fft_size = 1024;
    constexpr std::size_t hop_size = fft_size / 4;
    constexpr std::size_t bands = 32;
    who::DspEngine dsp(sample_rate, channels, fft_size, hop_size, bands);

    notcurses_options opts{};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    notcurses* nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses" << std::endl;
        audio.stop();
        return 1;
    }

    int grid_rows = 16;
    int grid_cols = 16;
    constexpr int min_grid_dim = 8;
    constexpr int max_grid_dim = 32;
    float sensitivity = 1.0f;
    constexpr float min_sensitivity = 0.2f;
    constexpr float max_sensitivity = 5.0f;
    constexpr float sensitivity_step = 0.1f;
    who::VisualizationMode mode = who::VisualizationMode::Bands;
    constexpr std::chrono::duration<double> frame_time(1.0 / 60.0);

    const std::size_t scratch_samples = std::max<std::size_t>(4096, ring_frames * static_cast<std::size_t>(channels));
    std::vector<float> audio_scratch(scratch_samples);
    who::AudioMetrics audio_metrics{};
    audio_metrics.active = audio_active;

    bool running = true;
    const auto start_time = std::chrono::steady_clock::now();

    while (running) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start_time;
        const float time_s = std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count();

        if (audio_active) {
            const std::size_t samples_read = audio.read_samples(audio_scratch.data(), audio_scratch.size());
            if (samples_read > 0) {
                dsp.push_samples(audio_scratch.data(), samples_read);
                double sum_squares = 0.0;
                float peak_value = 0.0f;
                for (std::size_t i = 0; i < samples_read; ++i) {
                    const float sample = audio_scratch[i];
                    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
                    peak_value = std::max(peak_value, std::abs(sample));
                }
                const float rms_instant = std::sqrt(sum_squares / static_cast<double>(samples_read));
                audio_metrics.rms = audio_metrics.rms * 0.9f + rms_instant * 0.1f;
                audio_metrics.peak = std::max(peak_value, audio_metrics.peak * 0.95f);
            } else {
                audio_metrics.rms *= 0.98f;
                audio_metrics.peak *= 0.98f;
            }
            audio_metrics.dropped = audio.dropped_samples();
        }

        who::draw_grid(nc,
                       grid_rows,
                       grid_cols,
                       time_s,
                       mode,
                       sensitivity,
                       audio_metrics,
                       dsp.band_energies(),
                       audio.using_file_stream());

        if (notcurses_render(nc) != 0) {
            std::cerr << "Failed to render frame" << std::endl;
            break;
        }

        ncinput input{};
        const timespec ts{0, 0};
        uint32_t key = 0;
        while ((key = notcurses_get(nc, &ts, &input)) != 0) {
            if (key == static_cast<uint32_t>(-1)) {
                running = false;
                break;
            }
            if (key == 'q' || key == 'Q') {
                running = false;
                break;
            }
            if (key == NCKEY_UP) {
                grid_rows = std::min(grid_rows + 1, max_grid_dim);
                continue;
            }
            if (key == NCKEY_DOWN) {
                grid_rows = std::max(grid_rows - 1, min_grid_dim);
                continue;
            }
            if (key == NCKEY_RIGHT) {
                grid_cols = std::min(grid_cols + 1, max_grid_dim);
                continue;
            }
            if (key == NCKEY_LEFT) {
                grid_cols = std::max(grid_cols - 1, min_grid_dim);
                continue;
            }
            if (key == 'm' || key == 'M') {
                switch (mode) {
                case who::VisualizationMode::Bands:
                    mode = who::VisualizationMode::Radial;
                    break;
                case who::VisualizationMode::Radial:
                    mode = who::VisualizationMode::Trails;
                    break;
                case who::VisualizationMode::Trails:
                    mode = who::VisualizationMode::Bands;
                    break;
                }
                continue;
            }
            if (key == '[') {
                sensitivity = std::max(min_sensitivity, sensitivity - sensitivity_step);
                continue;
            }
            if (key == ']') {
                sensitivity = std::min(max_sensitivity, sensitivity + sensitivity_step);
                continue;
            }
            if (key == NCKEY_RESIZE) {
                break;
            }
        }

        const auto frame_end = std::chrono::steady_clock::now();
        if (frame_end - now < frame_time) {
            std::this_thread::sleep_for(frame_time - (frame_end - now));
        }
    }

    audio.stop();

    if (notcurses_stop(nc) != 0) {
        std::cerr << "Failed to stop notcurses cleanly" << std::endl;
        return 1;
    }

    return 0;
}

