#include "conversation_manager.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../llm_connector/llm_connector.h"
#include "../environmental_perception/environmental_perception.h"
#include "../ui_module/ui_module.h"
#include "../common/structured_logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>

namespace burwell {

ConversationManager::ConversationManager()
    : m_maxConversationTurns(10)
    , m_conversationTimeoutMs(300000)  // 5 minutes
    , m_conversationExpirationMs(600000) {  // 10 minutes
    SLOG_DEBUG().message("ConversationManager initialized");
}

ConversationManager::~ConversationManager() {
    SLOG_DEBUG().message("ConversationManager destroyed");
}

void ConversationManager::setLLMConnector(std::shared_ptr<LLMConnector> llm) {
    m_llmConnector = llm;
}

void ConversationManager::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) {
    m_perception = perception;
}

void ConversationManager::setUIModule(std::shared_ptr<UIModule> ui) {
    m_ui = ui;
}

void ConversationManager::setMaxConversationTurns(int maxTurns) {
    m_maxConversationTurns = maxTurns;
}

void ConversationManager::setConversationTimeoutMs(int timeoutMs) {
    m_conversationTimeoutMs = timeoutMs;
}

void ConversationManager::setConversationExpirationMs(int expirationMs) {
    m_conversationExpirationMs = expirationMs;
}

std::string ConversationManager::initiateConversation(const std::string& userInput, ExecutionContext& context) {
    std::string conversationId = generateConversationId();
    
    {
        std::lock_guard<std::mutex> lock(m_conversationMutex);
        
        ConversationState& state = m_activeConversations[conversationId];
        state.conversationId = conversationId;
        state.originalRequest = userInput;
        state.executionContext = &context;
        state.maxTurns = m_maxConversationTurns;
        state.lastInteraction = std::chrono::steady_clock::now();
        
        // Initialize conversation context
        state.currentContext = {
            {"userRequest", userInput},
            {"environment", gatherRequestedEnvironmentalData({})},
            {"executionContext", {
                {"requestId", context.requestId},
                {"variables", context.variables}
            }}
        };
        
        // Add initial user message to history
        nlohmann::json userMessage = {
            {"role", "user"},
            {"content", userInput},
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
        };
        state.messageHistory.push_back(userMessage);
    }
    
    SLOG_INFO().message("Initiated conversation").context("conversation_id", conversationId);
    
    // Build and send initial LLM prompt
    if (m_llmConnector) {
        ConversationState& state = m_activeConversations[conversationId];
        auto prompt = buildLLMPrompt(state, userInput);
        
        state.awaitingLLMResponse = true;
        
        try {
            auto llmResponse = m_llmConnector->sendPrompt(prompt.dump());
            processConversationTurn(conversationId, llmResponse);
        } catch (const std::exception& e) {
            SLOG_ERROR().message("Failed to get LLM response").context("error", e.what());
            state.awaitingLLMResponse = false;
        }
    }
    
    return conversationId;
}

