#include "config_manager.h"
#include "structured_logger.h"
#include "os_utils.h"
#include "file_utils.h"
#include "json_utils.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

using namespace burwell;

ConfigManager::ConfigManager() {
    setDefaults();
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    m_configPath = configPath;
    
    try {
        // Use FileUtils for consistent JSON loading with proper validation
        if (utils::FileUtils::loadJsonFromFile(configPath, m_config)) {
            SLOG_INFO().message("Configuration loaded from").context("config_path", configPath);
            return true;
        } else {
            SLOG_WARNING().message("Config file not found or invalid, using defaults").context("config_path", configPath);
            setDefaults();
            saveConfig(configPath);
            return true;
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to load config").context("error", e.what());
        setDefaults();
        return false;
    }
}

void ConfigManager::saveConfig(const std::string& configPath) {
    try {
        // Use FileUtils for consistent JSON saving with proper validation
        if (utils::FileUtils::saveJsonToFile(configPath, m_config)) {
            SLOG_INFO().message("Configuration saved to").context("config_path", configPath);
        } else {
            SLOG_ERROR().message("Failed to save configuration to").context("config_path", configPath);
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to save config").context("error", e.what());
    }
}

void ConfigManager::setDefaults() {
    m_config = nlohmann::json{
        {"llm", {
            {"base_url", "https://api.together.xyz/v1"},
            {"model_name", "meta-llama/Llama-3-70b-chat-hf"},
            {"api_key", ""},
            {"api_key_env", "TOGETHER_API_KEY"},
            {"timeout_ms", 30000},
            {"max_retries", 3},
            {"context_window", 4000}
        }},
        {"security", {
            {"require_confirmation", true},
            {"dangerous_commands", nlohmann::json::array({"kill", "delete", "format", "shutdown", "restart"})},
            {"max_execution_time_ms", 300000},
            {"sandbox_mode", false}
        }},
        {"logging", {
            {"level", "INFO"},
            {"file", "logs/burwell.log"},
            {"max_size_mb", 100},
            {"max_files", 10},
            {"source_location_levels", "NONE"}
        }},
        {"ui", {
            {"theme", "dark"},
            {"auto_scroll", true},
            {"show_timestamps", true}
        }},
        {"tasks", {
            {"library_path", "tasks/"},
            {"auto_save", true},
            {"versioning", true}
        }}
    };
}

std::string ConfigManager::getEnvironmentVariable(const std::string& name) const {
    return os::SystemInfo::getEnvironmentVariable(name);
}

// LLM Configuration
std::string ConfigManager::getLLMBaseUrl() const {
    if (m_config.is_null() || !m_config.contains("llm") || !m_config["llm"].contains("base_url")) {
        return "https://api.together.xyz/v1"; // Default Together AI URL
    }
    return m_config["llm"]["base_url"].get<std::string>();
}

std::string ConfigManager::getLLMModelName() const {
    if (m_config.is_null() || !m_config.contains("llm") || !m_config["llm"].contains("model_name")) {
        return "meta-llama/Llama-3-70b-chat-hf"; // Default Together AI model
    }
    return m_config["llm"]["model_name"].get<std::string>();
}

std::string ConfigManager::getLLMApiKey() const {
    // First check if API key is directly configured in the config file
    if (m_config.contains("llm") && m_config["llm"].contains("api_key")) {
        std::string configApiKey = m_config["llm"]["api_key"].get<std::string>();
        if (!configApiKey.empty()) {
            return configApiKey;
        }
    }
    
    // Fallback to environment variable
    std::string envVar = "TOGETHER_API_KEY"; // Default env var
    if (m_config.contains("llm") && m_config["llm"].contains("api_key_env")) {
        envVar = m_config["llm"]["api_key_env"].get<std::string>();
    }
    
    std::string apiKey = getEnvironmentVariable(envVar);
    if (apiKey.empty()) {
        SLOG_WARNING().message("API key not found in config file or environment variable").context("env_var", envVar);
    }
    return apiKey;
}

int ConfigManager::getLLMTimeoutMs() const {
    return m_config["llm"]["timeout_ms"].get<int>();
}

int ConfigManager::getLLMMaxRetries() const {
    return m_config["llm"]["max_retries"].get<int>();
}

// Security Configuration
bool ConfigManager::getRequireConfirmation() const {
    return m_config["security"]["require_confirmation"].get<bool>();
}

std::vector<std::string> ConfigManager::getDangerousCommands() const {
    return m_config["security"]["dangerous_commands"].get<std::vector<std::string>>();
}

int ConfigManager::getMaxExecutionTimeMs() const {
    return m_config["security"]["max_execution_time_ms"].get<int>();
}

bool ConfigManager::getSandboxMode() const {
    return m_config["security"]["sandbox_mode"].get<bool>();
}

// UI Configuration
std::string ConfigManager::getUITheme() const {
    return m_config["ui"]["theme"].get<std::string>();
}

bool ConfigManager::getAutoScroll() const {
    return m_config["ui"]["auto_scroll"].get<bool>();
}

bool ConfigManager::getShowTimestamps() const {
    return m_config["ui"]["show_timestamps"].get<bool>();
}

// Task Configuration
std::string ConfigManager::getTaskLibraryPath() const {
    return m_config["tasks"]["library_path"].get<std::string>();
}

bool ConfigManager::getAutoSaveTask() const {
    return m_config["tasks"]["auto_save"].get<bool>();
}

bool ConfigManager::getTaskVersioning() const {
    return m_config["tasks"]["versioning"].get<bool>();
}

// Logging Configuration
std::string ConfigManager::getLogFile() const {
    if (m_config.contains("logging") && m_config["logging"].contains("file")) {
        return m_config["logging"]["file"].get<std::string>();
    }
    return "logs/burwell.log"; // Default value
}

std::string ConfigManager::getLogLevel() const {
    if (m_config.contains("logging") && m_config["logging"].contains("log_level")) {
        return m_config["logging"]["log_level"].get<std::string>();
    }
    // Fallback to check "level" for backward compatibility
    if (m_config.contains("logging") && m_config["logging"].contains("level")) {
        return m_config["logging"]["level"].get<std::string>();
    }
    return "INFO"; // Default value
}

int ConfigManager::getLogMaxSizeMb() const {
    if (m_config.contains("logging") && m_config["logging"].contains("max_size_mb")) {
        return m_config["logging"]["max_size_mb"].get<int>();
    }
    return 100; // Default value
}

int ConfigManager::getLogMaxFiles() const {
    if (m_config.contains("logging") && m_config["logging"].contains("max_files")) {
        return m_config["logging"]["max_files"].get<int>();
    }
    return 10; // Default value
}

std::string ConfigManager::getLogSourceLocationLevels() const {
    if (m_config.contains("logging") && m_config["logging"].contains("source_location_levels")) {
        return m_config["logging"]["source_location_levels"].get<std::string>();
    }
    return "NONE"; // Default value - no source location logging
}

// LLM Provider Configuration
std::string ConfigManager::getActiveProvider() const {
    // First try to get the best available provider based on API keys
    std::string bestProvider = getBestAvailableProvider();
    if (!bestProvider.empty()) {
        return bestProvider;
    }
    
    // Fallback to configured active provider
    if (m_config.contains("llm_provider") && m_config["llm_provider"].contains("active_provider")) {
        return m_config["llm_provider"]["active_provider"].get<std::string>();
    }
    return "together_ai"; // Default value
}

std::string ConfigManager::getBestAvailableProvider() const {
    auto providers = getAvailableProviders();
    
    // Sort by priority and check for API keys
    for (const auto& provider : providers) {
        if (isProviderEnabled(provider) && hasProviderApiKey(provider)) {
            SLOG_INFO().message("Auto-selected provider").context("provider", provider).context("reason", "has API key");
            return provider;
        }
    }
    
    return ""; // No provider with API key found
}

std::vector<std::string> ConfigManager::getAvailableProviders() const {
    std::vector<std::string> providers;
    
    if (m_config.contains("llm_provider") && m_config["llm_provider"].contains("providers")) {
        const auto& providersConfig = m_config["llm_provider"]["providers"];
        
        // Collect providers with their priorities
        std::vector<std::pair<std::string, int>> providerPriorities;
        for (const auto& [name, config] : providersConfig.items()) {
            int priority = config.contains("priority") ? config["priority"].get<int>() : 999;
            providerPriorities.emplace_back(name, priority);
        }
        
        // Sort by priority (lower number = higher priority)
        std::sort(providerPriorities.begin(), providerPriorities.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        
        for (const auto& [name, priority] : providerPriorities) {
            providers.push_back(name);
        }
    }
    
    return providers;
}

bool ConfigManager::hasProviderApiKey(const std::string& providerName) const {
    try {
        nlohmann::json providerConfig = loadProviderConfig(providerName);
        
        if (providerConfig.contains("connection") && 
            providerConfig["connection"].contains("authentication") &&
            providerConfig["connection"]["authentication"].contains("env_variable")) {
            
            std::string envVar = providerConfig["connection"]["authentication"]["env_variable"].get<std::string>();
            std::string apiKey = burwell::os::SystemInfo::getEnvironmentVariable(envVar);
            
            if (!apiKey.empty()) {
                return true;
            }
        }
        
        // Check if API key is stored directly in config
        if (providerConfig.contains("api_key") && !providerConfig["api_key"].get<std::string>().empty()) {
            return true;
        }
        
    } catch (const std::exception& e) {
        SLOG_DEBUG().message("Failed to check API key for provider").context("provider", providerName).context("error", e.what());
    }
    
    return false;
}

std::string ConfigManager::getFallbackProvider() const {
    if (m_config.contains("llm_provider") && m_config["llm_provider"].contains("fallback_provider")) {
        return m_config["llm_provider"]["fallback_provider"].get<std::string>();
    }
    return "local"; // Default value
}

std::string ConfigManager::getProviderConfigFile(const std::string& providerName) const {
    if (m_config.contains("llm_provider") && 
        m_config["llm_provider"].contains("providers") &&
        m_config["llm_provider"]["providers"].contains(providerName) &&
        m_config["llm_provider"]["providers"][providerName].contains("config_file")) {
        return m_config["llm_provider"]["providers"][providerName]["config_file"].get<std::string>();
    }
    return "llm_providers/" + providerName + ".json"; // Default pattern
}

bool ConfigManager::isProviderEnabled(const std::string& providerName) const {
    if (m_config.contains("llm_provider") && 
        m_config["llm_provider"].contains("providers") &&
        m_config["llm_provider"]["providers"].contains(providerName) &&
        m_config["llm_provider"]["providers"][providerName].contains("enabled")) {
        return m_config["llm_provider"]["providers"][providerName]["enabled"].get<bool>();
    }
    return true; // Default to enabled
}

nlohmann::json ConfigManager::loadProviderConfig(const std::string& providerName) const {
    nlohmann::json providerConfig;
    
    try {
        std::string configFile = getProviderConfigFile(providerName);
        
        // Create full path relative to main config directory
        std::filesystem::path mainConfigPath(m_configPath);
        std::filesystem::path providerConfigPath = mainConfigPath.parent_path() / configFile;
        
        if (std::filesystem::exists(providerConfigPath)) {
            std::ifstream file(providerConfigPath);
            file >> providerConfig;
            SLOG_DEBUG().message("Provider config loaded").context("config_path", os::PathUtils::toNativePath(providerConfigPath.string()));
        } else {
            SLOG_WARNING().message("Provider config not found").context("config_path", os::PathUtils::toNativePath(providerConfigPath.string()));
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to load provider config").context("provider", providerName).context("error", e.what());
    }
    
    return providerConfig;
}

bool ConfigManager::getAutoDetectCapabilities() const {
    if (m_config.contains("llm_provider") && m_config["llm_provider"].contains("auto_detect_capabilities")) {
        return m_config["llm_provider"]["auto_detect_capabilities"].get<bool>();
    }
    return true; // Default to auto-detect
}

std::string ConfigManager::getConfigPath() const {
    return m_configPath;
}

int ConfigManager::getMaxScriptNestingLevel() const {
    if (m_config.contains("orchestrator") && m_config["orchestrator"].contains("max_script_nesting_level")) {
        return m_config["orchestrator"]["max_script_nesting_level"].get<int>();
    }
    return 3; // Default value
}