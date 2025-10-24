#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <notcurses/notcurses.h>

#include "dsp.h"

namespace {
class FloatRingBuffer {
public:
    explicit FloatRingBuffer(std::size_t capacity)
        : buffer_(capacity), capacity_(capacity), head_(0), tail_(0) {}

    std::size_t write(const float* data, std::size_t count) {
        if (capacity_ == 0 || count == 0) {
            return 0;
        }

        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t used = head - tail;
        const std::size_t free_space = capacity_ > used ? capacity_ - used : 0;
        const std::size_t to_write = std::min(count, free_space);
        if (to_write == 0) {
            return 0;
        }

        const std::size_t first_chunk = std::min(to_write, capacity_ - (head % capacity_));
        std::memcpy(&buffer_[head % capacity_], data, first_chunk * sizeof(float));
        if (to_write > first_chunk) {
            std::memcpy(buffer_.data(), data + first_chunk, (to_write - first_chunk) * sizeof(float));
        }

        head_.store(head + to_write, std::memory_order_release);
        return to_write;
    }

    std::size_t read(float* dest, std::size_t count) {
        if (capacity_ == 0 || count == 0) {
            return 0;
        }

        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t available = head - tail;
        const std::size_t to_read = std::min(count, available);
        if (to_read == 0) {
            return 0;
        }

        const std::size_t first_chunk = std::min(to_read, capacity_ - (tail % capacity_));
        std::memcpy(dest, &buffer_[tail % capacity_], first_chunk * sizeof(float));
        if (to_read > first_chunk) {
            std::memcpy(dest + first_chunk, buffer_.data(), (to_read - first_chunk) * sizeof(float));
        }

        tail_.store(tail + to_read, std::memory_order_release);
        return to_read;
    }

private:
    std::vector<float> buffer_;
    const std::size_t capacity_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};

struct AudioMetrics {
    bool active = false;
    float rms = 0.0f;
    float peak = 0.0f;
    std::size_t dropped = 0;
};

class AudioEngine {
public:
    AudioEngine(ma_uint32 sample_rate, ma_uint32 channels, std::size_t ring_frames)
        : sample_rate_(sample_rate),
          channels_(channels),
          ring_buffer_(ring_frames * channels),
          dropped_samples_(0),
          device_initialized_(false) {}

    ~AudioEngine() { stop(); }

    bool start() {
        if (device_initialized_) {
            return true;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.sampleRate = sample_rate_;
        config.capture.format = ma_format_f32;
        config.capture.channels = channels_;
        config.dataCallback = &AudioEngine::data_callback;
        config.pUserData = this;

        if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS) {
            return false;
        }

        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            return false;
        }

        device_initialized_ = true;
        dropped_samples_.store(0, std::memory_order_relaxed);
        return true;
    }

    void stop() {
        if (!device_initialized_) {
            return;
        }

        ma_device_uninit(&device_);
        device_initialized_ = false;
    }

    std::size_t read_samples(float* dest, std::size_t max_samples) {
        return ring_buffer_.read(dest, max_samples);
    }

    std::size_t dropped_samples() const {
        return dropped_samples_.load(std::memory_order_relaxed);
    }

    ma_uint32 channels() const { return channels_; }

private:
    static void data_callback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frame_count) {
        auto* engine = reinterpret_cast<AudioEngine*>(device->pUserData);
        if (!engine) {
            return;
        }

        const float* samples = static_cast<const float*>(input);
        const std::size_t sample_count = static_cast<std::size_t>(frame_count) * engine->channels_;
        const std::size_t written = engine->ring_buffer_.write(samples, sample_count);
        if (written < sample_count) {
            engine->dropped_samples_.fetch_add(sample_count - written, std::memory_order_relaxed);
        }
    }

    ma_uint32 sample_rate_;
    ma_uint32 channels_;
    FloatRingBuffer ring_buffer_;
    std::atomic<std::size_t> dropped_samples_;
    ma_device device_{};
    bool device_initialized_;
};

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

