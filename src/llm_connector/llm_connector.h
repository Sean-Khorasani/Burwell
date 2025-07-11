#ifndef BURWELL_LLM_CONNECTOR_H
#define BURWELL_LLM_CONNECTOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include "http_client.h"

namespace burwell {

struct LLMMessage {
    std::string role;  // "system", "user", "assistant"
    std::string content;
    std::vector<uint8_t> imageData;  // For vision-capable LLMs
    std::string imageFormat;         // "png", "jpeg", "base64"
    
    LLMMessage(const std::string& r, const std::string& c) : role(r), content(c), imageFormat("") {}
    LLMMessage(const std::string& r, const std::string& c, const std::vector<uint8_t>& img, const std::string& fmt) 
        : role(r), content(c), imageData(img), imageFormat(fmt) {}
    
    bool hasImage() const { return !imageData.empty(); }
};

struct ExecutionPlan {
    std::vector<nlohmann::json> commands;
    std::string reasoning;
    std::string summary;
    bool isValid;
    
    ExecutionPlan() : isValid(false) {}
};

struct LLMContext {
    std::string activeWindow;
    std::string currentDirectory;
    std::vector<std::string> openWindows;
    std::map<std::string, std::string> environmentInfo;
    std::vector<std::string> recentActions;
    
    // Vision-related data
    std::vector<uint8_t> screenshotData;
    std::string screenshotFormat;
    std::string textDescription;        // Comprehensive text description for text-only LLMs
    nlohmann::json structuredData;      // Structured environmental data
    
    bool hasScreenshot() const { return !screenshotData.empty(); }
    
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& json);
};

class LLMConnector {
public:
    LLMConnector();
    ~LLMConnector();
    
    // Core LLM communication
    ExecutionPlan generatePlan(const std::string& userRequest, const LLMContext& context);
    nlohmann::json sendMessage(const std::vector<LLMMessage>& messages);
    nlohmann::json sendPrompt(const std::string& prompt);
    
    // Context management
    void updateContext(const LLMContext& context);
    void addToHistory(const LLMMessage& message);
    void clearHistory();
    std::vector<LLMMessage> getHistory() const;
    
    // Configuration
    void setApiKey(const std::string& apiKey);
    void setBaseUrl(const std::string& baseUrl);
    void setModelName(const std::string& modelName);
    void setTimeout(int timeoutMs);
    void setMaxRetries(int maxRetries);
    void setTemperature(double temperature);
    void setMaxTokens(int maxTokens);
    
    // Provider support
    enum class Provider {
        OPENAI,
        ANTHROPIC,
        AZURE_OPENAI,
        CUSTOM
    };
    
    void setProvider(Provider provider);
    void setCustomHeaders(const std::map<std::string, std::string>& headers);
    
    // Vision capability support
    struct VisionCapabilities {
        bool supportsVision;
        std::vector<std::string> supportedImageFormats;  // "png", "jpeg", "webp"
        int maxImageSize;                                // Maximum image size in bytes
        int maxContextLength;                            // Maximum context tokens
        std::string preferredInputMode;                  // "vision", "text", "hybrid"
        
        VisionCapabilities() : supportsVision(false), maxImageSize(4194304), maxContextLength(4096), preferredInputMode("text") {}
    };
    
    void setVisionCapabilities(const VisionCapabilities& capabilities);
    VisionCapabilities getVisionCapabilities() const;
    bool supportsVision() const;
    
    // Dual-mode LLM interface
    ExecutionPlan generatePlanWithContext(const std::string& userRequest, const LLMContext& context);
    std::string buildContextualPrompt(const std::string& userRequest, const LLMContext& context);
    std::string encodeImageAsBase64(const std::vector<uint8_t>& imageData, const std::string& format);
    
    // Validation and testing
    bool validateConfiguration();
    bool testConnection();
    ExecutionPlan validatePlan(const ExecutionPlan& plan);
    
