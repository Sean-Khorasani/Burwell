#include "script_manager.h"
#include "execution_engine.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include "../common/file_utils.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace burwell {

ScriptManager::ScriptManager()
    : m_maxNestingLevel(3)
    , m_cachingEnabled(true)
    , m_scriptDirectory("test_scripts") {
    SLOG_DEBUG().message("ScriptManager initialized");
}

ScriptManager::~ScriptManager() {
    SLOG_DEBUG().message("ScriptManager destroyed");
}

void ScriptManager::setExecutionEngine(std::shared_ptr<ExecutionEngine> engine) {
    m_executionEngine = engine;
}

void ScriptManager::setMaxNestingLevel(int maxLevel) {
    m_maxNestingLevel = maxLevel;
}

void ScriptManager::setScriptCachingEnabled(bool enabled) {
    m_cachingEnabled = enabled;
    if (!enabled) {
        clearScriptCache();
    }
}

void ScriptManager::setScriptDirectory(const std::string& directory) {
    m_scriptDirectory = directory;
}

TaskExecutionResult ScriptManager::executeScriptFile(const std::string& scriptPath, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    // Check nesting level
    if (!canExecuteNestedScript(context)) {
        result.errorMessage = "Maximum script nesting level exceeded";
        SLOG_ERROR().message("Maximum script nesting level exceeded");
        return result;
    }
    
    // Resolve script path
    std::string fullPath = resolveScriptPath(scriptPath);
    
    // Check for circular dependencies
    if (isScriptInStack(context, fullPath)) {
        result.errorMessage = "Circular script dependency detected: " + fullPath;
        SLOG_ERROR().message("Circular script dependency detected").context("script_path", fullPath);
        return result;
    }
    
    // Load the script
    nlohmann::json script = loadScript(fullPath);
    if (script.is_null()) {
        result.errorMessage = "Failed to load script: " + fullPath;
        return result;
    }
    
    // Validate the script
    if (!validateScript(script)) {
        result.errorMessage = "Invalid script format: " + fullPath;
        return result;
    }
    
    // Push script to stack
    pushScriptToStack(context, fullPath);
    
    // Load script variables
    loadScriptVariables(script, context);
    
    // Execute the script - check for both 'commands' and 'sequence' arrays
    nlohmann::json commandArray;
    if (script.contains("commands") && script["commands"].is_array()) {
        commandArray = script["commands"];
    } else if (script.contains("sequence") && script["sequence"].is_array()) {
        commandArray = script["sequence"];
    }
    
    if (m_executionEngine && !commandArray.is_null()) {
        result = m_executionEngine->executeCommandSequence(commandArray, context);
        
        // Store script result if requested
        if (script.contains("result_variable") && script["result_variable"].is_string()) {
            std::string resultVar = script["result_variable"];
            context.subScriptResults[resultVar] = result.success ? result.output : result.errorMessage;
        }
    } else {
        result.errorMessage = "Script must contain 'commands' or 'sequence' array";
    }
    
    // Pop script from stack
    popScriptFromStack(context);
    
    return result;
}

TaskExecutionResult ScriptManager::executeScriptCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    if (!command.contains("parameters") || !command["parameters"].contains("script_path")) {
        result.errorMessage = "Script command missing script_path parameter";
        return result;
    }
    
    std::string scriptPath = command["parameters"]["script_path"];
    
    // Check for result variable
    if (command["parameters"].contains("result_variable")) {
        std::string resultVar = command["parameters"]["result_variable"];
        auto scriptResult = executeScriptFile(scriptPath, context);
        
        // Store the result in parent context
        if (scriptResult.success) {
            context.variables[resultVar] = scriptResult.output;
        } else {
            context.variables[resultVar] = nlohmann::json::object({
                {"success", false},
                {"error", scriptResult.errorMessage}
            });
        }
        
        return scriptResult;
    }
    
    // Check for continue_on_failure
    bool continueOnFailure = false;
    if (command["parameters"].contains("continue_on_failure")) {
        continueOnFailure = command["parameters"]["continue_on_failure"];
    }
    
    auto scriptResult = executeScriptFile(scriptPath, context);
    
    // Handle continue_on_failure
    if (!scriptResult.success && continueOnFailure) {
        SLOG_WARNING().message("Script failed but continuing").context("error", scriptResult.errorMessage);
        scriptResult.success = true;  // Override to allow parent to continue
    }
    
    return scriptResult;
}

