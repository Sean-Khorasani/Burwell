#ifndef BURWELL_LLM_ADAPTER_H
#define BURWELL_LLM_ADAPTER_H

#include "cpl_parser.h"
#include "command_library.h"
#include "../llm_connector/llm_connector.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

namespace burwell {
namespace cpl {

struct LLMPromptTemplate {
    std::string templateName;
    std::string systemPrompt;
    std::string userPromptTemplate;
    std::vector<std::string> exampleCommands;
    std::map<std::string, std::string> variables;
    bool includeCommandReference;
    bool includeSuccessExamples;
    bool includeErrorExamples;
};

struct LLMResponse {
    std::string rawResponse;
    std::vector<CPLCommand> extractedCommands;
    bool parseSuccess;
    std::string parseError;
    double confidenceScore;
    std::string reasoning;
    std::map<std::string, std::string> metadata;
};

struct LLMFeedback {
    std::string originalRequest;
    std::vector<CPLCommand> generatedCommands;
    std::vector<ExecutionResult> executionResults;
    bool overallSuccess;
    std::string userFeedback;
    double satisfactionScore;
    std::chrono::system_clock::time_point timestamp;
};

class LLMToCPLAdapter {
public:
    LLMToCPLAdapter();
    ~LLMToCPLAdapter() = default;
    
    // Initialization
    bool initialize(std::shared_ptr<LLMConnector> llmConnector, 
                   CommandLibraryManager* library,
                   CPLParser* parser);
    
    // Main conversion interface
    LLMResponse convertUserRequestToCPL(const std::string& userRequest);
    LLMResponse improveCommandsWithFeedback(const std::vector<CPLCommand>& commands,
                                          const std::vector<ExecutionResult>& results);
    
    // Prompt management
    void loadPromptTemplate(const LLMPromptTemplate& template_);
    std::string generatePrompt(const std::string& userRequest);
    std::string generateFeedbackPrompt(const std::vector<CPLCommand>& commands,
                                     const std::vector<ExecutionResult>& results);
    
    // Learning and adaptation
    void recordSuccessfulConversion(const std::string& userRequest, 
                                   const std::vector<CPLCommand>& commands);
    void recordFailedConversion(const std::string& userRequest,
                               const std::string& errorMessage);
    void addUserFeedback(const LLMFeedback& feedback);
    
    // Command suggestion and completion
    std::vector<CPLCommand> suggestCommandsForIntent(const std::string& intent);
    std::vector<std::string> suggestParameterValues(const std::string& commandType,
                                                   const std::string& parameterName);
    CPLCommand completePartialCommand(const std::string& partialCommand);
    
    // Template and example management
    void addSuccessfulExample(const std::string& userRequest, 
                             const std::vector<CPLCommand>& commands);
    void updatePromptWithNewExamples();
    std::vector<std::string> getRelevantExamples(const std::string& userRequest);
    
    // LLM provider abstraction
    void setLLMProvider(const std::string& providerName);
    std::string getCurrentLLMProvider() const;
    bool testLLMConnection();
    
    // Quality and optimization
    double evaluateResponseQuality(const LLMResponse& response);
    void optimizePromptForProvider();
    std::vector<std::string> getQualityIssues(const LLMResponse& response);
    
    // Analytics and reporting
    nlohmann::json getConversionStatistics();
    std::vector<LLMFeedback> getRecentFeedback(int limit = 50);
    std::map<std::string, double> getCommandAccuracyRates();
    
    // Configuration
    void setMaxResponseLength(int maxLength);
    void setConfidenceThreshold(double threshold);
    void setRetryOnLowConfidence(bool retry);
    void setIncludeFeedbackInPrompt(bool include);

private:
    // Core conversion methods
    std::vector<CPLCommand> parseCommands(const std::string& llmResponse);
    double calculateConfidenceScore(const std::vector<CPLCommand>& commands);
    std::string extractReasoningFromResponse(const std::string& response);
    
    // Prompt building helpers
    std::string buildSystemPrompt();
    std::string buildCommandReference();
    std::string buildExampleSection();
    std::string buildFeedbackSection();
    
    // Learning helpers
    void updateSuccessPatterns(const std::string& userRequest,
                              const std::vector<CPLCommand>& commands);
    void analyzeFailurePatterns();
    void generateImprovedExamples();
    
    // Response processing
    bool isValidCPLResponse(const std::string& response);
    std::string cleanResponseText(const std::string& response);
    std::vector<std::string> extractCPLLines(const std::string& response);
    
    // Quality assessment
    bool hasValidCommandStructure(const std::vector<CPLCommand>& commands);
    bool hasReasonableParameterValues(const std::vector<CPLCommand>& commands);
    bool matchesUserIntent(const std::string& userRequest,
                          const std::vector<CPLCommand>& commands);
    
    // Provider-specific adaptations
    void adaptPromptForOpenAI();
    void adaptPromptForClaude();
    void adaptPromptForTogetherAI();
    void adaptPromptForLocal();
    
    // Member variables
    std::shared_ptr<LLMConnector> m_llmConnector;
    CommandLibraryManager* m_library;
    CPLParser* m_parser;
    
    // Prompt templates and examples
    LLMPromptTemplate m_currentTemplate;
    std::vector<std::pair<std::string, std::vector<CPLCommand>>> m_successfulExamples;
    std::vector<std::pair<std::string, std::string>> m_failureExamples;
    
    // Learning data
    std::vector<LLMFeedback> m_feedbackHistory;
    std::map<std::string, std::vector<std::vector<CPLCommand>>> m_successPatterns;
    std::map<std::string, std::vector<std::string>> m_failurePatterns;
    
    // Configuration
    std::string m_currentProvider;
    int m_maxResponseLength;
    double m_confidenceThreshold;
    bool m_retryOnLowConfidence;
    bool m_includeFeedbackInPrompt;
    int m_maxExamplesInPrompt;
    
    // Statistics
    int m_totalConversions;
    int m_successfulConversions;
    int m_failedConversions;
    std::map<std::string, int> m_commandTypeCount;
    std::map<std::string, double> m_commandAccuracy;
    
    // Cache and optimization
    std::map<std::string, LLMResponse> m_responseCache;
    std::chrono::system_clock::time_point m_lastOptimization;
    bool m_cacheEnabled;
    int m_maxCacheSize;
};

// Utility functions for LLM interaction
std::string createCommandReferenceText(const std::vector<std::string>& commandTypes);
std::string formatExampleForPrompt(const std::string& userRequest,
                                  const std::vector<CPLCommand>& commands);
double calculateStringSimilarity(const std::string& str1, const std::string& str2);
std::vector<std::string> extractIntentKeywords(const std::string& userRequest);

} // namespace cpl
} // namespace burwell

#endif // BURWELL_LLM_ADAPTER_H