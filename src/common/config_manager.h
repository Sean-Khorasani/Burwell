#ifndef BURWELL_CONFIG_MANAGER_H
#define BURWELL_CONFIG_MANAGER_H

#include <string>
#include <map>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadConfig(const std::string& configPath = "config/burwell.json");
    void saveConfig(const std::string& configPath = "config/burwell.json");
    
    // LLM Configuration
    std::string getLLMBaseUrl() const;
    std::string getLLMModelName() const;
    std::string getLLMApiKey() const;
    int getLLMTimeoutMs() const;
    int getLLMMaxRetries() const;
    
    // Security Configuration
    bool getRequireConfirmation() const;
    std::vector<std::string> getDangerousCommands() const;
    int getMaxExecutionTimeMs() const;
    bool getSandboxMode() const;
    
    // UI Configuration
    std::string getUITheme() const;
    bool getAutoScroll() const;
    bool getShowTimestamps() const;
    
    // Task Configuration
    std::string getTaskLibraryPath() const;
    bool getAutoSaveTask() const;
    bool getTaskVersioning() const;
    
    // Logging Configuration
    std::string getLogFile() const;
    std::string getLogLevel() const;
    int getLogMaxSizeMb() const;
    int getLogMaxFiles() const;
    std::string getLogSourceLocationLevels() const;
    
    // LLM Provider Configuration
    std::string getActiveProvider() const;
    std::string getBestAvailableProvider() const;
    std::vector<std::string> getAvailableProviders() const;
    bool hasProviderApiKey(const std::string& providerName) const;
    std::string getFallbackProvider() const;
    std::string getProviderConfigFile(const std::string& providerName) const;
    bool isProviderEnabled(const std::string& providerName) const;
    nlohmann::json loadProviderConfig(const std::string& providerName) const;
    bool getAutoDetectCapabilities() const;
    
    // Config path access
    std::string getConfigPath() const;
    
    // Script execution configuration
    int getMaxScriptNestingLevel() const;
    
    // Generic getters/setters
    template<typename T>
    T get(const std::string& key) const;
    
    template<typename T>
    void set(const std::string& key, const T& value);

private:
    ConfigManager();
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    nlohmann::json m_config;
    std::string m_configPath;
    
    void setDefaults();
    std::string getEnvironmentVariable(const std::string& name) const;
};

// Template implementations
template<typename T>
T ConfigManager::get(const std::string& key) const {
    if (m_config.contains(key)) {
        return m_config[key].get<T>();
    }
    throw std::runtime_error("Configuration key not found: " + key);
}

template<typename T>
void ConfigManager::set(const std::string& key, const T& value) {
    m_config[key] = value;
}

#endif // BURWELL_CONFIG_MANAGER_H