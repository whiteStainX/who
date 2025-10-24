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
#include <utility>
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
    AudioEngine(ma_uint32 sample_rate,
                ma_uint32 channels,
                std::size_t ring_frames,
                std::string file_path = {})
        : sample_rate_(sample_rate),
          channels_(channels),
          ring_buffer_(ring_frames * channels),
          dropped_samples_(0),
          mode_(file_path.empty() ? Mode::Capture : Mode::FileStream),
          file_path_(std::move(file_path)),
          device_initialized_(false),
          decoder_initialized_(false),
          decoder_channels_(0),
          decoder_sample_rate_(0),
          resampler_initialized_(false),
          stop_stream_thread_(false) {}

    ~AudioEngine() { stop(); }

    bool start() {
        if (mode_ == Mode::Capture) {
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

        if (decoder_initialized_) {
            return true;
        }

        if (file_path_.empty()) {
            return false;
        }

        ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file(file_path_.c_str(), &decoder_config, &decoder_) != MA_SUCCESS) {
            return false;
        }

        decoder_channels_ = decoder_.outputChannels;
        decoder_sample_rate_ = decoder_.outputSampleRate;
        if (decoder_channels_ == 0) {
            decoder_channels_ = 1;
        }
        if (decoder_sample_rate_ == 0) {
            decoder_sample_rate_ = sample_rate_;
        }

        if (decoder_sample_rate_ != sample_rate_) {
            ma_resampler_config resampler_config =
                ma_resampler_config_init(ma_format_f32, channels_, decoder_sample_rate_, sample_rate_, ma_resample_algorithm_linear);
            resampler_config.channels = channels_;
            if (ma_resampler_init(&resampler_config, nullptr, &resampler_) != MA_SUCCESS) {
                ma_decoder_uninit(&decoder_);
                decoder_initialized_ = false;
                return false;
            }
            resampler_initialized_ = true;
        }

        decoder_initialized_ = true;
        stop_stream_thread_.store(false, std::memory_order_relaxed);
        stream_thread_ = std::thread(&AudioEngine::file_stream_loop, this);
        dropped_samples_.store(0, std::memory_order_relaxed);
        return true;
    }

    void stop() {
        if (mode_ == Mode::Capture) {
            if (!device_initialized_) {
                return;
            }

            ma_device_uninit(&device_);
            device_initialized_ = false;
            return;
        }

        if (!decoder_initialized_) {
            return;
        }

        stop_stream_thread_.store(true, std::memory_order_relaxed);
        if (stream_thread_.joinable()) {
            stream_thread_.join();
        }

        if (resampler_initialized_) {
            ma_resampler_uninit(&resampler_, nullptr);
            resampler_initialized_ = false;
        }

        ma_decoder_uninit(&decoder_);
        decoder_initialized_ = false;
    }

    std::size_t read_samples(float* dest, std::size_t max_samples) {
        return ring_buffer_.read(dest, max_samples);
    }

    std::size_t dropped_samples() const {
        return dropped_samples_.load(std::memory_order_relaxed);
    }

    ma_uint32 channels() const { return channels_; }

    bool using_file_stream() const { return mode_ == Mode::FileStream; }

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

    void file_stream_loop() {
        if (!decoder_initialized_) {
            return;
        }

        constexpr std::size_t chunk_frames = 512;
        std::vector<float> decode_buffer(chunk_frames * decoder_channels_);
        std::vector<float> mono_buffer(chunk_frames, 0.0f);
        const double ratio = static_cast<double>(sample_rate_) / static_cast<double>(decoder_sample_rate_);
        const std::size_t max_output_frames = resampler_initialized_
                                                  ? static_cast<std::size_t>(std::ceil(chunk_frames * ratio)) + 8
                                                  : chunk_frames;
        std::vector<float> resample_buffer(resampler_initialized_ ? max_output_frames : 0);

        while (!stop_stream_thread_.load(std::memory_order_relaxed)) {
            ma_uint64 frames_requested = chunk_frames;
            ma_uint64 frames_read = 0;
            ma_result result =
                ma_decoder_read_pcm_frames(&decoder_, decode_buffer.data(), frames_requested, &frames_read);
            if (result != MA_SUCCESS || frames_read == 0) {
                ma_decoder_seek_to_pcm_frame(&decoder_, 0);
                continue;
            }

            const std::size_t frames_available = static_cast<std::size_t>(frames_read);
            for (std::size_t i = 0; i < frames_available; ++i) {
                double sum = 0.0;
                for (std::size_t ch = 0; ch < decoder_channels_; ++ch) {
                    sum += decode_buffer[i * decoder_channels_ + ch];
                }
                mono_buffer[i] = static_cast<float>(sum / static_cast<double>(decoder_channels_));
            }

            const float* data_to_write = mono_buffer.data();
            std::size_t frames_to_write = frames_available;

            if (resampler_initialized_) {
                ma_uint64 input_frame_count = frames_read;
                ma_uint64 output_frame_count = resample_buffer.size();
                if (ma_resampler_process_pcm_frames(&resampler_, mono_buffer.data(), &input_frame_count,
                                                    resample_buffer.data(), &output_frame_count) != MA_SUCCESS) {
                    continue;
                }
                frames_to_write = static_cast<std::size_t>(output_frame_count);
                data_to_write = resample_buffer.data();
            }

            const std::size_t samples_to_write = frames_to_write * static_cast<std::size_t>(channels_);
            const std::size_t written = ring_buffer_.write(data_to_write, samples_to_write);
            if (written < samples_to_write) {
                dropped_samples_.fetch_add(samples_to_write - written, std::memory_order_relaxed);
            }

            const double seconds = static_cast<double>(frames_to_write) / static_cast<double>(sample_rate_);
            if (seconds > 0.0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            }
        }
    }

    enum class Mode { Capture, FileStream };

    ma_uint32 sample_rate_;
    ma_uint32 channels_;
    FloatRingBuffer ring_buffer_;
    std::atomic<std::size_t> dropped_samples_;
    Mode mode_;
    std::string file_path_;
    ma_device device_{};
    bool device_initialized_;
    ma_decoder decoder_{};
    bool decoder_initialized_;
    ma_uint32 decoder_channels_;
    ma_uint32 decoder_sample_rate_;
    ma_resampler resampler_{};
    bool resampler_initialized_;
    std::thread stream_thread_;
    std::atomic<bool> stop_stream_thread_;
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
               const std::vector<float>& bands,
               bool file_stream) {
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
                      metrics.active ? (file_stream ? "file" : "capturing") : "inactive",
                      metrics.rms,
                      metrics.peak,
                      metrics.dropped);

    if (overlay_y + 1 < static_cast<int>(plane_rows)) {
        const std::string band_meter = format_band_meter(bands);
        ncplane_printf_yx(stdplane, overlay_y + 1, overlay_x, "%s", band_meter.c_str());
    }
}

} // namespace

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
    AudioEngine audio(sample_rate, channels, ring_frames, file_path);
    const bool audio_active = audio.start();

    constexpr std::size_t fft_size = 1024;
    constexpr std::size_t hop_size = fft_size / 4;
    constexpr std::size_t bands = 32;
    DspEngine dsp(sample_rate, channels, fft_size, hop_size, bands);

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

    const std::size_t scratch_samples = std::max<std::size_t>(4096, ring_frames * static_cast<std::size_t>(channels));
    std::vector<float> audio_scratch(scratch_samples);
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

        draw_grid(nc, grid_rows, grid_cols, time_s, audio_metrics, dsp.band_energies(), audio.using_file_stream());

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
