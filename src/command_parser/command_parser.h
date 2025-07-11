#ifndef BURWELL_COMMAND_PARSER_H
#define BURWELL_COMMAND_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <regex>
#include <nlohmann/json.hpp>

namespace burwell {

// Forward declarations
class TaskEngine;
class LLMConnector;

// Intent classification results
enum class IntentType {
    AUTOMATION,     // User wants to automate a task
    QUERY,         // User is asking a question
    SYSTEM,        // System control commands
    HELP,          // User needs help
    TASK_MANAGEMENT, // Managing existing tasks
    UNKNOWN        // Could not classify
};

// Confidence levels for parsing results
enum class ConfidenceLevel {
    HIGH = 3,      // Very confident in the parsing
    MEDIUM = 2,    // Moderately confident
    LOW = 1,       // Low confidence
    NONE = 0       // No confidence
};

// Structure to represent a parsed command with context
struct ParsedCommand {
    std::string action;              // The command to execute
    nlohmann::json parameters;       // Command parameters
    std::string description;         // Human-readable description
    int priority;                    // Execution priority (1-10)
    bool isOptional;                // Can this command fail without stopping execution?
    int delayAfterMs;               // Delay after execution
    
    ParsedCommand() : priority(5), isOptional(false), delayAfterMs(0) {}
};

// Structure for intent analysis results
struct IntentAnalysis {
    IntentType type;
    ConfidenceLevel confidence;
    std::string description;
    std::map<std::string, std::string> entities;  // Extracted entities (file names, app names, etc.)
    std::vector<std::string> keywords;           // Key terms identified
    
    IntentAnalysis() : type(IntentType::UNKNOWN), confidence(ConfidenceLevel::NONE) {}
};

// Structure for command parsing results
struct CommandParseResult {
    bool success;
    std::string errorMessage;
    IntentAnalysis intent;
    std::vector<ParsedCommand> commands;
    std::string suggestedTaskName;              // Suggested name if this should be saved as a task
    std::map<std::string, std::string> context; // Additional context information
    
    CommandParseResult() : success(false) {}
};

class CommandParser {
public:
    CommandParser();
    ~CommandParser();
    
    // Main parsing interface
    CommandParseResult parseUserInput(const std::string& userInput);
    CommandParseResult parseLLMPlan(const nlohmann::json& llmPlan);
    
    // Intent analysis
    IntentAnalysis analyzeIntent(const std::string& userInput);
    IntentType classifyIntent(const std::string& input);
    ConfidenceLevel calculateConfidence(const std::string& input, IntentType intent);
    
    // Entity extraction
    std::map<std::string, std::string> extractEntities(const std::string& input);
    std::vector<std::string> extractApplicationNames(const std::string& input);
    std::vector<std::string> extractFileNames(const std::string& input);
    std::vector<std::string> extractActionVerbs(const std::string& input);
    
    // Command validation and processing
    bool validateCommand(const ParsedCommand& command);
    std::vector<std::string> getValidationErrors(const ParsedCommand& command);
    ParsedCommand normalizeCommand(const ParsedCommand& command);
    
    // Pattern matching for common automation tasks
    std::vector<ParsedCommand> matchAutomationPatterns(const std::string& input);
    std::vector<ParsedCommand> parseFileOperationCommand(const std::string& input);
    std::vector<ParsedCommand> parseApplicationCommand(const std::string& input);
    std::vector<ParsedCommand> parseSystemCommand(const std::string& input);
    std::vector<ParsedCommand> parseUIInteractionCommand(const std::string& input);
    
    // Task integration
    void setTaskEngine(std::shared_ptr<TaskEngine> taskEngine);
    void setLLMConnector(std::shared_ptr<LLMConnector> llmConnector);
    std::vector<std::string> suggestExistingTasks(const std::string& input);
    bool shouldCreateNewTask(const CommandParseResult& result);
    
