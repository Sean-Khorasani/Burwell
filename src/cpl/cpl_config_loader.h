#ifndef BURWELL_CPL_CONFIG_LOADER_H
#define BURWELL_CPL_CONFIG_LOADER_H

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "../common/structured_logger.h"

namespace burwell {
namespace cpl {

/**
 * CPL Configuration Loader
 * Loads execution settings from CPL commands.json file
 * Provides runtime access to user-configurable parameters
 */
class CPLConfigLoader {
public:
    static CPLConfigLoader& getInstance();
    
    // Initialize from CPL commands.json file
    bool loadFromFile(const std::string& cplCommandsPath);
    
    // Getters for execution settings
    int getMouseSpeed() const;
    bool getMouseSmoothMovement() const;
    int getMouseClickDelay() const;
    int getMouseDoubleClickInterval() const;
    int getMousePrecisionThreshold() const;
    
    int getKeyboardTypingSpeed() const;
    int getKeyboardKeyPressDuration() const;
    int getKeyboardKeyReleaseDelay() const;
    int getKeyboardModifierHoldTime() const;
    
    int getSystemDefaultCommandDelay() const;
    int getSystemErrorRetryDelay() const;
    int getSystemMaxExecutionTime() const;
    int getSystemScreenshotDelay() const;
    
    int getWindowFocusDelay() const;
    int getWindowResizeAnimationWait() const;
    int getWindowSearchTimeout() const;
    
    int getApplicationLaunchTimeout() const;
    int getApplicationShutdownTimeout() const;
    int getApplicationStartupDelay() const;
    
    int getOrchestratorMainLoopDelay() const;
    int getOrchestratorExecutionTimeout() const;
    int getOrchestratorCommandSequenceDelay() const;
    int getOrchestratorErrorRecoveryDelay() const;
    
    // Generic getter for any setting
    template<typename T>
    T getSetting(const std::string& category, const std::string& setting, T defaultValue) const;
    
    // Check if settings are loaded
    bool isLoaded() const { return m_loaded; }
    
    // Reload settings from file
    bool reload();
    
private:
    CPLConfigLoader() = default;
    ~CPLConfigLoader() = default;
    CPLConfigLoader(const CPLConfigLoader&) = delete;
    CPLConfigLoader& operator=(const CPLConfigLoader&) = delete;
    
    nlohmann::json m_executionSettings;
    std::string m_configPath;
    bool m_loaded = false;
    
    template<typename T>
    T getSettingValue(const std::string& category, const std::string& setting, T defaultValue) const;
};

template<typename T>
T CPLConfigLoader::getSetting(const std::string& category, const std::string& setting, T defaultValue) const {
    return getSettingValue(category, setting, defaultValue);
}

template<typename T>
T CPLConfigLoader::getSettingValue(const std::string& category, const std::string& setting, T defaultValue) const {
    if (!m_loaded) {
        SLOG_WARNING().message("CPL settings not loaded, using default value").context("category", category).context("setting", setting);
        return defaultValue;
    }
    
    try {
        if (m_executionSettings.contains(category) && 
            m_executionSettings[category].contains(setting) &&
            m_executionSettings[category][setting].contains("value")) {
            return m_executionSettings[category][setting]["value"].get<T>();
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to read CPL setting").context("category", category).context("setting", setting).context("error", e.what());
    }
    
    SLOG_WARNING().message("CPL setting not found, using default").context("category", category).context("setting", setting);
    return defaultValue;
}

} // namespace cpl
} // namespace burwell

#endif // BURWELL_CPL_CONFIG_LOADER_H