nlohmann::json ScriptManager::loadScript(const std::string& scriptPath) {
    // Check cache first
    if (m_cachingEnabled) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_scriptCache.find(scriptPath);
        if (it != m_scriptCache.end()) {
            SLOG_DEBUG().message("Loading script from cache").context("script_path", scriptPath);
            return it->second;
        }
    }
    
    // Validate path
    if (!validateScriptPath(scriptPath)) {
        SLOG_ERROR().message("Invalid script path").context("script_path", scriptPath);
        return nlohmann::json();
    }
    
    // Load from file
    nlohmann::json script = loadScriptFromFile(scriptPath);
    
    // Cache if enabled
    if (m_cachingEnabled && !script.is_null()) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_scriptCache[scriptPath] = script;
    }
    
    return script;
}

bool ScriptManager::validateScript(const nlohmann::json& script) {
    if (!script.is_object()) {
        return false;
    }
    
    // Validate structure
    if (!validateScriptStructure(script)) {
        return false;
    }
    
    // Validate commands
    if (!validateScriptCommands(script)) {
        return false;
    }
    
    // Validate variables
    if (!validateScriptVariables(script)) {
        return false;
    }
    
    return true;
}

bool ScriptManager::validateScriptPath(const std::string& scriptPath) {
    // Use input validator
    auto validation = InputValidator::validateFilePath(scriptPath);
    if (!validation.isValid) {
        SLOG_ERROR().message("Script path validation failed").context("error", validation.errorMessage);
        return false;
    }
    
    // Check if path is safe (no directory traversal)
    if (!isPathSafe(scriptPath)) {
        return false;
    }
    
    return true;
}

void ScriptManager::loadScriptVariables(const nlohmann::json& script, ExecutionContext& context) {
    if (script.contains("variables") && script["variables"].is_object()) {
        for (auto& [name, value] : script["variables"].items()) {
            // Don't override existing variables unless explicitly specified
            if (context.variables.find(name) == context.variables.end()) {
                context.variables[name] = value;
                SLOG_DEBUG().message("Loaded script variable").context("name", name);
            }
        }
    }
}

void ScriptManager::inheritParentVariables(const ExecutionContext& parentContext, ExecutionContext& childContext) {
    // Copy all parent variables to child
    for (const auto& [name, value] : parentContext.variables) {
        childContext.variables[name] = value;
    }
    
    // Also copy sub-script results
    for (const auto& [name, value] : parentContext.subScriptResults) {
        childContext.subScriptResults[name] = value;
    }
}

bool ScriptManager::canExecuteNestedScript(const ExecutionContext& context) const {
    return context.nestingLevel < context.maxNestingLevel;
}

void ScriptManager::pushScriptToStack(ExecutionContext& context, const std::string& scriptPath) {
    context.scriptStack.push_back(scriptPath);
    context.nestingLevel++;
    SLOG_DEBUG().message("Script pushed to stack").context("script_path", scriptPath).context("level", context.nestingLevel);
}

void ScriptManager::popScriptFromStack(ExecutionContext& context) {
    if (!context.scriptStack.empty()) {
        std::string scriptPath = context.scriptStack.back();
        context.scriptStack.pop_back();
        context.nestingLevel--;
        SLOG_DEBUG().message("Script popped from stack").context("script_path", scriptPath).context("level", context.nestingLevel);
    }
}

bool ScriptManager::isScriptInStack(const ExecutionContext& context, const std::string& scriptPath) const {
    return std::find(context.scriptStack.begin(), context.scriptStack.end(), scriptPath) != context.scriptStack.end();
}