TaskExecutionResult ConversationManager::processConversationTurn(const std::string& conversationId, const nlohmann::json& llmResponse) {
    TaskExecutionResult result;
    result.success = false;
    
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it == m_activeConversations.end()) {
        result.errorMessage = "Conversation not found: " + conversationId;
        return result;
    }
    
    ConversationState& state = it->second;
    state.awaitingLLMResponse = false;
    state.lastInteraction = std::chrono::steady_clock::now();
    state.turnCount++;
    
    // Add LLM response to history
    nlohmann::json assistantMessage = {
        {"role", "assistant"},
        {"content", llmResponse},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };
    state.messageHistory.push_back(assistantMessage);
    
    // Validate and extract commands
    if (!validateLLMResponse(llmResponse)) {
        result.errorMessage = "Invalid LLM response format";
        return result;
    }
    
    auto commands = extractCommandsFromLLMResponse(llmResponse);
    
    // Check for environmental data requests
    if (llmResponse.contains("environmental_data_request")) {
        auto envData = handleEnvironmentalDataRequest(conversationId, llmResponse["environmental_data_request"]);
        state.currentContext["environment"] = envData;
        state.requiresEnvironmentalUpdate = true;
        
        // Continue conversation with updated environment
        if (shouldContinueConversation(state)) {
            auto prompt = buildLLMPrompt(state, "Environment data updated");
            state.awaitingLLMResponse = true;
            
            try {
                auto newResponse = m_llmConnector->sendPrompt(prompt.dump());
                return processConversationTurn(conversationId, newResponse);
            } catch (const std::exception& e) {
                result.errorMessage = "Failed to continue conversation: " + std::string(e.what());
            }
        }
    }
    
    // Check for user interaction requests
    if (llmResponse.contains("user_interaction_request")) {
        auto interactionRequest = llmResponse["user_interaction_request"];
        std::string interactionId = requestUserInput(
            conversationId,
            interactionRequest.value("prompt", ""),
            interactionRequest.value("type", "text"),
            interactionRequest.value("options", nlohmann::json::object())
        );
        
        // Wait for user response
        auto interactionResult = waitForUserResponse(interactionId);
        if (interactionResult.success) {
            // Continue conversation with user input
            if (shouldContinueConversation(state)) {
                auto prompt = buildLLMPrompt(state, "User provided input");
                state.awaitingLLMResponse = true;
                
                try {
                    auto newResponse = m_llmConnector->sendPrompt(prompt.dump());
                    return processConversationTurn(conversationId, newResponse);
                } catch (const std::exception& e) {
                    result.errorMessage = "Failed to continue after user input: " + std::string(e.what());
                }
            }
        }
    }
    
    // Process commands if any
    if (!commands.empty()) {
        result.success = true;
        result.output = commands.dump();
        
        // Update execution context with conversation results
        if (state.executionContext) {
            state.executionContext->variables["conversation_commands"] = commands;
            state.executionContext->variables["conversation_context"] = state.currentContext;
        }
    }
    
    // Check if conversation should end
    if (!shouldContinueConversation(state)) {
        finalizeConversation(conversationId, result);
    }
    
    return result;
}

void ConversationManager::endConversation(const std::string& conversationId) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        SLOG_INFO().message("Ending conversation").context("conversation_id", conversationId);
        m_activeConversations.erase(it);
    }
}

bool ConversationManager::isConversationActive(const std::string& conversationId) const {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    return m_activeConversations.find(conversationId) != m_activeConversations.end();
}

nlohmann::json ConversationManager::handleEnvironmentalDataRequest(const std::string& conversationId, const nlohmann::json& request) {
    nlohmann::json environmentData;
    
    if (!m_perception) {
        SLOG_WARNING().message("Environmental perception not available");
        return environmentData;
    }
    
    // Process specific data requests
    if (request.contains("windows") && request["windows"]) {
        environmentData["windows"] = getWindowInformation();
    }
    
    if (request.contains("applicationState") && request["applicationState"]) {
        environmentData["applicationState"] = getApplicationState();
    }
    
    if (request.contains("systemResources") && request["systemResources"]) {
        environmentData["systemResources"] = getSystemResources();
    }
    
    if (request.contains("screenshot") && request["screenshot"]) {
        // Take screenshot if requested
        auto screenshot = m_perception->captureScreen();
        if (screenshot.isValid()) {
            environmentData["screenshot"] = {
                {"available", true},
                {"width", screenshot.width},
                {"height", screenshot.height}
            };
        }
    }
    
    // Store in conversation state
    {
        std::lock_guard<std::mutex> lock(m_conversationMutex);
        auto it = m_activeConversations.find(conversationId);
        if (it != m_activeConversations.end()) {
            it->second.environmentalQueries[std::to_string(it->second.turnCount)] = environmentData;
        }
    }
    
    return environmentData;
}

bool ConversationManager::shouldRequestAdditionalEnvironmentalData(const ExecutionContext& context, const nlohmann::json& currentPlan) {
    // Check if plan requires environmental data we don't have
    if (currentPlan.contains("required_environment")) {
        auto required = currentPlan["required_environment"];
        
        if (required.contains("windows") && !context.currentEnvironment.contains("windows")) {
            return true;
        }
        
        if (required.contains("activeWindow") && !context.currentEnvironment.contains("activeWindow")) {
            return true;
        }
    }
    
    return false;
}

