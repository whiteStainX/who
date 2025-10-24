#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

extern "C" {
#include <kiss_fft.h>
}

namespace who {

class DspEngine {
public:
    static constexpr std::size_t kDefaultFftSize = 1024;
    static constexpr std::size_t kDefaultHopSize = kDefaultFftSize / 2;
    static constexpr std::size_t kDefaultBands = 16;

    DspEngine(std::uint32_t sample_rate,
              std::uint32_t channels,
              std::size_t fft_size = kDefaultFftSize,
              std::size_t hop_size = kDefaultHopSize,
              std::size_t bands = kDefaultBands);
    ~DspEngine();

    void push_samples(const float* interleaved_samples, std::size_t count);

    const std::vector<float>& band_energies() const { return band_energies_; }
    float beat_strength() const { return beat_strength_; }

private:
    void compute_band_ranges();
    void process_frame();

    std::uint32_t sample_rate_;
    std::uint32_t channels_;
    std::size_t fft_size_;
    std::size_t hop_size_;

    std::vector<float> window_;
    std::vector<float> frame_buffer_;
    std::deque<float> mono_fifo_;

    std::vector<float> band_energies_;
    std::vector<std::pair<std::size_t, std::size_t>> band_bin_ranges_;
    std::vector<float> prev_magnitudes_;

    kiss_fft_cfg fft_cfg_;
    std::vector<kiss_fft_cpx> fft_in_;
    std::vector<kiss_fft_cpx> fft_out_;

    float smoothing_attack_;
    float smoothing_release_;
    float flux_average_;
    float beat_strength_;
};

} // namespace who
