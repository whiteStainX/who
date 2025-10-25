#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef MINIAUDIO_IMPLEMENTATION

#include "audio_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string to_lower_copy(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    std::string lower_haystack = to_lower_copy(haystack);
    std::string lower_needle = to_lower_copy(needle);
    return lower_haystack.find(lower_needle) != std::string::npos;
}

} // namespace

namespace who {

AudioEngine::FloatRingBuffer::FloatRingBuffer(std::size_t capacity)
    : buffer_(capacity), capacity_(capacity), head_(0), tail_(0) {}

std::size_t AudioEngine::FloatRingBuffer::write(const float* data, std::size_t count) {
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

std::size_t AudioEngine::FloatRingBuffer::read(float* dest, std::size_t count) {
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

AudioEngine::AudioEngine(ma_uint32 sample_rate,
                         ma_uint32 channels,
                         std::size_t ring_frames,
                         std::string file_path,
                         std::string device_name,
                         bool system_audio)
    : sample_rate_(sample_rate),
      channels_(channels),
      ring_buffer_(ring_frames * channels),
      dropped_samples_(0),
      mode_(file_path.empty() ? Mode::Capture : Mode::FileStream),
      file_path_(std::move(file_path)),
      device_name_(std::move(device_name)),
      system_audio_(system_audio),
      device_initialized_(false),
      context_initialized_(false),
      have_device_id_(false),
      decoder_initialized_(false),
      decoder_channels_(0),
      decoder_sample_rate_(0),
      resampler_initialized_(false),
      stop_stream_thread_(false) {}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start() {
    last_error_.clear();
    if (mode_ == Mode::Capture) {
        if (device_initialized_) {
            return true;
        }

        ma_device_type device_type = ma_device_type_capture;
#if defined(_WIN32)
        if (system_audio_) {
            device_type = ma_device_type_loopback;
        }
#endif
        ma_device_config config = ma_device_config_init(device_type);
        config.sampleRate = sample_rate_;
        config.capture.format = ma_format_f32;
        config.capture.channels = channels_;
        config.dataCallback = &AudioEngine::data_callback;
        config.pUserData = this;

        ma_context* context = nullptr;
        if (!device_name_.empty() || system_audio_) {
            ma_context_config context_config = ma_context_config_init();
            if (ma_context_init(nullptr, 0, &context_config, &context_) != MA_SUCCESS) {
                last_error_ = "failed to initialize audio context";
                return false;
            }
            context_initialized_ = true;
            context = &context_;

            ma_device_info* playback_infos = nullptr;
            ma_uint32 playback_count = 0;
            ma_device_info* capture_infos = nullptr;
            ma_uint32 capture_count = 0;
            if (ma_context_get_devices(context, &playback_infos, &playback_count, &capture_infos, &capture_count) != MA_SUCCESS) {
                last_error_ = "failed to enumerate audio devices";
                ma_context_uninit(&context_);
                context_initialized_ = false;
                return false;
            }

            auto select_capture_id = [&](std::string_view name) -> bool {
                for (ma_uint32 i = 0; i < capture_count; ++i) {
                    if (equals_ignore_case(capture_infos[i].name, name) || contains_ignore_case(capture_infos[i].name, name)) {
                        device_id_ = capture_infos[i].id;
                        have_device_id_ = true;
                        return true;
                    }
                }
                for (ma_uint32 i = 0; i < playback_count; ++i) {
                    if (equals_ignore_case(playback_infos[i].name, name) || contains_ignore_case(playback_infos[i].name, name)) {
                        device_id_ = playback_infos[i].id;
                        have_device_id_ = true;
                        return true;
                    }
                }
                return false;
            };

            if (!device_name_.empty()) {
                if (!select_capture_id(device_name_)) {
                    last_error_ = "requested device not found: '" + device_name_ + "'";
                    ma_context_uninit(&context_);
                    context_initialized_ = false;
                    return false;
                }
            } else if (system_audio_) {
#if defined(_WIN32)
                if (ma_context_is_loopback_supported(context) == MA_FALSE) {
                    last_error_ = "loopback capture is not supported on this backend";
                    ma_context_uninit(&context_);
                    context_initialized_ = false;
                    return false;
                }
                have_device_id_ = false;
#elif defined(__APPLE__)
                bool found_blackhole = false;
                for (ma_uint32 i = 0; i < capture_count; ++i) {
                    if (contains_ignore_case(capture_infos[i].name, "blackhole")) {
                        device_id_ = capture_infos[i].id;
                        have_device_id_ = true;
                        found_blackhole = true;
                        break;
                    }
                }
                if (!found_blackhole) {
                    last_error_ =
                        "BlackHole device not found. Install blackhole-2ch and select it as part of a Multi-Output Device.";
                    ma_context_uninit(&context_);
                    context_initialized_ = false;
                    return false;
                }
#elif defined(__linux__)
                bool found_monitor = false;
                for (ma_uint32 i = 0; i < capture_count; ++i) {
                    if (contains_ignore_case(capture_infos[i].name, ".monitor")) {
                        device_id_ = capture_infos[i].id;
                        have_device_id_ = true;
                        found_monitor = true;
                        break;
                    }
                }
                if (!found_monitor) {
                    last_error_ =
                        "No PulseAudio monitor source found. Use 'pactl list sources short' and pass --device <monitor>.";
                    ma_context_uninit(&context_);
                    context_initialized_ = false;
                    return false;
                }
#else
                have_device_id_ = false;
#endif
            }

            if (have_device_id_) {
                config.capture.pDeviceID = &device_id_;
            }
        } else {
            context_initialized_ = false;
            have_device_id_ = false;
        }

        if (ma_device_init(context, &config, &device_) != MA_SUCCESS) {
            last_error_ = "failed to initialize audio capture device";
            if (context_initialized_) {
                ma_context_uninit(&context_);
                context_initialized_ = false;
            }
            have_device_id_ = false;
            return false;
        }

        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            if (context_initialized_) {
                ma_context_uninit(&context_);
                context_initialized_ = false;
            }
            have_device_id_ = false;
            last_error_ = "failed to start audio capture device";
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

void AudioEngine::stop() {
    if (mode_ == Mode::Capture) {
        if (!device_initialized_) {
            return;
        }

        ma_device_uninit(&device_);
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
        have_device_id_ = false;
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

std::size_t AudioEngine::read_samples(float* dest, std::size_t max_samples) {
    return ring_buffer_.read(dest, max_samples);
}

std::size_t AudioEngine::dropped_samples() const {
    return dropped_samples_.load(std::memory_order_relaxed);
}

void AudioEngine::data_callback(ma_device* device, void*, const void* input, ma_uint32 frame_count) {
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

void AudioEngine::file_stream_loop() {
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
        ma_result result = ma_decoder_read_pcm_frames(&decoder_, decode_buffer.data(), frames_requested, &frames_read);
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
            if (ma_resampler_process_pcm_frames(&resampler_, mono_buffer.data(), &input_frame_count, resample_buffer.data(),
                                                &output_frame_count) != MA_SUCCESS) {
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

} // namespace who