    // Rate limiting and quota
    void setRateLimit(int requestsPerMinute);
    bool checkRateLimit();
    void updateUsageStats(int tokensUsed);
    
    // Error handling
    struct LLMError {
        int code;
        std::string message;
        std::string type;
        bool isRetryable;
        
        LLMError() : code(0), isRetryable(false) {}
    };
    
    LLMError getLastError() const;

private:
    std::unique_ptr<HttpClient> m_httpClient;
    
    // Configuration
    std::string m_apiKey;
    std::string m_baseUrl;
    std::string m_modelName;
    int m_timeoutMs;
    int m_maxRetries;
    double m_temperature;
    int m_maxTokens;
    Provider m_provider;
    std::map<std::string, std::string> m_customHeaders;
    
    // Vision capabilities
    VisionCapabilities m_visionCapabilities;
    
    // Context and history
    LLMContext m_context;
    std::vector<LLMMessage> m_messageHistory;
    size_t m_maxHistorySize;
    
    // Rate limiting
    int m_requestsPerMinute;
    std::vector<std::chrono::system_clock::time_point> m_requestTimes;
    
    // Usage tracking
    int m_totalTokensUsed;
    int m_totalRequests;
    
    // Error tracking
    LLMError m_lastError;
    
    // Internal methods
    nlohmann::json createRequestPayload(const std::vector<LLMMessage>& messages);
    ExecutionPlan parseResponse(const nlohmann::json& response);
    ExecutionPlan parseResponseWithRules(const nlohmann::json& response);
    ExecutionPlan parseResponseFallback(const nlohmann::json& response);
    std::string extractContentFromResponse(const nlohmann::json& response);
    std::string applyExtractionPatterns(const std::string& content, const nlohmann::json& parsingRules);
    std::string applyCleaningRules(const std::string& content, const nlohmann::json& parsingRules);
    bool validateParsedContent(const nlohmann::json& parsedJson, const nlohmann::json& parsingRules);
    void initializeVisionCapabilities(const nlohmann::json& providerConfig = nlohmann::json{});
    std::string buildSystemPrompt(const LLMContext& context);
    
    // Configurable prompt system methods
    std::string loadSystemPromptTemplate();
    std::string substituteTemplateVariables(const std::string& templateContent, const LLMContext& context);
    std::string generateCommandReference();
    std::string generateFallbackCommandReference();
    std::string generateContextInfo(const LLMContext& context);
    std::string replaceAll(const std::string& str, const std::string& from, const std::string& to);
    // Removed hardcoded fallback methods - using configurable templates only
    
    // Contextual prompt template methods
    std::string loadVisionPromptTemplate();
    std::string loadTextPromptTemplate();
    std::string substituteContextualVariables(const std::string& templateContent, const std::string& userRequest, const LLMContext& context);
    std::string generateStructuredContext(const LLMContext& context);
    // Removed hardcoded fallback methods - using configurable templates only
    
    std::string getApiEndpoint() const;
    std::map<std::string, std::string> getRequestHeaders() const;
    
    // Provider-specific implementations
    nlohmann::json createOpenAIRequest(const std::vector<LLMMessage>& messages);
    nlohmann::json createAnthropicRequest(const std::vector<LLMMessage>& messages);
    ExecutionPlan parseOpenAIResponse(const nlohmann::json& response);
    ExecutionPlan parseAnthropicResponse(const nlohmann::json& response);
    
    // Rate limiting helpers
    void cleanupOldRequests();
    void addRequestTime();
    
    // Validation helpers
    bool isValidCommand(const nlohmann::json& command);
    bool hasRequiredFields(const nlohmann::json& command);
    
    // Error handling helpers
    void handleHttpError(const HttpResponse& response);
    void setError(int code, const std::string& message, const std::string& type, bool retryable);
    
    // Fallback for development/testing
    ExecutionPlan simulateLLMResponse(const std::string& request);
};

} // namespace burwell

#endif // BURWELL_LLM_CONNECTOR_H