std::vector<std::string> ScriptManager::listAvailableScripts() const {
    std::vector<std::string> scripts;
    
    try {
        std::filesystem::path scriptDir(m_scriptDirectory);
        if (std::filesystem::exists(scriptDir) && std::filesystem::is_directory(scriptDir)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    scripts.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Error listing scripts").context("error", e.what());
    }
    
    return scripts;
}

std::vector<std::string> ScriptManager::findScriptsByPattern(const std::string& pattern) const {
    std::vector<std::string> matchingScripts;
    auto allScripts = listAvailableScripts();
    
    for (const auto& script : allScripts) {
        if (script.find(pattern) != std::string::npos) {
            matchingScripts.push_back(script);
        }
    }
    
    return matchingScripts;
}

bool ScriptManager::scriptExists(const std::string& scriptName) const {
    // Inline path resolution for const method
    std::filesystem::path fullPath;
    if (std::filesystem::path(scriptName).is_absolute()) {
        fullPath = scriptName;
    } else {
        fullPath = std::filesystem::path(m_scriptDirectory) / scriptName;
        if (fullPath.extension().empty()) {
            fullPath += ".json";
        }
    }
    return std::filesystem::exists(fullPath);
}

ScriptManager::ScriptMetadata ScriptManager::getScriptMetadata(const std::string& scriptPath) {
    ScriptMetadata metadata;
    
    nlohmann::json script = loadScript(scriptPath);
    if (!script.is_null()) {
        extractMetadataFromScript(script, metadata);
    }
    
    // Add file info
    try {
        std::filesystem::path path(scriptPath);
        metadata.name = path.filename().string();
        
        auto ftime = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time_t = std::chrono::system_clock::to_time_t(sctp);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        metadata.lastModified = ss.str();
    } catch (...) {
        // Ignore errors
    }
    
    return metadata;
}

void ScriptManager::clearScriptCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_scriptCache.clear();
    SLOG_DEBUG().message("Script cache cleared");
}

size_t ScriptManager::getCacheSize() const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_scriptCache.size();
}

void ScriptManager::preloadScript(const std::string& scriptPath) {
    loadScript(scriptPath);  // This will cache it if caching is enabled
}

// Private methods

std::string ScriptManager::resolveScriptPath(const std::string& scriptPath) {
    if (isAbsolutePath(scriptPath)) {
        return scriptPath;
    }
    
    // Check if it's relative to script directory
    std::filesystem::path fullPath = std::filesystem::path(m_scriptDirectory) / scriptPath;
    
    // Add .json extension if not present
    if (fullPath.extension().empty()) {
        fullPath += ".json";
    }
    
    return fullPath.string();
}

bool ScriptManager::isAbsolutePath(const std::string& path) const {
    return std::filesystem::path(path).is_absolute();
}

bool ScriptManager::isPathSafe(const std::string& path) const {
    // Check for directory traversal
    if (path.find("..") != std::string::npos) {
        return false;
    }
    
    // Additional safety checks
    return InputValidator::isPathTraversalSafe(path);
}

nlohmann::json ScriptManager::loadScriptFromFile(const std::string& fullPath) {
    std::string content;
    if (!utils::FileUtils::readFileToString(fullPath, content)) {
        SLOG_ERROR().message("Failed to read script file").context("file_path", fullPath);
        return nlohmann::json();
    }
    
    try {
        return nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        SLOG_ERROR().message("Failed to parse script JSON").context("error", e.what());
        return nlohmann::json();
    }
}

void ScriptManager::extractMetadataFromScript(const nlohmann::json& script, ScriptMetadata& metadata) {
    if (script.contains("metadata") && script["metadata"].is_object()) {
        auto& meta = script["metadata"];
        
        metadata.description = meta.value("description", "");
        metadata.version = meta.value("version", "");
        metadata.author = meta.value("author", "");
        
        if (meta.contains("requiredParameters") && meta["requiredParameters"].is_array()) {
            for (const auto& param : meta["requiredParameters"]) {
                if (param.is_string()) {
                    metadata.requiredParameters.push_back(param);
                }
            }
        }
        
        if (meta.contains("optionalParameters") && meta["optionalParameters"].is_array()) {
            for (const auto& param : meta["optionalParameters"]) {
                if (param.is_string()) {
                    metadata.optionalParameters.push_back(param);
                }
            }
        }
        
        if (meta.contains("dependencies") && meta["dependencies"].is_array()) {
            for (const auto& dep : meta["dependencies"]) {
                if (dep.is_string()) {
                    metadata.dependencies.push_back(dep);
                }
            }
        }
    }
}

