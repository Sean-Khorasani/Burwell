#ifndef BURWELL_CONVERSATION_MANAGER_H
#define BURWELL_CONVERSATION_MANAGER_H

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declarations
class LLMConnector;
class EnvironmentalPerception;
class UIModule;
struct ExecutionContext;

/**
 * @class ConversationManager
 * @brief Manages LLM conversations for dynamic execution and adaptation
 * 
 * This class handles multi-turn conversations with the LLM for environmental
 * data requests, command adaptation, and user interaction management.
 */
class ConversationManager {
public:
    ConversationManager();
    ~ConversationManager();

    // Dependencies
    void setLLMConnector(std::shared_ptr<LLMConnector> llm);
    void setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception);
    void setUIModule(std::shared_ptr<UIModule> ui);

    // Configuration
    void setMaxConversationTurns(int maxTurns);
    void setConversationTimeoutMs(int timeoutMs);
    void setConversationExpirationMs(int expirationMs);

    // Conversation lifecycle
    std::string initiateConversation(const std::string& userInput, ExecutionContext& context);
    TaskExecutionResult processConversationTurn(const std::string& conversationId, const nlohmann::json& llmResponse);
    void endConversation(const std::string& conversationId);
    bool isConversationActive(const std::string& conversationId) const;

    // Environmental data handling
    nlohmann::json handleEnvironmentalDataRequest(const std::string& conversationId, const nlohmann::json& request);
    bool shouldRequestAdditionalEnvironmentalData(const ExecutionContext& context, const nlohmann::json& currentPlan);
    nlohmann::json generateEnvironmentalDataQuery(const ExecutionContext& context, const std::string& queryType);

    // Command adaptation
    TaskExecutionResult adaptCommandsBasedOnFeedback(const std::string& conversationId, const nlohmann::json& adaptationRequest);
    nlohmann::json suggestCommandAlternatives(const std::string& conversationId, const nlohmann::json& failedCommand);

    // Context management
    void updateConversationContext(const std::string& conversationId, const nlohmann::json& newContext);
    nlohmann::json getConversationContext(const std::string& conversationId) const;
    nlohmann::json getConversationHistory(const std::string& conversationId) const;

    // User interaction
    struct UserInteractionRequest {
        std::string interactionId;
        std::string conversationId;
        std::string promptMessage;
        std::string inputType;  // "text", "choice", "password", "file_path", "confirmation"
        nlohmann::json inputOptions;
        std::chrono::steady_clock::time_point requestTime;
        std::chrono::steady_clock::time_point timeoutTime;
        bool isUrgent;
        bool hasResponse;
        nlohmann::json userResponse;
    };

    std::string requestUserInput(const std::string& conversationId, const std::string& prompt, 
                                const std::string& inputType, const nlohmann::json& options = {});
    TaskExecutionResult waitForUserResponse(const std::string& interactionId, int timeoutMs = 30000);
    bool provideUserResponse(const std::string& interactionId, const nlohmann::json& response);
    std::vector<UserInteractionRequest> getPendingUserInteractions() const;
    void cancelUserInteraction(const std::string& interactionId);

    // Conversation management
    std::vector<std::string> getActiveConversations() const;
    void cleanupExpiredConversations();
    size_t getActiveConversationCount() const;

    // Error recovery conversations
    std::string initiateErrorRecoveryConversation(const std::string& failedCommand, 
                                                  const nlohmann::json& errorContext);
    TaskExecutionResult generateRecoveryPlan(const std::string& conversationId);

private:
    // Conversation state
    struct ConversationState {
        std::string conversationId;
        std::vector<nlohmann::json> messageHistory;
        nlohmann::json currentContext;
        bool awaitingLLMResponse;
        bool requiresEnvironmentalUpdate;
        std::chrono::steady_clock::time_point lastInteraction;
        std::map<std::string, nlohmann::json> environmentalQueries;
        int turnCount;
        int maxTurns;
        std::string originalRequest;
        ExecutionContext* executionContext;
        
        ConversationState() : awaitingLLMResponse(false), 
                            requiresEnvironmentalUpdate(false),
                            turnCount(0),
                            maxTurns(10),
                            executionContext(nullptr) {}
    };

    // Dependencies
    std::shared_ptr<LLMConnector> m_llmConnector;
    std::shared_ptr<EnvironmentalPerception> m_perception;
    std::shared_ptr<UIModule> m_ui;

    // Configuration
    int m_maxConversationTurns;
    int m_conversationTimeoutMs;
    int m_conversationExpirationMs;

    // State storage
    std::map<std::string, ConversationState> m_activeConversations;
    std::map<std::string, UserInteractionRequest> m_pendingUserInteractions;
    mutable std::mutex m_conversationMutex;
    mutable std::mutex m_interactionMutex;

    // Helper methods
    std::string generateConversationId();
    std::string generateInteractionId();
    nlohmann::json buildLLMPrompt(const ConversationState& state, const std::string& userInput);
    nlohmann::json extractCommandsFromLLMResponse(const nlohmann::json& response);
    bool validateLLMResponse(const nlohmann::json& response);
    void appendToMessageHistory(const std::string& conversationId, const nlohmann::json& message);
    
    // Environmental data helpers
    nlohmann::json gatherRequestedEnvironmentalData(const nlohmann::json& request);
    nlohmann::json getWindowInformation();
    nlohmann::json getApplicationState();
    nlohmann::json getSystemResources();
    
    // Conversation flow
    bool shouldContinueConversation(const ConversationState& state) const;
    void handleConversationTimeout(const std::string& conversationId);
    void finalizeConversation(const std::string& conversationId, const TaskExecutionResult& result);

    // User interaction helpers
    void displayUserPrompt(const UserInteractionRequest& request);
    nlohmann::json validateUserResponse(const UserInteractionRequest& request, const nlohmann::json& response);
    void cleanupExpiredUserInteractions();
};

} // namespace burwell

#endif // BURWELL_CONVERSATION_MANAGER_H