nlohmann::json ConversationManager::generateEnvironmentalDataQuery(const ExecutionContext& context, const std::string& queryType) {
    nlohmann::json query = {
        {"type", "environmental_data_request"},
        {"queryType", queryType},
        {"context", {
            {"currentTask", context.originalRequest},
            {"executionStage", context.executionLog.size()}
        }}
    };
    
    if (queryType == "windows") {
        query["windows"] = true;
        query["includeHidden"] = false;
    } else if (queryType == "fullEnvironment") {
        query["windows"] = true;
        query["applicationState"] = true;
        query["systemResources"] = true;
    }
    
    return query;
}

TaskExecutionResult ConversationManager::adaptCommandsBasedOnFeedback(const std::string& conversationId, const nlohmann::json& adaptationRequest) {
    TaskExecutionResult result;
    result.success = false;
    
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it == m_activeConversations.end()) {
        result.errorMessage = "Conversation not found";
        return result;
    }
    
    ConversationState& state = it->second;
    
    // Build adaptation prompt
    nlohmann::json adaptationPrompt = {
        {"type", "command_adaptation"},
        {"original_commands", adaptationRequest.value("original_commands", nlohmann::json::array())},
        {"feedback", adaptationRequest.value("feedback", "")},
        {"environment_changes", adaptationRequest.value("environment_changes", nlohmann::json::object())},
        {"conversation_context", state.currentContext}
    };
    
    // Send to LLM for adaptation
    if (m_llmConnector) {
        try {
            auto response = m_llmConnector->sendPrompt(adaptationPrompt.dump());
            
            if (response.contains("adapted_commands")) {
                result.success = true;
                result.output = response["adapted_commands"].dump();
            }
        } catch (const std::exception& e) {
            result.errorMessage = "LLM adaptation failed: " + std::string(e.what());
        }
    }
    
    return result;
}

nlohmann::json ConversationManager::suggestCommandAlternatives(const std::string& conversationId, const nlohmann::json& failedCommand) {
    nlohmann::json suggestions = nlohmann::json::array();
    
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it == m_activeConversations.end()) {
        return suggestions;
    }
    
    ConversationState& state = it->second;
    
    // Build alternatives request
    nlohmann::json alternativesPrompt = {
        {"type", "suggest_alternatives"},
        {"failed_command", failedCommand},
        {"error_context", failedCommand.value("error", "")},
        {"conversation_history", state.messageHistory},
        {"current_environment", state.currentContext["environment"]}
    };
    
    // Request alternatives from LLM
    if (m_llmConnector) {
        try {
            auto response = m_llmConnector->sendPrompt(alternativesPrompt.dump());
            
            if (response.contains("alternatives")) {
                suggestions = response["alternatives"];
            }
        } catch (const std::exception& e) {
            SLOG_ERROR().message("Failed to get command alternatives").context("error", e.what());
        }
    }
    
    return suggestions;
}

void ConversationManager::updateConversationContext(const std::string& conversationId, const nlohmann::json& newContext) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        // Merge new context with existing
        for (auto& [key, value] : newContext.items()) {
            it->second.currentContext[key] = value;
        }
        
        it->second.lastInteraction = std::chrono::steady_clock::now();
    }
}

nlohmann::json ConversationManager::getConversationContext(const std::string& conversationId) const {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        return it->second.currentContext;
    }
    
    return nlohmann::json::object();
}

nlohmann::json ConversationManager::getConversationHistory(const std::string& conversationId) const {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        return nlohmann::json(it->second.messageHistory);
    }
    
    return nlohmann::json::array();
}

std::string ConversationManager::requestUserInput(const std::string& conversationId, const std::string& prompt, 
                                                 const std::string& inputType, const nlohmann::json& options) {
    std::string interactionId = generateInteractionId();
    
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        
        UserInteractionRequest& request = m_pendingUserInteractions[interactionId];
        request.interactionId = interactionId;
        request.conversationId = conversationId;
        request.promptMessage = prompt;
        request.inputType = inputType;
        request.inputOptions = options;
        request.requestTime = std::chrono::steady_clock::now();
        request.timeoutTime = request.requestTime + std::chrono::milliseconds(m_conversationTimeoutMs);
        request.isUrgent = options.value("urgent", false);
        request.hasResponse = false;
    }
    
    // Display prompt to user if UI available
    displayUserPrompt(m_pendingUserInteractions[interactionId]);
    
    SLOG_INFO().message("Requested user input").context("interaction_id", interactionId);
    return interactionId;
}