bool ScriptManager::validateScriptStructure(const nlohmann::json& script) {
    // Must have commands or sequence array
    bool hasCommands = script.contains("commands") && script["commands"].is_array();
    bool hasSequence = script.contains("sequence") && script["sequence"].is_array();
    
    if (!hasCommands && !hasSequence) {
        SLOG_ERROR().message("Script missing commands or sequence array");
        return false;
    }
    
    // Commands/sequence array must not be empty
    if ((hasCommands && script["commands"].empty()) || (hasSequence && script["sequence"].empty())) {
        SLOG_ERROR().message("Script has empty commands or sequence array");
        return false;
    }
    
    return true;
}

bool ScriptManager::validateScriptCommands(const nlohmann::json& script) {
    // Check for both 'commands' and 'sequence' arrays
    nlohmann::json commandArray;
    if (script.contains("commands") && script["commands"].is_array()) {
        commandArray = script["commands"];
    } else if (script.contains("sequence") && script["sequence"].is_array()) {
        commandArray = script["sequence"];
    } else {
        // This shouldn't happen as validateScriptStructure already checked this
        return false;
    }
    
    for (size_t i = 0; i < commandArray.size(); ++i) {
        const auto& cmd = commandArray[i];
        
        if (!cmd.is_object()) {
            SLOG_ERROR().message("Command is not an object").context("command_index", i);
            return false;
        }
        
        if (!cmd.contains("command") || !cmd["command"].is_string()) {
            SLOG_ERROR().message("Command missing 'command' field").context("command_index", i);
            return false;
        }
    }
    
    return true;
}

bool ScriptManager::validateScriptVariables(const nlohmann::json& script) {
    if (script.contains("variables")) {
        if (!script["variables"].is_object()) {
            SLOG_ERROR().message("Script 'variables' must be an object");
            return false;
        }
    }
    
    return true;
}

bool ScriptManager::checkCircularDependencies(const std::string& scriptPath, std::vector<std::string>& visitedScripts) {
    if (std::find(visitedScripts.begin(), visitedScripts.end(), scriptPath) != visitedScripts.end()) {
        return true;  // Circular dependency found
    }
    
    visitedScripts.push_back(scriptPath);
    
    // Load script and check for EXECUTE_SCRIPT commands
    nlohmann::json script = loadScript(scriptPath);
    if (!script.is_null()) {
        // Check both 'commands' and 'sequence' arrays
        nlohmann::json commandArray;
        if (script.contains("commands") && script["commands"].is_array()) {
            commandArray = script["commands"];
        } else if (script.contains("sequence") && script["sequence"].is_array()) {
            commandArray = script["sequence"];
        }
        
        if (!commandArray.is_null()) {
            for (const auto& cmd : commandArray) {
                if (cmd.contains("command") && cmd["command"] == "EXECUTE_SCRIPT") {
                    if (cmd.contains("parameters") && cmd["parameters"].contains("script_path")) {
                        std::string childPath = resolveScriptPath(cmd["parameters"]["script_path"]);
                        if (checkCircularDependencies(childPath, visitedScripts)) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    
    visitedScripts.pop_back();
    return false;
}

std::string ScriptManager::getScriptErrorMessage(const std::string& scriptPath, const std::string& error) {
    return "Script error in '" + scriptPath + "': " + error;
}

void ScriptManager::logScriptError(const std::string& scriptPath, const std::string& error) {
    SLOG_ERROR().message("Script error").context("script_path", scriptPath).context("error", error);
}

} // namespace burwell