    // Configuration and learning
    void addCustomPattern(const std::string& pattern, const std::string& commandTemplate);
    void setStrictMode(bool enabled);  // Strict validation vs permissive
    void setConfidenceThreshold(ConfidenceLevel threshold);
    void enableLearning(bool enabled); // Learn from user feedback
    
    // Feedback and improvement
    void provideFeedback(const std::string& originalInput, const CommandParseResult& result, bool wasCorrect);
    void saveUserCorrection(const std::string& input, const std::vector<ParsedCommand>& correctedCommands);
    
    // Statistics and analytics
    struct ParsingStatistics {
        int totalParses;
        int successfulParses;
        int failedParses;
        std::map<IntentType, int> intentCounts;
        std::map<std::string, int> commandCounts;
        double averageConfidence;
    };
    
    ParsingStatistics getStatistics() const;
    void resetStatistics();

private:
    struct CommandParserImpl;
    std::unique_ptr<CommandParserImpl> m_impl;
    
    // Core parsing components
    std::shared_ptr<TaskEngine> m_taskEngine;
    std::shared_ptr<LLMConnector> m_llmConnector;
    
    // Configuration
    bool m_strictMode;
    ConfidenceLevel m_confidenceThreshold;
    bool m_learningEnabled;
    
    // Pattern matching
    std::vector<std::regex> m_automationPatterns;
    std::vector<std::regex> m_queryPatterns;
    std::vector<std::regex> m_systemPatterns;
    std::map<std::string, std::string> m_customPatterns;
    
    // Entity extraction patterns
    std::regex m_fileNamePattern;
    std::regex m_applicationNamePattern;
    std::regex m_actionVerbPattern;
    std::regex m_coordinatePattern;
    std::regex m_timePattern;
    
    // Statistics
    mutable ParsingStatistics m_statistics;
    
    // Internal methods
    void initializePatterns();
    void loadCustomPatterns();
    void saveCustomPatterns();
    
    // Intent classification helpers
    bool containsAutomationKeywords(const std::string& input);
    bool containsQueryKeywords(const std::string& input);
    bool containsSystemKeywords(const std::string& input);
    bool containsHelpKeywords(const std::string& input);
    
    // Pattern matching helpers
    std::vector<ParsedCommand> applyPatternMatching(const std::string& input, IntentType intent);
    ParsedCommand createCommandFromPattern(const std::string& pattern, const std::map<std::string, std::string>& matches);
    
    // Validation helpers
    bool isValidAction(const std::string& action);
    bool hasRequiredParameters(const std::string& action, const nlohmann::json& params);
    std::vector<std::string> getRequiredParameters(const std::string& action);
    
    // LLM integration helpers
    std::string buildNLPPrompt(const std::string& userInput);
    CommandParseResult parseLLMResponse(const nlohmann::json& response);
    bool shouldUseLLMFallback(const std::string& input, const CommandParseResult& initialResult);
    
    // Learning and feedback
    void updatePatternsFromFeedback(const std::string& input, const std::vector<ParsedCommand>& commands);
    void adjustConfidenceWeights(bool wasCorrect);
    
    // Utility methods
    std::string normalizeInput(const std::string& input);
    std::vector<std::string> tokenizeInput(const std::string& input);
    std::string extractQuotedStrings(const std::string& input);
    std::map<std::string, std::string> parseKeyValuePairs(const std::string& input);
    std::string generateTaskName(const std::string& input);
    
    // Command creation helpers
    ParsedCommand createMouseClickCommand(int x, int y, const std::string& button = "left");
    ParsedCommand createKeyboardCommand(const std::string& text);
    ParsedCommand createApplicationCommand(const std::string& appName, const std::string& action);
    ParsedCommand createFileCommand(const std::string& fileName, const std::string& action);
    ParsedCommand createDelayCommand(int milliseconds);
    
    // Error handling
    void updateStatistics(const CommandParseResult& result);
    void logParsingAttempt(const std::string& input, const CommandParseResult& result);
};

} // namespace burwell

#endif // BURWELL_COMMAND_PARSER_H


