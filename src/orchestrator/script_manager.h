#ifndef BURWELL_SCRIPT_MANAGER_H
#define BURWELL_SCRIPT_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declarations
class ExecutionEngine;
struct ExecutionContext;

/**
 * @class ScriptManager
 * @brief Manages script loading, validation, and nested script execution
 * 
 * This class handles all script-related functionality including loading
 * script files, managing nested script execution, and script caching.
 */
class ScriptManager {
public:
    ScriptManager();
    ~ScriptManager();

    // Dependencies
    void setExecutionEngine(std::shared_ptr<ExecutionEngine> engine);

    // Configuration
    void setMaxNestingLevel(int maxLevel);
    void setScriptCachingEnabled(bool enabled);
    void setScriptDirectory(const std::string& directory);

    // Script execution
    TaskExecutionResult executeScriptFile(const std::string& scriptPath, ExecutionContext& context);
    TaskExecutionResult executeScriptCommand(const nlohmann::json& command, ExecutionContext& context);
    
    // Script loading and validation
    nlohmann::json loadScript(const std::string& scriptPath);
    bool validateScript(const nlohmann::json& script);
    bool validateScriptPath(const std::string& scriptPath);
    
    // Variable management
    void loadScriptVariables(const nlohmann::json& script, ExecutionContext& context);
    void inheritParentVariables(const ExecutionContext& parentContext, ExecutionContext& childContext);
    
    // Nested script management
    bool canExecuteNestedScript(const ExecutionContext& context) const;
    void pushScriptToStack(ExecutionContext& context, const std::string& scriptPath);
    void popScriptFromStack(ExecutionContext& context);
    bool isScriptInStack(const ExecutionContext& context, const std::string& scriptPath) const;
    
    // Script discovery
    std::vector<std::string> listAvailableScripts() const;
    std::vector<std::string> findScriptsByPattern(const std::string& pattern) const;
    bool scriptExists(const std::string& scriptName) const;
    
    // Script metadata
    struct ScriptMetadata {
        std::string name;
        std::string description;
        std::string version;
        std::vector<std::string> requiredParameters;
        std::vector<std::string> optionalParameters;
        std::vector<std::string> dependencies;
        std::string author;
        std::string lastModified;
    };
    
    ScriptMetadata getScriptMetadata(const std::string& scriptPath);
    
    // Cache management
    void clearScriptCache();
    size_t getCacheSize() const;
    void preloadScript(const std::string& scriptPath);

private:
    // Dependencies
    std::shared_ptr<ExecutionEngine> m_executionEngine;
    
    // Configuration
    int m_maxNestingLevel;
    bool m_cachingEnabled;
    std::string m_scriptDirectory;
    
    // Script cache
    std::map<std::string, nlohmann::json> m_scriptCache;
    mutable std::mutex m_cacheMutex;
    
    // Helper methods
    std::string resolveScriptPath(const std::string& scriptPath);
    bool isAbsolutePath(const std::string& path) const;
    bool isPathSafe(const std::string& path) const;
    nlohmann::json loadScriptFromFile(const std::string& fullPath);
    void extractMetadataFromScript(const nlohmann::json& script, ScriptMetadata& metadata);
    
    // Validation helpers
    bool validateScriptStructure(const nlohmann::json& script);
    bool validateScriptCommands(const nlohmann::json& script);
    bool validateScriptVariables(const nlohmann::json& script);
    bool checkCircularDependencies(const std::string& scriptPath, std::vector<std::string>& visitedScripts);
    
    // Error handling
    std::string getScriptErrorMessage(const std::string& scriptPath, const std::string& error);
    void logScriptError(const std::string& scriptPath, const std::string& error);
};

} // namespace burwell

#endif // BURWELL_SCRIPT_MANAGER_H