TaskExecutionResult ConversationManager::waitForUserResponse(const std::string& interactionId, int timeoutMs) {
    TaskExecutionResult result;
    result.success = false;
    
    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::milliseconds(timeoutMs);
    
    while (std::chrono::steady_clock::now() < endTime) {
        {
            std::lock_guard<std::mutex> lock(m_interactionMutex);
            auto it = m_pendingUserInteractions.find(interactionId);
            if (it != m_pendingUserInteractions.end() && it->second.hasResponse) {
                result.success = true;
                result.output = it->second.userResponse.dump();
                m_pendingUserInteractions.erase(it);
                return result;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    result.errorMessage = "User response timeout";
    return result;
}

bool ConversationManager::provideUserResponse(const std::string& interactionId, const nlohmann::json& response) {
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    
    auto it = m_pendingUserInteractions.find(interactionId);
    if (it != m_pendingUserInteractions.end()) {
        // Validate response
        auto validatedResponse = validateUserResponse(it->second, response);
        
        it->second.userResponse = validatedResponse;
        it->second.hasResponse = true;
        
        SLOG_INFO().message("User response provided").context("interaction_id", interactionId);
        return true;
    }
    
    return false;
}

std::vector<ConversationManager::UserInteractionRequest> ConversationManager::getPendingUserInteractions() const {
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    
    std::vector<UserInteractionRequest> pending;
    for (const auto& [id, request] : m_pendingUserInteractions) {
        if (!request.hasResponse) {
            pending.push_back(request);
        }
    }
    
    return pending;
}

void ConversationManager::cancelUserInteraction(const std::string& interactionId) {
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_pendingUserInteractions.erase(interactionId);
}

std::vector<std::string> ConversationManager::getActiveConversations() const {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    std::vector<std::string> activeIds;
    for (const auto& [id, state] : m_activeConversations) {
        activeIds.push_back(id);
    }
    
    return activeIds;
}

void ConversationManager::cleanupExpiredConversations() {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto it = m_activeConversations.begin();
    
    while (it != m_activeConversations.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.lastInteraction).count();
            
        if (elapsed > m_conversationExpirationMs) {
            SLOG_INFO().message("Cleaning up expired conversation").context("conversation_id", it->first);
            it = m_activeConversations.erase(it);
        } else {
            ++it;
        }
    }
    
    cleanupExpiredUserInteractions();
}

size_t ConversationManager::getActiveConversationCount() const {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    return m_activeConversations.size();
}

std::string ConversationManager::initiateErrorRecoveryConversation(const std::string& failedCommand, 
                                                                  const nlohmann::json& errorContext) {
    // Build error recovery prompt
    std::string prompt = "The following command failed: " + failedCommand + 
                        "\nError context: " + errorContext.dump() + 
                        "\nPlease suggest alternative approaches.";
    
    ExecutionContext dummyContext;
    dummyContext.requestId = generateConversationId();
    dummyContext.originalRequest = "Error recovery for: " + failedCommand;
    
    return initiateConversation(prompt, dummyContext);
}

TaskExecutionResult ConversationManager::generateRecoveryPlan(const std::string& conversationId) {
    TaskExecutionResult result;
    result.success = false;
    
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it == m_activeConversations.end()) {
        result.errorMessage = "Conversation not found";
        return result;
    }
    
    ConversationState& state = it->second;
    
    // Build recovery plan request
    nlohmann::json recoveryRequest = {
        {"type", "generate_recovery_plan"},
        {"conversation_history", state.messageHistory},
        {"current_context", state.currentContext},
        {"original_request", state.originalRequest}
    };
    
    if (m_llmConnector) {
        try {
            auto response = m_llmConnector->sendPrompt(recoveryRequest.dump());
            
            if (response.contains("recovery_plan")) {
                result.success = true;
                result.output = response["recovery_plan"].dump();
            }
        } catch (const std::exception& e) {
            result.errorMessage = "Failed to generate recovery plan: " + std::string(e.what());
        }
    }
    
    return result;
}

// Private methods

std::string ConversationManager::generateConversationId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hexChars = "0123456789ABCDEF";
    
    std::stringstream ss;
    ss << "CONV-";
    
    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d%H%M%S");
    ss << "-";
    
    // Add random part
    for (int i = 0; i < 8; ++i) {
        ss << hexChars[dis(gen)];
    }
    
    return ss.str();
}

