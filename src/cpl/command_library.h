#ifndef BURWELL_COMMAND_LIBRARY_H
#define BURWELL_COMMAND_LIBRARY_H

#include "cpl_parser.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

namespace burwell {
namespace cpl {

struct CommandSequence {
    std::string name;
    std::string description;
    std::vector<CPLCommand> commands;
    std::map<std::string, std::string> metadata;
    
    // Learning data
    int totalExecutions;
    int successfulExecutions;
    double successRate;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point lastUsed;
    std::vector<std::string> tags;
    
    // Optimization data
    std::vector<std::string> knownIssues;
    std::vector<std::string> improvements;
    std::map<std::string, double> parameterOptimizations;
};

struct ExecutionMetrics {
    std::string commandType;
    int totalExecutions;
    int successfulExecutions;
    double averageExecutionTimeMs;
    double successRate;
    std::map<std::string, int> failureReasons;
    std::vector<std::string> optimizationSuggestions;
};

struct LearningData {
    std::map<std::string, ExecutionMetrics> commandMetrics;
    std::vector<std::string> frequentPatterns;
    std::map<std::string, std::string> failurePatterns;
    std::vector<std::string> suggestedImprovements;
    std::chrono::system_clock::time_point lastUpdated;
};

class CommandLibraryManager {
public:
    CommandLibraryManager();
    ~CommandLibraryManager() = default;
    
    // Library management
    bool initialize(const std::string& libraryPath);
    bool save();
    bool load();
    void setLibraryPath(const std::string& path);
    
    // Sequence management
    bool saveSequence(const std::string& name, const std::vector<CPLCommand>& commands,
                     const std::string& description = "");
    bool loadSequence(const std::string& name, CommandSequence& sequence);
    bool deleteSequence(const std::string& name);
    bool sequenceExists(const std::string& name);
    
    std::vector<std::string> listSequences();
    std::vector<CommandSequence> searchSequences(const std::string& query);
    std::vector<std::string> getSequencesByTag(const std::string& tag);
    
    // Sequence editing
    bool editSequence(const std::string& name, const std::vector<CPLCommand>& newCommands);
    bool addCommandToSequence(const std::string& sequenceName, const CPLCommand& command, int position = -1);
    bool removeCommandFromSequence(const std::string& sequenceName, int commandIndex);
    bool replaceCommandInSequence(const std::string& sequenceName, int commandIndex, const CPLCommand& newCommand);
    
    // Learning and analytics
    void recordExecution(const std::string& sequenceName, bool success, 
                        double executionTimeMs, const std::string& errorMessage = "");
    void recordCommandExecution(const std::string& commandType, bool success,
                               double executionTimeMs, const std::string& errorMessage = "");
    
    ExecutionMetrics getCommandMetrics(const std::string& commandType);
    LearningData getLearningData();
    std::vector<std::string> getSuggestedOptimizations(const std::string& sequenceName);
    
    // Pattern recognition and suggestions
    std::vector<std::string> suggestSimilarSequences(const std::string& userIntent);
    std::vector<CommandSequence> findSequencesByPattern(const std::vector<std::string>& commandTypes);
    std::string suggestSequenceName(const std::vector<CPLCommand>& commands);
    
    // Template and example management
    bool saveAsTemplate(const std::string& templateName, const CommandSequence& sequence);
    std::vector<std::string> getAvailableTemplates();
    bool createSequenceFromTemplate(const std::string& templateName, const std::string& newSequenceName,
                                   const std::map<std::string, std::string>& parameters);
    
    // Export/Import functionality
    bool exportSequence(const std::string& sequenceName, const std::string& filePath);
    bool importSequence(const std::string& filePath, const std::string& newSequenceName = "");
    bool exportLibrary(const std::string& filePath);
    bool importLibrary(const std::string& filePath, bool merge = true);
    
    // Statistics and reporting
    nlohmann::json getLibraryStatistics();
    std::vector<CommandSequence> getMostUsedSequences(int limit = 10);
    std::vector<CommandSequence> getRecentlyUsedSequences(int limit = 10);
    std::map<std::string, double> getCommandSuccessRates();
    
    // Maintenance and optimization
    void cleanupOldData(int maxAgeInDays = 90);
    void optimizeLibrary();
    bool validateLibraryIntegrity();
    std::vector<std::string> getLibraryIssues();

private:
    // Internal data management
    bool loadSequencesFromFile();
    bool saveSequencesToFile();
    bool loadLearningDataFromFile();
    bool saveLearningDataToFile();
    
    // Analysis helpers
    double calculateSimilarity(const std::vector<CPLCommand>& seq1, const std::vector<CPLCommand>& seq2);
    std::vector<std::string> extractKeywords(const std::string& text);
    std::string generateSequenceHash(const std::vector<CPLCommand>& commands);
    
    // Learning helpers
    void updateCommandStatistics(const std::string& commandType, bool success, double timeMs);
    void analyzeFailurePatterns();
    void generateOptimizationSuggestions();
    
    // File I/O helpers
    std::string getSequencesFilePath();
    std::string getLearningDataFilePath();
    std::string getTemplatesFilePath();
    bool ensureDirectoryExists(const std::string& path);
    
    // Member variables
    std::string m_libraryPath;
    std::map<std::string, CommandSequence> m_sequences;
    std::map<std::string, CommandSequence> m_templates;
    LearningData m_learningData;
    
    // Configuration
    int m_maxSequences;
    int m_maxExecutionHistory;
    bool m_autoOptimize;
    bool m_collectAnalytics;
    
    // Runtime state
    bool m_isInitialized;
    bool m_hasUnsavedChanges;
    std::chrono::system_clock::time_point m_lastSaved;
};

// Utility functions for command sequence manipulation
std::vector<CPLCommand> mergeSequences(const std::vector<CPLCommand>& seq1, 
                                      const std::vector<CPLCommand>& seq2);
std::vector<CPLCommand> optimizeSequence(const std::vector<CPLCommand>& commands);
bool areSequencesSimilar(const std::vector<CPLCommand>& seq1, 
                        const std::vector<CPLCommand>& seq2, 
                        double threshold = 0.8);

} // namespace cpl
} // namespace burwell

#endif // BURWELL_COMMAND_LIBRARY_H