#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_engine.h"
#include "config.h"

namespace who {

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual std::string id() const = 0;
    virtual void on_load(const AppConfig& config) = 0;
    virtual void on_frame(const AudioMetrics& metrics,
                          const std::vector<float>& bands,
                          float beat_strength,
                          double time_s) = 0;
};

using PluginFactory = std::function<std::unique_ptr<Plugin>()>;

class PluginManager {
public:
    void register_factory(const std::string& id, PluginFactory factory);
    void load_from_config(const AppConfig& config);
    void notify_frame(const AudioMetrics& metrics,
                      const std::vector<float>& bands,
                      float beat_strength,
                      double time_s);

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::unordered_map<std::string, PluginFactory> factories_;
    std::vector<std::unique_ptr<Plugin>> active_;
    std::vector<std::string> warnings_;
};

void register_builtin_plugins(PluginManager& manager);

} // namespace who

