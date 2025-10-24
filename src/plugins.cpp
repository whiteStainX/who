#include "plugins.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace who {
namespace {

class BeatFlashDebugPlugin final : public Plugin {
public:
    std::string id() const override { return "beat-flash-debug"; }

    void on_load(const AppConfig& config) override {
        enabled_ = config.runtime.beat_flash;
        threshold_ = std::max(0.35f, static_cast<float>(config.dsp.beat_sensitivity));
        last_log_time_ = -10.0;
        log_interval_ = 1.0;
        if (!enabled_) {
            std::clog << "[plugin] beat-flash-debug disabled via runtime.beat_flash" << std::endl;
            return;
        }
        open_log(config.plugins.directory);
        if (!log_) {
            enabled_ = false;
            std::clog << "[plugin] beat-flash-debug logging unavailable; disabling plugin" << std::endl;
            return;
        }
        std::clog << "[plugin] beat-flash-debug armed (threshold=" << threshold_ << ", log='" << log_path_
                  << "')" << std::endl;
        log_header();
    }

    void on_frame(const AudioMetrics&, const std::vector<float>&, float beat_strength, double time_s) override {
        if (!enabled_) {
            return;
        }
        if (beat_strength < threshold_) {
            return;
        }
        if (time_s - last_log_time_ < log_interval_) {
            return;
        }
        last_log_time_ = time_s;
        write_log(beat_strength, time_s);
    }

private:
    void open_log(const std::string& directory) {
        log_.close();
        log_path_.clear();
        std::filesystem::path base_path;
        if (!directory.empty()) {
            base_path = std::filesystem::path(directory);
            std::error_code ec;
            std::filesystem::create_directories(base_path, ec);
            if (ec) {
                std::clog << "[plugin] beat-flash-debug failed to create directory '" << base_path.string()
                          << "' (" << ec.message() << ")" << std::endl;
                base_path.clear();
            }
        }
        const std::filesystem::path log_path = base_path.empty() ? std::filesystem::path("beat-flash-debug.log")
                                                                 : base_path / "beat-flash-debug.log";
        log_path_ = log_path.string();
        log_.open(log_path_, std::ios::out | std::ios::app);
    }

    void log_header() {
        if (!log_) {
            return;
        }
        log_ << "\n=== beat-flash-debug session started ===\n";
        log_.flush();
    }

    void write_log(float beat_strength, double time_s) {
        if (!log_) {
            return;
        }
        std::ostringstream line;
        line << std::fixed << std::setprecision(3) << time_s << "s beat_strength=" << beat_strength;
        log_ << line.str() << '\n';
        log_.flush();
    }

    bool enabled_ = true;
    float threshold_ = 0.75f;
    double last_log_time_ = 0.0;
    double log_interval_ = 1.0;
    std::ofstream log_;
    std::string log_path_;
};

} // namespace

void PluginManager::register_factory(const std::string& id, PluginFactory factory) {
    factories_[id] = std::move(factory);
}

void PluginManager::load_from_config(const AppConfig& config) {
    warnings_.clear();
    active_.clear();
    if (config.plugins.safe_mode) {
        warnings_.push_back("Plug-ins disabled by plugins.safe_mode");
        return;
    }
    for (const std::string& id : config.plugins.autoload) {
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            warnings_.push_back("Unknown plugin '" + id + "'");
            continue;
        }
        std::unique_ptr<Plugin> plugin = it->second();
        if (!plugin) {
            warnings_.push_back("Factory for plugin '" + id + "' returned null");
            continue;
        }
        plugin->on_load(config);
        active_.push_back(std::move(plugin));
    }
}

void PluginManager::notify_frame(const AudioMetrics& metrics,
                                 const std::vector<float>& bands,
                                 float beat_strength,
                                 double time_s) {
    for (const std::unique_ptr<Plugin>& plugin : active_) {
        plugin->on_frame(metrics, bands, beat_strength, time_s);
    }
}

void register_builtin_plugins(PluginManager& manager) {
    manager.register_factory("beat-flash-debug", []() { return std::make_unique<BeatFlashDebugPlugin>(); });
}

} // namespace who