void draw_grid(notcurses* nc,
               int grid_rows,
               int grid_cols,
               float time_s,
               const AudioMetrics& metrics,
               const std::vector<float>& bands) {
    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(stdplane, &plane_rows, &plane_cols);

    // To make cells square, assume a 2:1 character aspect ratio.
    const int cell_h_from_rows = (int)plane_rows / grid_rows;
    const int cell_h_from_cols = (int)plane_cols / (grid_cols * 2);
    const int cell_h = std::max(1, std::min(cell_h_from_rows, cell_h_from_cols));
    const int cell_w = cell_h * 2;

    const int grid_height = cell_h * grid_rows;
    const int grid_width = cell_w * grid_cols;

    const int offset_y = std::max(0, ((int)plane_rows - grid_height) / 2);
    const int offset_x = std::max(0, ((int)plane_cols - grid_width) / 2);

    ncplane_erase(stdplane);
    ncplane_set_fg_default(stdplane);

    const int v_gap = 1;
    const int h_gap = 2;
    const int fill_w = std::max(0, cell_w - h_gap);
    const std::string cell_fill(fill_w, ' ');

    for (int r = 0; r < grid_rows; ++r) {
        for (int c = 0; c < grid_cols; ++c) {
            const float base_hue = static_cast<float>(c) / static_cast<float>(grid_cols);
            const float wave = std::sin(time_s * 1.5f + r * 0.35f + c * 0.2f);
            const float shimmer = std::sin(time_s * 0.8f + c * 0.45f);
            const float brightness = 0.45f + 0.4f * wave;
            const float saturation = 0.5f + 0.4f * shimmer;
            const RGB color = hsl_to_rgb(base_hue, saturation, brightness);

            for (int dy = 0; dy < cell_h - v_gap; ++dy) {
                const int y = offset_y + r * cell_h + dy;
                if (y >= (int)plane_rows) {
                    continue;
                }
                const int x = offset_x + c * cell_w;
                if (x >= (int)plane_cols) {
                    continue;
                }

                ncplane_set_bg_rgb8(stdplane, color.r, color.g, color.b);
                ncplane_putstr_yx(stdplane, y, x, cell_fill.c_str());
            }
        }
    }

    const int overlay_y = std::min(static_cast<int>(plane_rows) - 1, offset_y + grid_height);
    const int overlay_x = offset_x;
    ncplane_set_fg_rgb8(stdplane, 200, 200, 200);
    ncplane_set_bg_default(stdplane);
    ncplane_printf_yx(stdplane, overlay_y, overlay_x,
                      "Audio %s | RMS: %.3f | Peak: %.3f | Dropped: %zu",
                      metrics.active ? "capturing" : "inactive",
                      metrics.rms,
                      metrics.peak,
                      metrics.dropped);

    if (overlay_y + 1 < static_cast<int>(plane_rows)) {
        const std::string band_meter = format_band_meter(bands);
        ncplane_printf_yx(stdplane, overlay_y + 1, overlay_x, "%s", band_meter.c_str());
    }
}

} // namespace

int main() {
    std::setlocale(LC_ALL, "");

    constexpr ma_uint32 sample_rate = 48000;
    constexpr ma_uint32 channels = 2;
    constexpr std::size_t ring_frames = 8192;
    AudioEngine audio(sample_rate, channels, ring_frames);
    const bool audio_active = audio.start();

    DspEngine dsp(sample_rate, channels);

    notcurses_options opts{};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    notcurses* nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses" << std::endl;
        audio.stop();
        return 1;
    }

    constexpr int grid_rows = 16;
    constexpr int grid_cols = 16;
    constexpr std::chrono::duration<double> frame_time(1.0 / 60.0);

    std::vector<float> audio_scratch(4096);
    AudioMetrics audio_metrics{};
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

        draw_grid(nc, grid_rows, grid_cols, time_s, audio_metrics, dsp.band_energies());

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

    audio.stop();

    if (notcurses_stop(nc) != 0) {
        std::cerr << "Failed to stop notcurses cleanly" << std::endl;
        return 1;
    }

    return 0;
}
