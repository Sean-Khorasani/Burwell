#ifndef BURWELL_EXTERNAL_CONFIG_LOADER_H
#define BURWELL_EXTERNAL_CONFIG_LOADER_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace burwell {
namespace cpl {

struct ConfigFile {
    std::string path;
    nlohmann::json data;
    std::filesystem::file_time_type lastModified;
    bool isValid;
    std::string errorMessage;
    bool isUserEditable;
    bool requiresBackup;
};

struct ConfigValidationResult {
    bool isValid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> suggestions;
};

class ExternalConfigLoader {
public:
    ExternalConfigLoader();
    ~ExternalConfigLoader() = default;
    
    // Initialization and setup
    bool initialize(const std::string& configBasePath);
    void setConfigBasePath(const std::string& basePath);
    std::string getConfigBasePath() const;
    
    // Core loading functionality
    bool loadAllConfigurations();
    bool loadCommandDefinitions();
    bool loadLLMProviderConfigs();
    bool loadUserSequences();
    bool loadUserPreferences();
    
    // Individual config access
    nlohmann::json getCommandDefinitions() const;
    nlohmann::json getLLMProviderConfig(const std::string& providerName) const;
    nlohmann::json getUserSequences() const;
    nlohmann::json getUserPreferences() const;
    
    // Dynamic reloading
    bool reloadIfModified();
    bool reloadConfiguration(const std::string& configType);
    void enableAutoReload(bool enable, int checkIntervalMs = 5000);
    
    // Configuration validation
    ConfigValidationResult validateCommandDefinitions(const nlohmann::json& config);
    ConfigValidationResult validateLLMProviderConfig(const nlohmann::json& config);
    ConfigValidationResult validateUserSequences(const nlohmann::json& config);
    
    // User modification support
    bool saveUserSequences(const nlohmann::json& sequences);
    bool saveUserPreferences(const nlohmann::json& preferences);
    bool saveCustomCommand(const std::string& commandName, const nlohmann::json& commandDef);
    bool saveLLMProviderConfig(const std::string& providerName, const nlohmann::json& config);
    
    // Backup and recovery
    bool createBackup(const std::string& configType);
    bool restoreFromBackup(const std::string& configType, const std::string& backupTimestamp = "");
    std::vector<std::string> listBackups(const std::string& configType);
    bool cleanupOldBackups(int maxBackups = 10);
    
    // Template and example management
    nlohmann::json getDefaultCommandTemplates();
    nlohmann::json getExampleSequences();
    bool installExampleConfigurations();
    bool resetToDefaults(const std::string& configType);
    
    // Configuration discovery and validation
    std::vector<std::string> findConfigurationFiles();
    std::map<std::string, bool> validateAllConfigurations();
    std::vector<std::string> getConfigurationIssues();
    
    // Error handling and diagnostics
    std::string getLastError() const;
    std::vector<std::string> getValidationErrors() const;
    bool hasConfigurationErrors() const;
    nlohmann::json getConfigurationStatus();
    
    // File watching and change detection
    void setFileChangeCallback(std::function<void(const std::string&, const std::string&)> callback);
    bool startFileWatcher();
    void stopFileWatcher();
    
    // Import/Export functionality
    bool exportConfiguration(const std::string& configType, const std::string& exportPath);
    bool importConfiguration(const std::string& configType, const std::string& importPath);
    bool exportAllConfigurations(const std::string& exportDir);
    
    // Configuration merging and inheritance
    nlohmann::json mergeConfigurations(const nlohmann::json& base, const nlohmann::json& override);
    bool supportsConfigInheritance() const;
    void setConfigInheritance(const std::string& configType, const std::string& parentConfig);

private:
    // Internal loading methods
    bool loadConfigFile(const std::string& filePath, ConfigFile& configFile);
    bool saveConfigFile(const std::string& filePath, const nlohmann::json& data);
    bool validateConfigStructure(const nlohmann::json& config, const std::string& schemaType);
    
    // File system operations
    bool ensureConfigDirectoryExists(const std::string& path);
    bool createDefaultConfigFile(const std::string& filePath, const std::string& configType);
    std::string generateBackupFilename(const std::string& configType);
    
    // Validation helpers
    bool validateCommandDefinition(const nlohmann::json& commandDef);
    bool validateParameterDefinition(const nlohmann::json& paramDef);
    bool validateSequenceDefinition(const nlohmann::json& sequenceDef);
    bool validateProviderConnection(const nlohmann::json& connectionConfig);
    
    // File watching implementation
    void fileWatcherThread();
    bool isFileModified(const std::string& filePath);
    void updateFileTimestamp(const std::string& filePath);
    
    // Default configuration generators
    nlohmann::json generateDefaultCommandDefinitions();
    nlohmann::json generateDefaultLLMProviderConfig(const std::string& providerName);
    nlohmann::json generateDefaultUserPreferences();
    nlohmann::json generateDefaultUserSequences();
    
    // Member variables
    std::string m_configBasePath;
    std::map<std::string, ConfigFile> m_configFiles;
    
    // Configuration data
    nlohmann::json m_commandDefinitions;
    std::map<std::string, nlohmann::json> m_llmProviderConfigs;
    nlohmann::json m_userSequences;
    nlohmann::json m_userPreferences;
    
    // File watching
    bool m_autoReloadEnabled;
    int m_checkIntervalMs;
    std::thread m_fileWatcherThread;
    std::atomic<bool> m_stopFileWatcher;
    std::function<void(const std::string&, const std::string&)> m_fileChangeCallback;
    
    // Error tracking
    std::string m_lastError;
    std::vector<std::string> m_validationErrors;
    
    // Configuration paths
    struct ConfigPaths {
        std::string commands;
        std::string llmProviders;
        std::string userSequences;
        std::string userPreferences;
        std::string backups;
        std::string templates;
    } m_paths;
    
    // Validation schemas (could be externalized too)
    std::map<std::string, nlohmann::json> m_validationSchemas;
    
    // Runtime state
    bool m_initialized;
    bool m_hasUnsavedChanges;
    std::chrono::system_clock::time_point m_lastReload;
};

// Utility functions for external configuration management
std::string expandConfigPath(const std::string& path);
bool isValidConfigFileName(const std::string& filename);
std::string generateConfigSchema(const std::string& configType);
nlohmann::json validateJsonSchema(const nlohmann::json& data, const nlohmann::json& schema);

} // namespace cpl
} // namespace burwell

#endif // BURWELL_EXTERNAL_CONFIG_LOADER_H