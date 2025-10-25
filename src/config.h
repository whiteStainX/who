#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "renderer.h"

namespace who {

struct AudioCaptureConfig {
    bool enabled = true;
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
    std::size_t ring_frames = 8192;
    std::string device;
    float input_gain = 1.0f;
    bool system = false;
};

struct AudioFileConfig {
    bool enabled = true;
    std::string path;
    std::uint32_t channels = 1;
    float gain = 1.0f;
};

struct AudioConfig {
    AudioCaptureConfig capture;
    AudioFileConfig file;
    bool prefer_file = false;
};

struct DspConfig {
    std::size_t fft_size = 1024;
    std::size_t hop_size = 256;
    std::size_t bands = 32;
    std::string window = "hann";
    float smoothing_attack = 0.2f;
    float smoothing_release = 0.05f;
    float beat_sensitivity = 1.0f;
    bool enable_flux = true;
};

struct GridConfig {
    int rows = 16;
    int cols = 16;
    int min_dim = 8;
    int max_dim = 32;
};

struct SensitivityConfig {
    float value = 1.0f;
    float min_value = 0.2f;
    float max_value = 5.0f;
    float step = 0.1f;
};

struct VisualConfig {
    GridConfig grid;
    SensitivityConfig sensitivity;
    VisualizationMode default_mode = VisualizationMode::Bands;
    ColorPalette default_palette = ColorPalette::Rainbow;
    double target_fps = 60.0;
};

struct RuntimeConfig {
    bool show_metrics = true;
    bool allow_resize = true;
    bool beat_flash = true;
    bool show_overlay_metrics = false; // New config option, default to false
};

struct PluginConfig {
    std::string directory = "plugins";
    std::vector<std::string> autoload;
    bool safe_mode = false;
};

struct AppConfig {
    std::string log_level = "info";
    AudioConfig audio;
    DspConfig dsp;
    VisualConfig visual;
    RuntimeConfig runtime;
    PluginConfig plugins;
};

struct ConfigLoadResult {
    AppConfig config;
    std::vector<std::string> warnings;
    bool loaded_file = false;
};

ConfigLoadResult load_app_config(const std::string& path);
VisualizationMode visualization_mode_from_string(const std::string& value,
                                                  VisualizationMode fallback = VisualizationMode::Bands);
ColorPalette color_palette_from_string(const std::string& value,
                                       ColorPalette fallback = ColorPalette::Rainbow);

} // namespace who

