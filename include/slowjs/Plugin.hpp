#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace slowjs {

class JSEngine;
class JSModule;

class IEnginePlugin {
public:
    virtual ~IEnginePlugin() = default;
    virtual const char* name() const = 0;
    virtual void install(JSEngine& engine, JSModule& root) = 0;
};

using PluginPtr = std::unique_ptr<IEnginePlugin>;

class PluginRegistry {
public:
    void add(PluginPtr plugin) {
        if (!plugin) return;
        plugins_.emplace(plugin->name(), std::move(plugin));
    }

    template <typename PluginT, typename... Args>
    PluginT& emplace(Args&&... args) {
        auto p = std::make_unique<PluginT>(std::forward<Args>(args)...);
        PluginT& ref = *p;
        add(std::move(p));
        return ref;
    }

    void installAll(JSEngine& engine, JSModule& root) const;

private:
    std::unordered_map<std::string, PluginPtr> plugins_;
};

} // namespace slowjs

