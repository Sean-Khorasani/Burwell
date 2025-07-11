#include "cpl_config_loader.h"
#include "../common/os_utils.h"
#include "../common/file_utils.h"
#include "../common/json_utils.h"
#include "../common/structured_logger.h"
#include <fstream>

namespace burwell {
namespace cpl {

CPLConfigLoader& CPLConfigLoader::getInstance() {
    static CPLConfigLoader instance;
    return instance;
}

bool CPLConfigLoader::loadFromFile(const std::string& cplCommandsPath) {
    try {
        m_configPath = cplCommandsPath;
        
        // Use FileUtils for consistent JSON loading with proper validation
        nlohmann::json fullConfig;
        if (!utils::FileUtils::loadJsonFromFile(cplCommandsPath, fullConfig)) {
            SLOG_ERROR().message("Failed to load CPL commands file").context("path", cplCommandsPath);
            return false;
        }
        
        // Use JsonUtils for safe field extraction
        nlohmann::json executionSettings;
        if (utils::JsonUtils::getObjectField(fullConfig, "execution_settings", executionSettings)) {
            m_executionSettings = executionSettings;
            m_loaded = true;
            SLOG_INFO().message("CPL execution settings loaded").context("path", cplCommandsPath);
            return true;
        } else {
            SLOG_WARNING().message("No execution_settings section found in CPL commands file");
            m_loaded = false;
            return false;
        }
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to load CPL settings").context("error", e.what());
        m_loaded = false;
        return false;
    }
}

bool CPLConfigLoader::reload() {
    if (m_configPath.empty()) {
        SLOG_ERROR().message("Cannot reload CPL settings: no config path set");
        return false;
    }
    return loadFromFile(m_configPath);
}

// Mouse settings
int CPLConfigLoader::getMouseSpeed() const {
    return getSettingValue<int>("mouse", "speed", 50);
}

bool CPLConfigLoader::getMouseSmoothMovement() const {
    return getSettingValue<bool>("mouse", "smooth_movement", true);
}

int CPLConfigLoader::getMouseClickDelay() const {
    return getSettingValue<int>("mouse", "click_delay", 50);
}

int CPLConfigLoader::getMouseDoubleClickInterval() const {
    return getSettingValue<int>("mouse", "double_click_interval", 200);
}

int CPLConfigLoader::getMousePrecisionThreshold() const {
    return getSettingValue<int>("mouse", "move_precision_threshold", 1);
}

// Keyboard settings
int CPLConfigLoader::getKeyboardTypingSpeed() const {
    return getSettingValue<int>("keyboard", "typing_speed", 50);
}

int CPLConfigLoader::getKeyboardKeyPressDuration() const {
    return getSettingValue<int>("keyboard", "key_press_duration", 50);
}

int CPLConfigLoader::getKeyboardKeyReleaseDelay() const {
    return getSettingValue<int>("keyboard", "key_release_delay", 25);
}

int CPLConfigLoader::getKeyboardModifierHoldTime() const {
    return getSettingValue<int>("keyboard", "modifier_hold_time", 100);
}

// System settings
int CPLConfigLoader::getSystemDefaultCommandDelay() const {
    return getSettingValue<int>("system", "default_command_delay", 100);
}

int CPLConfigLoader::getSystemErrorRetryDelay() const {
    return getSettingValue<int>("system", "error_retry_delay", 1000);
}

int CPLConfigLoader::getSystemMaxExecutionTime() const {
    return getSettingValue<int>("system", "max_execution_time", 30000);
}

int CPLConfigLoader::getSystemScreenshotDelay() const {
    return getSettingValue<int>("system", "screenshot_delay", 500);
}

// Window settings
int CPLConfigLoader::getWindowFocusDelay() const {
    return getSettingValue<int>("window", "focus_delay", 250);
}

int CPLConfigLoader::getWindowResizeAnimationWait() const {
    return getSettingValue<int>("window", "resize_animation_wait", 300);
}

int CPLConfigLoader::getWindowSearchTimeout() const {
    return getSettingValue<int>("window", "search_timeout", 5000);
}

// Application settings
int CPLConfigLoader::getApplicationLaunchTimeout() const {
    return getSettingValue<int>("application", "launch_timeout", 10000);
}

int CPLConfigLoader::getApplicationShutdownTimeout() const {
    return getSettingValue<int>("application", "shutdown_timeout", 5000);
}

int CPLConfigLoader::getApplicationStartupDelay() const {
    return getSettingValue<int>("application", "startup_delay", 1000);
}

// Orchestrator settings
int CPLConfigLoader::getOrchestratorMainLoopDelay() const {
    return getSettingValue<int>("orchestrator", "main_loop_delay", 100);
}

int CPLConfigLoader::getOrchestratorExecutionTimeout() const {
    return getSettingValue<int>("orchestrator", "execution_timeout", 300000);
}

int CPLConfigLoader::getOrchestratorCommandSequenceDelay() const {
    return getSettingValue<int>("orchestrator", "command_sequence_delay", 50);
}

int CPLConfigLoader::getOrchestratorErrorRecoveryDelay() const {
    return getSettingValue<int>("orchestrator", "error_recovery_delay", 2000);
}

} // namespace cpl
} // namespace burwell