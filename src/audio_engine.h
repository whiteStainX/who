#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <miniaudio.h>

namespace who {

struct AudioMetrics {
    bool active = false;
    float rms = 0.0f;
    float peak = 0.0f;
    std::size_t dropped = 0;
};

class AudioEngine {
public:
    AudioEngine(ma_uint32 sample_rate, ma_uint32 channels, std::size_t ring_frames, std::string file_path = {});
    ~AudioEngine();

    bool start();
    void stop();

    std::size_t read_samples(float* dest, std::size_t max_samples);
    std::size_t dropped_samples() const;

    ma_uint32 channels() const { return channels_; }
    bool using_file_stream() const { return mode_ == Mode::FileStream; }

private:
    class FloatRingBuffer {
    public:
        explicit FloatRingBuffer(std::size_t capacity);

        std::size_t write(const float* data, std::size_t count);
        std::size_t read(float* dest, std::size_t count);

    private:
        std::vector<float> buffer_;
        const std::size_t capacity_;
        std::atomic<std::size_t> head_;
        std::atomic<std::size_t> tail_;
    };

    enum class Mode { Capture, FileStream };

    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);
    void file_stream_loop();

    const ma_uint32 sample_rate_;
    const ma_uint32 channels_;
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

} // namespace who