std::string ConversationManager::generateInteractionId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hexChars = "0123456789ABCDEF";
    
    std::stringstream ss;
    ss << "INT-";
    
    for (int i = 0; i < 12; ++i) {
        ss << hexChars[dis(gen)];
    }
    
    return ss.str();
}

nlohmann::json ConversationManager::buildLLMPrompt(const ConversationState& state, const std::string& userInput) {
    nlohmann::json prompt = {
        {"type", "conversation"},
        {"conversationId", state.conversationId},
        {"turn", state.turnCount},
        {"userInput", userInput},
        {"conversationHistory", state.messageHistory},
        {"currentContext", state.currentContext},
        {"requiresEnvironmentalUpdate", state.requiresEnvironmentalUpdate}
    };
    
    // Add specific instructions based on state
    if (state.turnCount == 0) {
        prompt["instructions"] = "Analyze the user request and generate an execution plan. "
                               "If you need environmental data, request it. "
                               "If you need user clarification, request it.";
    } else {
        prompt["instructions"] = "Continue the conversation based on the history and context. "
                               "Generate commands or request additional information as needed.";
    }
    
    return prompt;
}

nlohmann::json ConversationManager::extractCommandsFromLLMResponse(const nlohmann::json& response) {
    nlohmann::json commands = nlohmann::json::array();
    
    if (response.contains("commands") && response["commands"].is_array()) {
        commands = response["commands"];
    } else if (response.contains("execution_plan") && response["execution_plan"].contains("commands")) {
        commands = response["execution_plan"]["commands"];
    }
    
    return commands;
}

bool ConversationManager::validateLLMResponse(const nlohmann::json& response) {
    // Basic validation
    if (!response.is_object()) {
        return false;
    }
    
    // Must have either commands, environmental request, or user interaction request
    bool hasValidContent = response.contains("commands") ||
                          response.contains("environmental_data_request") ||
                          response.contains("user_interaction_request") ||
                          response.contains("message");
    
    return hasValidContent;
}

void ConversationManager::appendToMessageHistory(const std::string& conversationId, const nlohmann::json& message) {
    // Should be called with lock already held
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        it->second.messageHistory.push_back(message);
        
        // Limit history size
        while (it->second.messageHistory.size() > 50) {
            it->second.messageHistory.erase(it->second.messageHistory.begin());
        }
    }
}

nlohmann::json ConversationManager::gatherRequestedEnvironmentalData(const nlohmann::json& request) {
    nlohmann::json data;
    
    if (!m_perception) {
        return data;
    }
    
    // Default to basic environment info
    data = m_perception->gatherEnvironmentInfo();
    
    // Add specific requested data
    if (request.contains("includeScreenshot") && request["includeScreenshot"]) {
        auto screenshot = m_perception->captureScreen();
        data["screenshot"] = {
            {"available", screenshot.isValid()},
            {"width", screenshot.width},
            {"height", screenshot.height}
        };
    }
    
    return data;
}

nlohmann::json ConversationManager::getWindowInformation() {
    if (!m_perception) {
        return nlohmann::json::array();
    }
    
    nlohmann::json windows = nlohmann::json::array();
    auto windowList = m_perception->getVisibleWindows();
    
    for (const auto& window : windowList) {
        windows.push_back({
            {"title", window.title},
            {"className", window.className},
            {"processName", window.processName},
            {"isVisible", window.isVisible},
            {"bounds", {
                {"x", window.bounds.x},
                {"y", window.bounds.y},
                {"width", window.bounds.width},
                {"height", window.bounds.height}
            }}
        });
    }
    
    return windows;
}

