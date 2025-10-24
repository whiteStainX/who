#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

extern "C" {
#include <kiss_fft.h>
}

namespace who {

namespace {
constexpr float kMinDisplayFrequency = 20.0f;
constexpr float kPi = 3.14159265358979323846f;
} // namespace

DspEngine::DspEngine(std::uint32_t sample_rate,
                     std::uint32_t channels,
                     std::size_t fft_size,
                     std::size_t hop_size,
                     std::size_t bands)
    : sample_rate_(sample_rate),
      channels_(channels),
      fft_size_(fft_size),
      hop_size_(hop_size),
      window_(fft_size_, 0.0f),
      frame_buffer_(fft_size_, 0.0f),
      band_energies_(bands, 0.0f),
      band_bin_ranges_(bands),
      prev_magnitudes_(bands, 0.0f),
      fft_cfg_(nullptr),
      fft_in_(fft_size_),
      fft_out_(fft_size_),
      smoothing_attack_(0.35f),
      smoothing_release_(0.08f),
      flux_average_(0.0f),
      beat_strength_(0.0f) {
    if (fft_size_ < 2 || (fft_size_ & (fft_size_ - 1)) != 0) {
        throw std::invalid_argument("FFT size must be a power of two greater than 1");
    }
    if (hop_size_ == 0 || hop_size_ > fft_size_) {
        throw std::invalid_argument("Invalid hop size");
    }
    if (channels_ == 0) {
        throw std::invalid_argument("Channels must be non-zero");
    }

    const float denominator = static_cast<float>(fft_size_ - 1);
    for (std::size_t i = 0; i < fft_size_; ++i) {
        const float phase = (denominator == 0.0f) ? 0.0f : (2.0f * kPi * static_cast<float>(i)) / denominator;
        const float w = 0.5f - 0.5f * std::cos(phase);
        window_[i] = w;
    }

    fft_cfg_ = kiss_fft_alloc(static_cast<int>(fft_size_), 0, nullptr, nullptr);
    if (!fft_cfg_) {
        throw std::runtime_error("Failed to allocate FFT config");
    }

    compute_band_ranges();
}

DspEngine::~DspEngine() {
    if (fft_cfg_) {
        kiss_fft_free(fft_cfg_);
        fft_cfg_ = nullptr;
    }
}

void DspEngine::push_samples(const float* interleaved_samples, std::size_t count) {
    if (!interleaved_samples || count == 0) {
        return;
    }

    const std::size_t frames = count / channels_;
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (std::size_t ch = 0; ch < channels_; ++ch) {
            sum += interleaved_samples[i * channels_ + ch];
        }
        mono_fifo_.push_back(static_cast<float>(sum / static_cast<double>(channels_)));
    }

    while (mono_fifo_.size() >= hop_size_) {
        std::memmove(frame_buffer_.data(), frame_buffer_.data() + hop_size_,
                     (fft_size_ - hop_size_) * sizeof(float));

        for (std::size_t i = 0; i < hop_size_; ++i) {
            frame_buffer_[fft_size_ - hop_size_ + i] = mono_fifo_.front();
            mono_fifo_.pop_front();
        }

        process_frame();
    }
}

void DspEngine::compute_band_ranges() {
    const std::size_t bands = band_energies_.size();
    if (bands == 0) {
        return;
    }

    const float nyquist = std::max(static_cast<float>(sample_rate_) * 0.5f, kMinDisplayFrequency * 1.1f);
    const float bin_width = static_cast<float>(sample_rate_) / static_cast<float>(fft_size_);
    const float min_freq = std::max(kMinDisplayFrequency, bin_width);
    const float log_min = std::log(min_freq);
    const float log_max = std::log(nyquist);

    for (std::size_t i = 0; i < bands; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(bands);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(bands);
        const float f0 = (i == 0) ? 0.0f : std::exp(log_min + (log_max - log_min) * t0);
        const float f1 = std::exp(log_min + (log_max - log_min) * t1);

        std::size_t bin0 = static_cast<std::size_t>(std::floor(f0 / bin_width));
        std::size_t bin1 = static_cast<std::size_t>(std::ceil(f1 / bin_width));

        bin0 = std::min(bin0, fft_size_ / 2);
        bin1 = std::clamp(bin1, bin0 + 1, fft_size_ / 2 + 1);

        band_bin_ranges_[i] = {bin0, bin1};
    }
}

void DspEngine::process_frame() {
    if (!fft_cfg_) {
        return;
    }

    const float norm = 1.0f / static_cast<float>(fft_size_);

    for (std::size_t i = 0; i < fft_size_; ++i) {
        const float windowed = frame_buffer_[i] * window_[i];
        fft_in_[i].r = windowed;
        fft_in_[i].i = 0.0f;
    }

    kiss_fft(fft_cfg_, fft_in_.data(), fft_out_.data());

    float flux = 0.0f;
    for (std::size_t band = 0; band < band_bin_ranges_.size(); ++band) {
        const auto [start_bin, end_bin] = band_bin_ranges_[band];
        float energy = 0.0f;
        for (std::size_t bin = start_bin; bin < end_bin && bin <= fft_size_ / 2; ++bin) {
            const float real = fft_out_[bin].r * norm;
            const float imag = fft_out_[bin].i * norm;
            energy += real * real + imag * imag;
        }
        const std::size_t bin_count = (end_bin > start_bin) ? (end_bin - start_bin) : 1;
        const float average_energy = energy / static_cast<float>(bin_count);
        const float magnitude = std::sqrt(std::max(average_energy, 0.0f));
        const float previous = (band < prev_magnitudes_.size()) ? prev_magnitudes_[band] : 0.0f;
        if (band < prev_magnitudes_.size()) {
            prev_magnitudes_[band] = magnitude;
        }
        flux += std::max(0.0f, magnitude - previous);
        const float current = band_energies_[band];
        const float target = magnitude;
        const float alpha = (target > current) ? smoothing_attack_ : smoothing_release_;
        band_energies_[band] = current + (target - current) * alpha;
    }

    flux_average_ = flux_average_ * 0.92f + flux * 0.08f;
    const float baseline = std::max(flux_average_ * 1.35f, 1e-4f);
    float beat_instant = 0.0f;
    if (flux > baseline) {
        beat_instant = std::min((flux - baseline) / baseline, 1.0f);
    }
    beat_strength_ = std::max(beat_instant, beat_strength_ * 0.6f);
    beat_strength_ = std::clamp(beat_strength_, 0.0f, 1.0f);
}

} // namespace who