nlohmann::json ConversationManager::getApplicationState() {
    nlohmann::json appState;
    
    if (m_perception) {
        auto activeWindow = m_perception->getActiveWindow();
        appState["activeWindow"] = {
            {"title", activeWindow.title},
            {"className", activeWindow.className},
            {"processName", activeWindow.processName}
        };
    }
    
    return appState;
}

nlohmann::json ConversationManager::getSystemResources() {
    nlohmann::json resources;
    
    // Basic system info - would be expanded with actual resource monitoring
    resources["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    
    return resources;
}

bool ConversationManager::shouldContinueConversation(const ConversationState& state) const {
    // Check turn limit
    if (state.turnCount >= state.maxTurns) {
        return false;
    }
    
    // Check if awaiting response
    if (state.awaitingLLMResponse) {
        return true;
    }
    
    // Check if there are pending interactions
    bool hasPendingInteractions = false;
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        for (const auto& [id, request] : m_pendingUserInteractions) {
            if (request.conversationId == state.conversationId && !request.hasResponse) {
                hasPendingInteractions = true;
                break;
            }
        }
    }
    
    return hasPendingInteractions || state.requiresEnvironmentalUpdate;
}

void ConversationManager::handleConversationTimeout(const std::string& conversationId) {
    SLOG_WARNING().message("Conversation timeout").context("conversation_id", conversationId);
    
    // Cancel any pending interactions
    std::vector<std::string> interactionsToCancel;
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        for (const auto& [id, request] : m_pendingUserInteractions) {
            if (request.conversationId == conversationId) {
                interactionsToCancel.push_back(id);
            }
        }
    }
    
    for (const auto& id : interactionsToCancel) {
        cancelUserInteraction(id);
    }
    
    // End the conversation
    endConversation(conversationId);
}

void ConversationManager::finalizeConversation(const std::string& conversationId, const TaskExecutionResult& result) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    auto it = m_activeConversations.find(conversationId);
    if (it != m_activeConversations.end()) {
        // Log final state
        SLOG_INFO().message("Finalizing conversation")
            .context("conversation_id", conversationId)
            .context("turns", it->second.turnCount);
        
        // Store final result in execution context if available
        if (it->second.executionContext) {
            it->second.executionContext->variables["conversation_result"] = {
                {"success", result.success},
                {"output", result.output},
                {"turns", it->second.turnCount}
            };
        }
    }
}

void ConversationManager::displayUserPrompt(const UserInteractionRequest& request) {
    if (!m_ui) {
        return;
    }
    
    // Format prompt based on type
    std::string formattedPrompt = request.promptMessage;
    
    if (request.inputType == "choice" && request.inputOptions.contains("choices")) {
        formattedPrompt += "\nOptions:";
        int i = 1;
        for (const auto& choice : request.inputOptions["choices"]) {
            formattedPrompt += "\n  " + std::to_string(i++) + ". " + choice.get<std::string>();
        }
    }
    
    m_ui->displayFeedback(formattedPrompt);
}

nlohmann::json ConversationManager::validateUserResponse(const UserInteractionRequest& request, const nlohmann::json& response) {
    nlohmann::json validated = response;
    
    if (request.inputType == "choice") {
        // Validate choice is within range
        if (response.is_number() && request.inputOptions.contains("choices")) {
            int choice = response.get<int>();
            int numChoices = request.inputOptions["choices"].size();
            if (choice < 1 || choice > numChoices) {
                validated = 1;  // Default to first choice
            }
        }
    } else if (request.inputType == "confirmation") {
        // Ensure boolean response
        if (!response.is_boolean()) {
            validated = false;  // Default to no
        }
    }
    
    return validated;
}

void ConversationManager::cleanupExpiredUserInteractions() {
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto it = m_pendingUserInteractions.begin();
    
    while (it != m_pendingUserInteractions.end()) {
        if (now > it->second.timeoutTime) {
            SLOG_INFO().message("Cleaning up expired user interaction").context("interaction_id", it->first);
            it = m_pendingUserInteractions.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace burwell