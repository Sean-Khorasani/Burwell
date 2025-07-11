#include "llm_connector.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/config_manager.h"
#include "../common/os_utils.h"
#include "../common/input_validator.h"
#include "../common/file_utils.h"
#include <chrono>
#include <algorithm>
#include <regex>
#include <cctype>
#include <filesystem>
#include <fstream>

using namespace burwell;

// LLMContext implementations
nlohmann::json LLMContext::toJson() const {
    nlohmann::json json = {
        {"activeWindow", activeWindow},
        {"currentDirectory", currentDirectory},
        {"openWindows", openWindows},
        {"environmentInfo", environmentInfo},
        {"recentActions", recentActions},
        {"textDescription", textDescription},
        {"structuredData", structuredData}
    };
    
    // Note: screenshotData is binary and handled separately for vision-capable LLMs
    if (hasScreenshot()) {
        json["hasScreenshot"] = true;
        json["screenshotFormat"] = screenshotFormat;
    }
    
    return json;
}

void LLMContext::fromJson(const nlohmann::json& json) {
    if (json.contains("activeWindow")) activeWindow = json["activeWindow"];
    if (json.contains("currentDirectory")) currentDirectory = json["currentDirectory"];
    if (json.contains("openWindows")) openWindows = json["openWindows"];
    if (json.contains("environmentInfo")) environmentInfo = json["environmentInfo"];
    if (json.contains("recentActions")) recentActions = json["recentActions"];
    if (json.contains("textDescription")) textDescription = json["textDescription"];
    if (json.contains("structuredData")) structuredData = json["structuredData"];
    if (json.contains("screenshotFormat")) screenshotFormat = json["screenshotFormat"];
}

// LLMConnector implementation
LLMConnector::LLMConnector() 
    : m_httpClient(std::make_unique<HttpClient>())
    , m_timeoutMs(30000)
    , m_maxRetries(3)
    , m_temperature(0.7)
    , m_maxTokens(4000)
    , m_provider(Provider::OPENAI)
    , m_maxHistorySize(20)
    , m_requestsPerMinute(60)
    , m_totalTokensUsed(0)
    , m_totalRequests(0) {
    
    // Load configuration with provider selection
    auto& config = ConfigManager::getInstance();
    
    // Get active provider and load its configuration
    std::string activeProvider = config.getActiveProvider();
    nlohmann::json providerConfig = config.loadProviderConfig(activeProvider);
    
    // Load provider-specific settings or fallback to legacy config methods
    if (!providerConfig.empty() && providerConfig.contains("connection")) {
        const auto& connection = providerConfig["connection"];
        
        // Safely get base_url
        if (connection.contains("base_url") && connection["base_url"].is_string()) {
            m_baseUrl = connection["base_url"].get<std::string>();
        } else {
            m_baseUrl = config.getLLMBaseUrl();
        }
        
        // Load API key from environment variable or config
        if (connection.contains("authentication") && connection["authentication"].is_object()) {
            const auto& auth = connection["authentication"];
            if (auth.contains("env_variable") && auth["env_variable"].is_string()) {
                std::string envVar = auth["env_variable"].get<std::string>();
                std::string apiKey = burwell::os::SystemInfo::getEnvironmentVariable(envVar);
                if (apiKey.empty()) {
                    SLOG_WARNING().message("API key not found in environment variable")
                        .context("env_variable", envVar);
                    // Try to get from provider config file directly
                    if (auth.contains("config_key") && auth["config_key"].is_string()) {
                        std::string configKey = auth["config_key"].get<std::string>();
                        if (providerConfig.contains(configKey) && providerConfig[configKey].is_string()) {
                            m_apiKey = providerConfig[configKey].get<std::string>();
                        }
                    }
                } else {
                    m_apiKey = apiKey;
                    SLOG_INFO().message("API key loaded from environment variable")
                        .context("env_variable", envVar);
                }
            } else {
                // Try to get from provider config file directly
                if (auth.contains("config_key") && auth["config_key"].is_string()) {
                    std::string configKey = auth["config_key"].get<std::string>();
                    if (providerConfig.contains(configKey) && providerConfig[configKey].is_string()) {
                        m_apiKey = providerConfig[configKey].get<std::string>();
                    }
                }
            }
        }
        
        // Safely get timeout and retries
        if (connection.contains("timeout_ms") && connection["timeout_ms"].is_number()) {
            m_timeoutMs = connection["timeout_ms"].get<int>();
        } else {
            m_timeoutMs = config.getLLMTimeoutMs();
        }
        
        if (connection.contains("max_retries") && connection["max_retries"].is_number()) {
            m_maxRetries = connection["max_retries"].get<int>();
        } else {
            m_maxRetries = config.getLLMMaxRetries();
        }
        
        // Load model configuration
        if (providerConfig.contains("model_configuration") && providerConfig["model_configuration"].is_object()) {
            const auto& modelConfig = providerConfig["model_configuration"];
            if (modelConfig.contains("default_model") && modelConfig["default_model"].is_string()) {
                m_modelName = modelConfig["default_model"].get<std::string>();
            } else {
                m_modelName = config.getLLMModelName();
            }
        } else {
            m_modelName = config.getLLMModelName();
        }
    } else {
        // Fallback to legacy configuration
        SLOG_WARNING().message("Provider config not found, using legacy configuration")
            .context("provider", activeProvider);
        m_baseUrl = config.getLLMBaseUrl();
        m_modelName = config.getLLMModelName();
        
        // Try to load API key from legacy method only if no provider is set
        std::string legacyApiKey = config.getLLMApiKey();
        if (!legacyApiKey.empty()) {
            m_apiKey = legacyApiKey;
        } else {
            SLOG_ERROR().message("No API key configured for provider")
                .context("provider", activeProvider);
        }
        
        m_timeoutMs = config.getLLMTimeoutMs();
        m_maxRetries = config.getLLMMaxRetries();
    }
    
    // Configure HTTP client
    m_httpClient->setTimeout(m_timeoutMs);
    m_httpClient->setMaxRetries(m_maxRetries);
    m_httpClient->setUserAgent("Burwell/1.0");
    
    // Check if we have a valid API key
    if (m_apiKey.empty()) {
        SLOG_WARNING().message("No API key configured for provider. Please set the appropriate environment variable or add the key to the provider configuration file.")
            .context("provider", activeProvider);
    } else {
        SLOG_INFO().message("API key configured for provider")
            .context("provider", activeProvider);
    }
    
    // Initialize vision capabilities from provider configuration
    initializeVisionCapabilities(providerConfig);
    
    SLOG_INFO().message("LLMConnector initialized")
        .context("provider", activeProvider)
        .context("model", m_modelName);
}

LLMConnector::~LLMConnector() = default;

ExecutionPlan LLMConnector::generatePlan(const std::string& userRequest, const LLMContext& context) {
    BURWELL_TRY_CATCH({
        SLOG_INFO().message("Generating execution plan")
            .context("request", userRequest);
        
        // Check rate limit
        if (!checkRateLimit()) {
            setError(429, "Rate limit exceeded", "RATE_LIMIT", true);
            return ExecutionPlan();
        }
        
        // Build system prompt with context
        std::string systemPrompt = buildSystemPrompt(context);
        
        // Create message sequence
        std::vector<LLMMessage> messages;
        messages.emplace_back("system", systemPrompt);
        messages.emplace_back("user", userRequest);
        
        // Add relevant history
        if (!m_messageHistory.empty()) {
            size_t historyStart = m_messageHistory.size() > 4 ? m_messageHistory.size() - 4 : 0;
            for (size_t i = historyStart; i < m_messageHistory.size(); ++i) {
                messages.push_back(m_messageHistory[i]);
            }
        }
        
        // Send request to LLM
        nlohmann::json response = sendMessage(messages);
        
        // Parse response into execution plan
        ExecutionPlan plan = parseResponse(response);
        
        // Validate the plan
        plan = validatePlan(plan);
        
        // Add to history
        addToHistory(LLMMessage("user", userRequest));
        if (plan.isValid) {
            addToHistory(LLMMessage("assistant", plan.summary));
        }
        
        return plan;
        
    }, "LLMConnector::generatePlan");
    
    return ExecutionPlan();
}

nlohmann::json LLMConnector::sendMessage(const std::vector<LLMMessage>& messages) {
    try {
        if (m_apiKey.empty()) {
            setError(401, "API key not configured", "AUTHENTICATION", false);
            return nlohmann::json{{"error", "No API key configured"}};
        }
        
        // Create request payload based on provider
        nlohmann::json requestPayload;
        switch (m_provider) {
            case Provider::OPENAI:
            case Provider::AZURE_OPENAI:
                requestPayload = createOpenAIRequest(messages);
                break;
            case Provider::ANTHROPIC:
                requestPayload = createAnthropicRequest(messages);
                break;
            case Provider::CUSTOM:
                requestPayload = createRequestPayload(messages);
                break;
        }
        
        // Send HTTP request
        std::string endpoint = getApiEndpoint();
        std::map<std::string, std::string> headers = getRequestHeaders();
        
        SLOG_DEBUG().message("LLM Request")
            .context("url", endpoint);
        SLOG_DEBUG().message("LLM Request Payload")
            .context("payload", requestPayload);
        
        HttpResponse response = m_httpClient->post(endpoint, requestPayload.dump(), headers);
        
        SLOG_DEBUG().message("LLM Response")
            .context("status_code", response.statusCode);
        SLOG_DEBUG().message("LLM Response Body")
            .context("body_length", response.body.length())
            .context("body_preview", response.body.substr(0, 200));
        
        if (!response.success) {
            handleHttpError(response);
            return nlohmann::json{{"error", "HTTP error occurred"}};
        }
        
        // Parse JSON response
        nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
        
        // Update usage stats
        if (jsonResponse.contains("usage") && jsonResponse["usage"].contains("total_tokens")) {
            updateUsageStats(jsonResponse["usage"]["total_tokens"]);
        }
        
        addRequestTime();
        m_totalRequests++;
        
        return jsonResponse;
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().handleException(e, "LLMConnector::sendMessage");
    }
    
    return nlohmann::json{{"error", "Error occurred during processing"}};
}

nlohmann::json LLMConnector::sendPrompt(const std::string& prompt) {
    std::vector<LLMMessage> messages;
    messages.emplace_back("user", prompt);
    return sendMessage(messages);
}

std::string LLMConnector::buildSystemPrompt(const LLMContext& context) {
    // Try to load template from provider configuration first
    std::string templateContent = loadSystemPromptTemplate();
    
    if (templateContent.empty()) {
        // No fallback - configuration should be complete
        SLOG_ERROR().message("System prompt template not found in provider configuration");
        throw std::runtime_error("System prompt template missing from provider configuration. Please check config/llm_providers/[provider].json");
    }
    
    // Replace variables in template
    std::string prompt = substituteTemplateVariables(templateContent, context);
    
    SLOG_DEBUG().message("Using configurable system prompt template");
    return prompt;
}

std::string LLMConnector::loadSystemPromptTemplate() {
    auto& config = ConfigManager::getInstance();
    std::string activeProvider = config.getActiveProvider();
    nlohmann::json providerConfig = config.loadProviderConfig(activeProvider);
    
    if (providerConfig.contains("prompt_templates") && 
        providerConfig["prompt_templates"].is_object() &&
        providerConfig["prompt_templates"].contains("system_prompt") &&
        providerConfig["prompt_templates"]["system_prompt"].is_object() &&
        providerConfig["prompt_templates"]["system_prompt"].contains("template") &&
        providerConfig["prompt_templates"]["system_prompt"]["template"].is_string()) {
        
        return providerConfig["prompt_templates"]["system_prompt"]["template"].get<std::string>();
    }
    
    return ""; // Empty string indicates no template found
}

std::string LLMConnector::substituteTemplateVariables(const std::string& templateContent, const LLMContext& context) {
    std::string result = templateContent;
    
    // Replace {{COMMAND_REFERENCE}} with available commands
    std::string commandReference = generateCommandReference();
    result = replaceAll(result, "{{COMMAND_REFERENCE}}", commandReference);
    
    // Replace {{CONTEXT}} with current system context
    std::string contextInfo = generateContextInfo(context);
    result = replaceAll(result, "{{CONTEXT}}", contextInfo);
    
    // Add more variable substitutions as needed
    result = replaceAll(result, "{{SYSTEM_NAME}}", "Burwell");
    result = replaceAll(result, "{{VERSION}}", "1.0.0");
    
    return result;
}

std::string LLMConnector::generateCommandReference() {
    // Load commands dynamically from CPL configuration (Task #79)
    auto& config = ConfigManager::getInstance();
    
    try {
        // Get CPL commands file path
        std::filesystem::path configDir = std::filesystem::path(config.getConfigPath()).parent_path();
        std::filesystem::path cplCommandsPath = configDir / "cpl" / "commands.json";
        
        if (!std::filesystem::exists(cplCommandsPath)) {
            SLOG_WARNING().message("CPL commands.json not found")
                .context("path", burwell::os::PathUtils::toNativePath(cplCommandsPath.string()));
            return generateFallbackCommandReference();
        }
        
        // Load and parse CPL commands configuration
        nlohmann::json cplConfig;
        if (!utils::FileUtils::loadJsonFromFile(cplCommandsPath.string(), cplConfig)) {
            SLOG_WARNING().message("Failed to load CPL commands.json")
                .context("path", cplCommandsPath);
            return generateFallbackCommandReference();
        }
        
        if (!cplConfig.contains("commands") || !cplConfig["commands"].is_object()) {
            SLOG_WARNING().message("No commands section found in CPL config");
            return generateFallbackCommandReference();
        }
        
        // Use fallback command reference when CPL config is incomplete
        SLOG_INFO().message("Using enhanced command reference for automation");
        return generateFallbackCommandReference();
        
        // Generate command reference from CPL config
        std::ostringstream commands;
        commands << "## Available Commands\n\n";
        
        const auto& commandsConfig = cplConfig["commands"];
        for (const auto& [commandName, commandDef] : commandsConfig.items()) {
            if (!commandDef.is_object()) continue;
            
            // Get command syntax and description
            std::string syntax = commandDef.contains("syntax") && commandDef["syntax"].is_string() 
                               ? commandDef["syntax"].get<std::string>() 
                               : commandName;
            std::string description = commandDef.contains("description") && commandDef["description"].is_string()
                                    ? commandDef["description"].get<std::string>()
                                    : "No description available";
            
            commands << "- **" << syntax << "**: " << description << "\n";
            
            // Add examples if available
            if (commandDef.contains("examples") && commandDef["examples"].is_array()) {
                const auto& examples = commandDef["examples"];
                if (!examples.empty()) {
                    commands << "  Examples: ";
                    for (size_t i = 0; i < examples.size() && i < 2; ++i) {
                        if (i > 0) commands << ", ";
                        commands << "`" << examples[i].get<std::string>() << "`";
                    }
                    commands << "\n";
                }
            }
        }
        
        commands << "\n";
        SLOG_DEBUG().message("Generated command reference from CPL config")
            .context("command_count", commandsConfig.size());
        
        return commands.str();
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to generate command reference from CPL config")
            .context("error", e.what());
        return generateFallbackCommandReference();
    }
}

std::string LLMConnector::generateFallbackCommandReference() {
    // Fallback hardcoded command reference when CPL config is not available
    std::ostringstream commands;
    commands << "## Available Commands\n\n";
    
    commands << "### Basic Automation Commands\n";
    commands << "- **MOUSE_CLICK**: {\"command\": \"MOUSE_CLICK\", \"parameters\": {\"x\": int, \"y\": int, \"button\": \"left|right|middle\"}}\n";
    commands << "- **KEY_TYPE**: {\"command\": \"KEY_TYPE\", \"parameters\": {\"text\": \"string\"}}\n";
    commands << "- **KEY_COMBO**: {\"command\": \"KEY_COMBO\", \"parameters\": {\"keys\": \"ctrl+w|alt+f4|ctrl+shift+t\"}}\n";
    commands << "- **WINDOW_FIND**: {\"command\": \"WINDOW_FIND\", \"parameters\": {\"title\": \"window title\", \"store_as\": \"var_name\"}}\n";
    commands << "- **APP_LAUNCH**: {\"command\": \"APP_LAUNCH\", \"parameters\": {\"path\": \"application path\"}}\n";
    commands << "- **WAIT**: {\"command\": \"WAIT\", \"parameters\": {\"duration\": \"5s|1000ms\"}}\n";
    
    commands << "\n### System Command Execution (POWERFUL)\n";
    commands << "- **SYSTEM_COMMAND**: {\"command\": \"SYSTEM_COMMAND\", \"parameters\": {\"command\": \"any command/powershell\", \"elevated\": true/false}}\n";
    commands << "- **POWERSHELL_COMMAND**: {\"command\": \"POWERSHELL_COMMAND\", \"parameters\": {\"script\": \"PowerShell script\", \"elevated\": true/false}}\n";
    commands << "- **CMD_COMMAND**: {\"command\": \"CMD_COMMAND\", \"parameters\": {\"command\": \"cmd command\", \"elevated\": true/false}}\n";
    
    commands << "\n### Browser Tab Automation - VISUAL APPROACH REQUIRED\n";
    commands << "For closing specific tabs (like Extensions tabs), use VISUAL coordination:\n\n";
    
    commands << "**PREFERRED Method - Visual Tab Management:**\n";
    commands << "1. Find Chrome windows: WINDOW_FIND with title='Chrome'\n";
    commands << "2. Activate window: WINDOW_ACTIVATE with window handle\n";
    commands << "3. Wait for activation: WAIT 500ms (critical for visual accuracy)\n";
    commands << "4. Analyze screen description to locate specific tab\n";
    commands << "5. Click tab close button: MOUSE_CLICK at exact coordinates\n\n";
    
    commands << "**WRONG Method - Keyboard Shortcuts:**\n";
    commands << "- NEVER use Ctrl+W for specific tabs (closes active tab, not target tab)\n";
    commands << "- NEVER use PowerShell for individual tab operations\n\n";
    
    commands << "**PowerShell Only For Entire Windows:**\n";
    commands << "- Use Get-Process + CloseMainWindow() only for closing entire browser windows\n";
    commands << "- Example: Get-Process chrome | Where-Object {condition} | ForEach-Object {$_.CloseMainWindow()}\n\n";
    
    commands << "CRITICAL: For tab-specific tasks, always use visual coordinates from screen description!\n";
    
    commands << "\n### Window Management Commands\n";
    commands << "- **WINDOW_FIND**: {\"command\": \"WINDOW_FIND\", \"parameters\": {\"title\": \"partial title\", \"store_as\": \"var_name\"}}\n";
    commands << "- **WINDOW_ACTIVATE**: {\"command\": \"WINDOW_ACTIVATE\", \"parameters\": {\"handle\": window_handle}}\n";
    commands << "- **WINDOW_LIST**: {\"command\": \"WINDOW_LIST\", \"parameters\": {}}\n";
    commands << "- **WINDOW_CLOSE**: {\"command\": \"WINDOW_CLOSE\", \"parameters\": {\"handle\": window_handle}}\n";
    
    commands << "\nCRITICAL: Always use 'parameters' not 'args' or 'params'.\n";
    commands << "CRITICAL: For complex tasks like browser automation, prefer SYSTEM_COMMAND or POWERSHELL_COMMAND over basic mouse clicks.\n";
    commands << "\n";
    SLOG_DEBUG().message("Using comprehensive command reference with system automation");
    return commands.str();
}

std::string LLMConnector::generateContextInfo(const LLMContext& context) {
    std::ostringstream contextInfo;
    
    if (!context.activeWindow.empty()) {
        contextInfo << "Active Window: " << context.activeWindow << "\n";
    }
    if (!context.currentDirectory.empty()) {
        contextInfo << "Current Directory: " << context.currentDirectory << "\n";
    }
    if (!context.openWindows.empty()) {
        contextInfo << "Open Windows: ";
        for (const auto& window : context.openWindows) {
            contextInfo << window << ", ";
        }
        contextInfo << "\n";
    }
    
    return contextInfo.str();
}

std::string LLMConnector::replaceAll(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t startPos = 0;
    while ((startPos = result.find(from, startPos)) != std::string::npos) {
        result.replace(startPos, from.length(), to);
        startPos += to.length();
    }
    return result;
}

// Removed hardcoded fallback - system now uses configurable templates only

ExecutionPlan LLMConnector::parseResponse(const nlohmann::json& response) {
    ExecutionPlan plan;
    
    BURWELL_TRY_CATCH({
        // Try configurable parsing first using provider configuration
        plan = parseResponseWithRules(response);
        
        // If configurable parsing failed, fall back to hardcoded parsing
        if (!plan.isValid) {
            SLOG_WARNING().message("Configurable parsing failed, falling back to hardcoded parsing");
            plan = parseResponseFallback(response);
        }
        
    }, "LLMConnector::parseResponse");
    
    return plan;
}

ExecutionPlan LLMConnector::parseResponseWithRules(const nlohmann::json& response) {
    ExecutionPlan plan;
    
    try {
        // Load provider configuration to get parsing rules
        auto& config = ConfigManager::getInstance();
        std::string activeProvider = config.getActiveProvider();
        nlohmann::json providerConfig = config.loadProviderConfig(activeProvider);
        
        if (!providerConfig.contains("parsing_rules")) {
            SLOG_DEBUG().message("No parsing rules found in provider config, using fallback");
            return plan; // Invalid plan, will trigger fallback
        }
        
        const auto& parsingRules = providerConfig["parsing_rules"];
        
        // Extract content from response based on provider format
        std::string content = extractContentFromResponse(response);
        if (content.empty()) {
            SLOG_WARNING().message("Failed to extract content from LLM response");
            return plan;
        }
        
        SLOG_DEBUG().message("Extracted content from response")
            .context("content_preview", content.substr(0, 200) + "...");
        
        // Apply extraction patterns to get JSON content
        std::string jsonContent = applyExtractionPatterns(content, parsingRules);
        if (jsonContent.empty()) {
            SLOG_WARNING().message("Failed to extract JSON using configured patterns");
            return plan;
        }
        
        // Apply cleaning rules
        jsonContent = applyCleaningRules(jsonContent, parsingRules);
        
        // Parse JSON
        nlohmann::json parsedJson = nlohmann::json::parse(jsonContent);
        
        // Validate parsed content according to rules
        if (!validateParsedContent(parsedJson, parsingRules)) {
            SLOG_WARNING().message("Parsed content failed validation");
            return plan;
        }
        
        // Extract ExecutionPlan fields
        if (parsedJson.contains("reasoning") && parsedJson["reasoning"].is_string()) {
            plan.reasoning = parsedJson["reasoning"].get<std::string>();
        }
        if (parsedJson.contains("summary") && parsedJson["summary"].is_string()) {
            plan.summary = parsedJson["summary"].get<std::string>();
        }
        if (parsedJson.contains("commands") && parsedJson["commands"].is_array()) {
            plan.commands = parsedJson["commands"];
            plan.isValid = true;
        }
        
        SLOG_DEBUG().message("Successfully parsed response using configurable rules");
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Error in configurable response parsing")
            .context("error", e.what());
        plan.isValid = false;
    }
    
    return plan;
}

ExecutionPlan LLMConnector::parseResponseFallback(const nlohmann::json& response) {
    ExecutionPlan plan;
    
    // Try provider-specific parsing
    switch (m_provider) {
        case Provider::OPENAI:
        case Provider::AZURE_OPENAI:
            plan = parseOpenAIResponse(response);
            break;
        case Provider::ANTHROPIC:
            plan = parseAnthropicResponse(response);
            break;
        default:
            // Generic parsing
            if (response.contains("choices") && !response["choices"].empty()) {
                std::string content = response["choices"][0]["message"]["content"];
                
                // Try to extract JSON from the content
                std::regex jsonRegex(R"(\{[\s\S]*\})");
                std::smatch match;
                if (std::regex_search(content, match, jsonRegex)) {
                    try {
                        nlohmann::json planJson = nlohmann::json::parse(match.str());
                        
                        if (planJson.contains("reasoning")) {
                            plan.reasoning = planJson["reasoning"];
                        }
                        if (planJson.contains("summary")) {
                            plan.summary = planJson["summary"];
                        }
                        if (planJson.contains("commands")) {
                            plan.commands = planJson["commands"];
                            plan.isValid = true;
                        }
                    } catch (const std::exception& e) {
                        SLOG_ERROR().message("Fallback JSON parsing failed")
                            .context("error", e.what());
                    }
                }
            }
            break;
    }
    
    return plan;
}

std::string LLMConnector::extractContentFromResponse(const nlohmann::json& response) {
    std::string content;
    
    // Try different response formats
    if (response.contains("choices") && !response["choices"].empty()) {
        // OpenAI/OpenRouter format
        if (response["choices"][0].contains("message") && 
            response["choices"][0]["message"].contains("content")) {
            content = response["choices"][0]["message"]["content"].get<std::string>();
        }
    } else if (response.contains("content") && !response["content"].empty()) {
        // Anthropic format
        if (response["content"][0].contains("text")) {
            content = response["content"][0]["text"].get<std::string>();
        }
    } else if (response.contains("text")) {
        // Direct text format
        content = response["text"].get<std::string>();
    }
    
    return content;
}

std::string LLMConnector::applyExtractionPatterns(const std::string& content, const nlohmann::json& parsingRules) {
    if (!parsingRules.contains("extraction_patterns") || !parsingRules["extraction_patterns"].is_array()) {
        return content; // Return original content if no patterns
    }
    
    const auto& patterns = parsingRules["extraction_patterns"];
    
    for (const auto& patternConfig : patterns) {
        if (!patternConfig.contains("pattern") || !patternConfig["pattern"].is_string()) {
            continue;
        }
        
        std::string pattern = patternConfig["pattern"].get<std::string>();
        
        try {
            std::regex regex(pattern);
            std::smatch match;
            
            if (std::regex_search(content, match, regex)) {
                // Check if there's a specific group to extract
                if (patternConfig.contains("group") && patternConfig["group"].is_number()) {
                    int group = patternConfig["group"].get<int>();
                    if (group < static_cast<int>(match.size())) {
                        std::string extracted = match[group].str();
                        SLOG_DEBUG().message("Extracted content using pattern")
                            .context("pattern_name", patternConfig.value("name", "unnamed"));
                        return extracted;
                    }
                } else {
                    // Return the full match
                    std::string extracted = match.str();
                    SLOG_DEBUG().message("Extracted content using pattern").context("pattern_name", patternConfig.value("name", "unnamed"));
                    return extracted;
                }
            }
        } catch (const std::exception& e) {
            SLOG_WARNING().message("Invalid regex pattern")
                .context("pattern", pattern)
                .context("error", e.what());
        }
    }
    
    return content; // Return original if no patterns matched
}

std::string LLMConnector::applyCleaningRules(const std::string& content, const nlohmann::json& parsingRules) {
    std::string cleaned = content;
    
    if (!parsingRules.contains("cleaning_rules") || !parsingRules["cleaning_rules"].is_array()) {
        return cleaned;
    }
    
    const auto& cleaningRules = parsingRules["cleaning_rules"];
    
    for (const auto& rule : cleaningRules) {
        // Remove specific patterns (supports both literal strings and regex)
        if (rule.contains("remove_patterns") && rule["remove_patterns"].is_array()) {
            for (const auto& pattern : rule["remove_patterns"]) {
                if (pattern.is_string()) {
                    std::string patternStr = pattern.get<std::string>();
                    
                    // Check if it looks like a regex pattern (contains regex metacharacters)
                    if (patternStr.find_first_of(".*+?^$[]{}()\\|") != std::string::npos) {
                        try {
                            std::regex regexPattern(patternStr);
                            cleaned = std::regex_replace(cleaned, regexPattern, "");
                            SLOG_DEBUG().message("Applied regex pattern removal")
                                .context("pattern", patternStr);
                        } catch (const std::exception& e) {
                            SLOG_WARNING().message("Invalid regex pattern")
                                .context("pattern", patternStr)
                                .context("error", e.what());
                            // Fallback to literal string removal
                            size_t pos = 0;
                            while ((pos = cleaned.find(patternStr, pos)) != std::string::npos) {
                                cleaned.erase(pos, patternStr.length());
                            }
                        }
                    } else {
                        // Literal string removal
                        size_t pos = 0;
                        while ((pos = cleaned.find(patternStr, pos)) != std::string::npos) {
                            cleaned.erase(pos, patternStr.length());
                        }
                    }
                }
            }
        }
        
        // Apply text transformations
        if (rule.contains("trim_whitespace") && rule["trim_whitespace"].get<bool>()) {
            // Trim leading and trailing whitespace
            cleaned.erase(0, cleaned.find_first_not_of(" \t\n\r"));
            cleaned.erase(cleaned.find_last_not_of(" \t\n\r") + 1);
        }
        
        if (rule.contains("normalize_line_endings") && rule["normalize_line_endings"].get<bool>()) {
            // Normalize line endings to \n
            std::replace(cleaned.begin(), cleaned.end(), '\r', '\n');
            // Remove duplicate newlines
            std::regex doubleNewlines("\n\n+");
            cleaned = std::regex_replace(cleaned, doubleNewlines, "\n");
        }
        
        if (rule.contains("remove_empty_lines") && rule["remove_empty_lines"].get<bool>()) {
            std::regex emptyLines("\\n\\s*\\n");
            cleaned = std::regex_replace(cleaned, emptyLines, "\n");
        }
    }
    
    return cleaned;
}

bool LLMConnector::validateParsedContent(const nlohmann::json& parsedJson, const nlohmann::json& parsingRules) {
    if (!parsingRules.contains("validation")) {
        return true; // No validation rules, consider valid
    }
    
    const auto& validation = parsingRules["validation"];
    
    // Check if JSON is valid (already parsed, so this is true)
    if (validation.contains("require_valid_json") && validation["require_valid_json"].get<bool>()) {
        if (parsedJson.is_null()) {
            SLOG_WARNING().message("Validation failed: invalid JSON");
            return false;
        }
    }
    
    // Check if commands array exists
    if (validation.contains("require_commands_array") && validation["require_commands_array"].get<bool>()) {
        if (!parsedJson.contains("commands") || !parsedJson["commands"].is_array()) {
            SLOG_WARNING().message("Validation failed: missing or invalid commands array");
            return false;
        }
    }
    
    // Check if reasoning exists
    if (validation.contains("require_reasoning") && validation["require_reasoning"].get<bool>()) {
        if (!parsedJson.contains("reasoning") || !parsedJson["reasoning"].is_string()) {
            SLOG_WARNING().message("Validation failed: missing or invalid reasoning");
            return false;
        }
    }
    
    // Check maximum commands limit
    if (validation.contains("max_commands_per_response") && validation["max_commands_per_response"].is_number()) {
        int maxCommands = validation["max_commands_per_response"].get<int>();
        if (parsedJson.contains("commands") && parsedJson["commands"].is_array()) {
            if (static_cast<int>(parsedJson["commands"].size()) > maxCommands) {
                SLOG_WARNING().message("Validation failed: too many commands")
                    .context("command_count", parsedJson["commands"].size())
                    .context("max_allowed", maxCommands);
                return false;
            }
        }
    }
    
    // Check for forbidden commands
    if (validation.contains("forbidden_commands") && validation["forbidden_commands"].is_array()) {
        const auto& forbiddenCommands = validation["forbidden_commands"];
        if (parsedJson.contains("commands") && parsedJson["commands"].is_array()) {
            for (const auto& command : parsedJson["commands"]) {
                if (command.contains("command") && command["command"].is_string()) {
                    std::string commandName = command["command"].get<std::string>();
                    for (const auto& forbidden : forbiddenCommands) {
                        if (forbidden.is_string() && forbidden.get<std::string>() == commandName) {
                            SLOG_WARNING().message("Validation failed: forbidden command used")
                                .context("command", commandName);
                            return false;
                        }
                    }
                }
            }
        }
    }
    
    // Check for forbidden patterns in the original JSON content
    if (validation.contains("forbidden_patterns") && validation["forbidden_patterns"].is_array()) {
        std::string jsonStr = parsedJson.dump();
        const auto& forbiddenPatterns = validation["forbidden_patterns"];
        
        for (const auto& pattern : forbiddenPatterns) {
            if (pattern.is_string()) {
                std::string patternStr = pattern.get<std::string>();
                try {
                    std::regex regexPattern(patternStr);
                    if (std::regex_search(jsonStr, regexPattern)) {
                        SLOG_WARNING().message("Validation failed: forbidden pattern found")
                            .context("pattern", patternStr);
                        return false;
                    }
                } catch (const std::exception& e) {
                    SLOG_WARNING().message("Invalid forbidden pattern regex")
                        .context("pattern", patternStr);
                }
            }
        }
    }
    
    SLOG_DEBUG().message("Content validation passed");
    return true;
}

ExecutionPlan LLMConnector::parseOpenAIResponse(const nlohmann::json& response) {
    ExecutionPlan plan;
    
    if (response.contains("choices") && !response["choices"].empty()) {
        std::string content = response["choices"][0]["message"]["content"];
        
        try {
            std::string jsonContent = content;
            
            // Try to extract JSON from markdown code blocks first
            std::regex codeBlockRegex(R"(```(?:json)?\s*\n?([\s\S]*?)\n?```)");
            std::smatch match;
            if (std::regex_search(content, match, codeBlockRegex)) {
                jsonContent = match[1].str();
                SLOG_DEBUG().message("Extracted JSON from code block");
            }
            
            // Try to parse the JSON content
            nlohmann::json planJson = nlohmann::json::parse(jsonContent);
            
            if (planJson.contains("reasoning")) {
                plan.reasoning = planJson["reasoning"];
            }
            if (planJson.contains("summary")) {
                plan.summary = planJson["summary"];
            }
            if (planJson.contains("commands") && planJson["commands"].is_array()) {
                plan.commands = planJson["commands"];
                plan.isValid = true;
            }
        } catch (const std::exception& e) {
            SLOG_WARNING().message("Failed to parse LLM response as JSON")
                .context("error", e.what());
            SLOG_DEBUG().message("Raw LLM response content")
                .context("content", content);
            
            // Try extracting JSON with a more flexible regex as fallback
            std::regex jsonRegex(R"(\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\})");
            std::smatch jsonMatch;
            if (std::regex_search(content, jsonMatch, jsonRegex)) {
                try {
                    nlohmann::json fallbackJson = nlohmann::json::parse(jsonMatch.str());
                    if (fallbackJson.contains("commands")) {
                        plan.commands = fallbackJson["commands"];
                        plan.reasoning = fallbackJson.value("reasoning", "Extracted from partial response");
                        plan.summary = fallbackJson.value("summary", "Parsed with fallback method");
                        plan.isValid = true;
                        SLOG_INFO().message("Successfully parsed JSON using fallback regex");
                    }
                } catch (const std::exception& fallbackE) {
                    SLOG_ERROR().message("Fallback JSON parsing also failed")
                        .context("error", fallbackE.what());
                    plan.summary = "Failed to parse LLM response";
                }
            } else {
                plan.summary = "Failed to parse LLM response";
            }
        }
    }
    
    return plan;
}

ExecutionPlan LLMConnector::parseAnthropicResponse(const nlohmann::json& response) {
    ExecutionPlan plan;
    
    if (response.contains("content") && !response["content"].empty()) {
        std::string content = response["content"][0]["text"];
        
        try {
            nlohmann::json planJson = nlohmann::json::parse(content);
            
            if (planJson.contains("reasoning")) {
                plan.reasoning = planJson["reasoning"];
            }
            if (planJson.contains("summary")) {
                plan.summary = planJson["summary"];
            }
            if (planJson.contains("commands") && planJson["commands"].is_array()) {
                plan.commands = planJson["commands"];
                plan.isValid = true;
            }
        } catch (const std::exception& e) {
            SLOG_WARNING().message("Failed to parse Anthropic response as JSON")
                .context("error", e.what());
            plan.summary = "Failed to parse LLM response";
        }
    }
    
    return plan;
}

nlohmann::json LLMConnector::createOpenAIRequest(const std::vector<LLMMessage>& messages) {
    nlohmann::json request;
    
    request["model"] = m_modelName;
    request["temperature"] = m_temperature;
    request["max_tokens"] = m_maxTokens;
    
    nlohmann::json jsonMessages = nlohmann::json::array();
    for (const auto& msg : messages) {
        jsonMessages.push_back({
            {"role", msg.role},
            {"content", msg.content}
        });
    }
    request["messages"] = jsonMessages;
    
    return request;
}

nlohmann::json LLMConnector::createAnthropicRequest(const std::vector<LLMMessage>& messages) {
    nlohmann::json request;
    
    request["model"] = m_modelName;
    request["max_tokens"] = m_maxTokens;
    request["temperature"] = m_temperature;
    
    // Anthropic format is different - separate system message
    std::string systemMessage;
    nlohmann::json jsonMessages = nlohmann::json::array();
    
    for (const auto& msg : messages) {
        if (msg.role == "system") {
            systemMessage = msg.content;
        } else {
            jsonMessages.push_back({
                {"role", msg.role},
                {"content", msg.content}
            });
        }
    }
    
    if (!systemMessage.empty()) {
        request["system"] = systemMessage;
    }
    request["messages"] = jsonMessages;
    
    return request;
}

ExecutionPlan LLMConnector::simulateLLMResponse(const std::string& request) {
    (void)request; // TODO: Use request to generate context-aware simulation
    ExecutionPlan plan;
    plan.reasoning = "Simulated response for development/testing";
    plan.summary = "This is a simulated execution plan";
    plan.isValid = true;
    
    // Create a simple example command
    nlohmann::json command = {
        {"command", "system.sleep"},
        {"params", {{"ms", 1000}}}
    };
    plan.commands.push_back(command);
    
    SLOG_DEBUG().message("Using simulated LLM response");
    return plan;
}

// Configuration methods
void LLMConnector::setApiKey(const std::string& apiKey) { m_apiKey = apiKey; }
void LLMConnector::setBaseUrl(const std::string& baseUrl) { m_baseUrl = baseUrl; }
void LLMConnector::setModelName(const std::string& modelName) { m_modelName = modelName; }
void LLMConnector::setTimeout(int timeoutMs) { 
    m_timeoutMs = timeoutMs; 
    m_httpClient->setTimeout(timeoutMs);
}
void LLMConnector::setMaxRetries(int maxRetries) {
    // Validate max retries is non-negative
    if (maxRetries < 0) {
        SLOG_ERROR().message("Invalid max retries value")
            .context("max_retries", maxRetries);
        return;
    }
    
    // Reasonable maximum
    const int MAX_RETRIES = 10;
    if (maxRetries > MAX_RETRIES) {
        SLOG_WARNING().message("Max retries exceeds recommended maximum")
            .context("max_retries", maxRetries)
            .context("recommended_max", MAX_RETRIES);
    }
    
    m_maxRetries = maxRetries;
    m_httpClient->setMaxRetries(maxRetries);
    SLOG_DEBUG().message("Max retries set")
        .context("max_retries", maxRetries);
}
void LLMConnector::setTemperature(double temperature) { m_temperature = temperature; }
void LLMConnector::setMaxTokens(int maxTokens) { m_maxTokens = maxTokens; }
void LLMConnector::setProvider(Provider provider) { m_provider = provider; }
void LLMConnector::setCustomHeaders(const std::map<std::string, std::string>& headers) { m_customHeaders = headers; }

// Context and history management
void LLMConnector::updateContext(const LLMContext& context) { m_context = context; }
void LLMConnector::addToHistory(const LLMMessage& message) {
    m_messageHistory.push_back(message);
    if (m_messageHistory.size() > m_maxHistorySize) {
        m_messageHistory.erase(m_messageHistory.begin());
    }
}
void LLMConnector::clearHistory() { m_messageHistory.clear(); }
std::vector<LLMMessage> LLMConnector::getHistory() const { return m_messageHistory; }

// Utility methods
std::string LLMConnector::getApiEndpoint() const {
    switch (m_provider) {
        case Provider::OPENAI:
            return m_baseUrl + "/chat/completions";
        case Provider::ANTHROPIC:
            return m_baseUrl + "/v1/messages";
        case Provider::AZURE_OPENAI:
            return m_baseUrl + "/openai/deployments/" + m_modelName + "/chat/completions?api-version=2023-05-15";
        case Provider::CUSTOM:
        default:
            return m_baseUrl;
    }
}

std::map<std::string, std::string> LLMConnector::getRequestHeaders() const {
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"}
    };
    
    switch (m_provider) {
        case Provider::OPENAI:
            headers["Authorization"] = "Bearer " + m_apiKey;
            break;
        case Provider::ANTHROPIC:
            headers["x-api-key"] = m_apiKey;
            headers["anthropic-version"] = "2023-06-01";
            break;
        case Provider::AZURE_OPENAI:
            headers["api-key"] = m_apiKey;
            break;
        case Provider::CUSTOM:
            // Add custom headers
            for (const auto& header : m_customHeaders) {
                headers[header.first] = header.second;
            }
            break;
    }
    
    return headers;
}

// Rate limiting and validation
bool LLMConnector::checkRateLimit() {
    cleanupOldRequests();
    return m_requestTimes.size() < static_cast<size_t>(m_requestsPerMinute);
}

void LLMConnector::cleanupOldRequests() {
    auto now = std::chrono::system_clock::now();
    auto oneMinuteAgo = now - std::chrono::minutes(1);
    
    m_requestTimes.erase(
        std::remove_if(m_requestTimes.begin(), m_requestTimes.end(),
                      [oneMinuteAgo](const auto& time) { return time < oneMinuteAgo; }),
        m_requestTimes.end()
    );
}

void LLMConnector::addRequestTime() {
    m_requestTimes.push_back(std::chrono::system_clock::now());
}

ExecutionPlan LLMConnector::validatePlan(const ExecutionPlan& plan) {
    ExecutionPlan validatedPlan = plan;
    
    if (!plan.isValid) {
        return validatedPlan;
    }
    
    // Validate each command
    for (const auto& command : plan.commands) {
        if (!isValidCommand(command)) {
            validatedPlan.isValid = false;
            SLOG_WARNING().message("Invalid command in execution plan")
                .context("command", command);
            break;
        }
    }
    
    return validatedPlan;
}

bool LLMConnector::isValidCommand(const nlohmann::json& command) {
    if (!command.contains("command") || !command["command"].is_string()) {
        return false;
    }
    
    // Accept both "params" and "parameters" for compatibility
    bool hasParams = (command.contains("params") && command["params"].is_object()) ||
                     (command.contains("parameters") && command["parameters"].is_object());
    
    if (!hasParams) {
        SLOG_DEBUG().message("Command missing parameters")
            .context("command", command);
        return false;
    }
    
    return hasRequiredFields(command);
}

bool LLMConnector::hasRequiredFields(const nlohmann::json& command) {
    (void)command; // TODO: Implement command-specific field validation
    // Basic validation - could be expanded based on specific command requirements
    return true; // For now, assume valid if basic structure is correct
}

// Error handling
void LLMConnector::handleHttpError(const HttpResponse& response) {
    std::string errorType = "HTTP_ERROR";
    bool retryable = false;
    
    if (response.statusCode == 429) {
        errorType = "RATE_LIMIT";
        retryable = true;
    } else if (response.statusCode == 401 || response.statusCode == 403) {
        errorType = "AUTHENTICATION";
        retryable = false;
    } else if (response.statusCode >= 500) {
        errorType = "SERVER_ERROR";
        retryable = true;
    }
    
    setError(response.statusCode, response.errorMessage, errorType, retryable);
}

void LLMConnector::setError(int code, const std::string& message, const std::string& type, bool retryable) {
    m_lastError.code = code;
    m_lastError.message = message;
    m_lastError.type = type;
    m_lastError.isRetryable = retryable;
}

LLMConnector::LLMError LLMConnector::getLastError() const { return m_lastError; }

// Simple implementations for remaining methods
nlohmann::json LLMConnector::createRequestPayload(const std::vector<LLMMessage>& messages) { return createOpenAIRequest(messages); }
bool LLMConnector::validateConfiguration() { return !m_apiKey.empty() && !m_baseUrl.empty(); }
bool LLMConnector::testConnection() { return validateConfiguration(); }
void LLMConnector::setRateLimit(int requestsPerMinute) { m_requestsPerMinute = requestsPerMinute; }
void LLMConnector::updateUsageStats(int tokensUsed) { m_totalTokensUsed += tokensUsed; }

// Vision capability methods
void LLMConnector::setVisionCapabilities(const VisionCapabilities& capabilities) {
    m_visionCapabilities = capabilities;
    SLOG_INFO().message("LLM vision capabilities updated")
        .context("vision_support", capabilities.supportsVision ? "enabled" : "disabled");
}

LLMConnector::VisionCapabilities LLMConnector::getVisionCapabilities() const {
    return m_visionCapabilities;
}

bool LLMConnector::supportsVision() const {
    return m_visionCapabilities.supportsVision;
}

// Dual-mode LLM interface implementation
ExecutionPlan LLMConnector::generatePlanWithContext(const std::string& userRequest, const LLMContext& context) {
    ExecutionPlan plan;
    
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Generating plan with contextual data")
            .context("vision_support", supportsVision() ? "enabled" : "disabled");
        
        std::vector<LLMMessage> messages;
        
        // Build system prompt based on capabilities
        std::string systemPrompt = buildSystemPrompt(context);
        messages.emplace_back("system", systemPrompt);
        
        // Build user message with appropriate context format
        std::string userMessage = buildContextualPrompt(userRequest, context);
        
        if (supportsVision() && context.hasScreenshot()) {
            // Vision-capable LLM: Send screenshot + structured data
            SLOG_DEBUG().message("Sending visual data to vision-capable LLM");
            messages.emplace_back("user", userMessage, context.screenshotData, context.screenshotFormat);
        } else {
            // Text-only LLM: Send comprehensive text description
            SLOG_DEBUG().message("Sending text description to text-only LLM");
            messages.emplace_back("user", userMessage);
        }
        
        // Send request to LLM
        nlohmann::json response = sendMessage(messages);
        
        // Parse response into execution plan
        plan = parseResponse(response);
        
        // Validate the plan
        plan = validatePlan(plan);
        
        // Add to history
        addToHistory(LLMMessage("user", userRequest));
        if (plan.isValid) {
            addToHistory(LLMMessage("assistant", plan.summary));
        }
        
        SLOG_DEBUG().message("Plan generation completed")
            .context("valid", plan.isValid);
        
        return plan;
        
    }, "LLMConnector::generatePlanWithContext");
    
    return plan;
}

std::string LLMConnector::buildContextualPrompt(const std::string& userRequest, const LLMContext& context) {
    // Try to load the appropriate template based on vision capabilities
    std::string templateContent;
    
    if (supportsVision() && context.hasScreenshot()) {
        templateContent = loadVisionPromptTemplate();
    } else {
        templateContent = loadTextPromptTemplate();
    }
    
    if (templateContent.empty()) {
        // No fallback - configuration should be complete
        SLOG_ERROR().message("Contextual prompt template not found in provider configuration");
        throw std::runtime_error("Contextual prompt template missing from provider configuration. Please check config/llm_providers/[provider].json");
    }
    
    // Replace variables in template
    std::string prompt = substituteContextualVariables(templateContent, userRequest, context);
    
    SLOG_DEBUG().message("Using configurable contextual prompt template");
    return prompt;
}

std::string LLMConnector::loadVisionPromptTemplate() {
    auto& config = ConfigManager::getInstance();
    std::string activeProvider = config.getActiveProvider();
    nlohmann::json providerConfig = config.loadProviderConfig(activeProvider);
    
    if (providerConfig.contains("prompt_templates") && 
        providerConfig["prompt_templates"].is_object() &&
        providerConfig["prompt_templates"].contains("vision_prompt") &&
        providerConfig["prompt_templates"]["vision_prompt"].is_object() &&
        providerConfig["prompt_templates"]["vision_prompt"].contains("template") &&
        providerConfig["prompt_templates"]["vision_prompt"]["template"].is_string()) {
        
        return providerConfig["prompt_templates"]["vision_prompt"]["template"].get<std::string>();
    }
    
    return ""; // Empty string indicates no template found
}

std::string LLMConnector::loadTextPromptTemplate() {
    auto& config = ConfigManager::getInstance();
    std::string activeProvider = config.getActiveProvider();
    nlohmann::json providerConfig = config.loadProviderConfig(activeProvider);
    
    if (providerConfig.contains("prompt_templates") && 
        providerConfig["prompt_templates"].is_object() &&
        providerConfig["prompt_templates"].contains("text_prompt") &&
        providerConfig["prompt_templates"]["text_prompt"].is_object() &&
        providerConfig["prompt_templates"]["text_prompt"].contains("template") &&
        providerConfig["prompt_templates"]["text_prompt"]["template"].is_string()) {
        
        return providerConfig["prompt_templates"]["text_prompt"]["template"].get<std::string>();
    }
    
    return ""; // Empty string indicates no template found
}

std::string LLMConnector::substituteContextualVariables(const std::string& templateContent, const std::string& userRequest, const LLMContext& context) {
    std::string result = templateContent;
    
    // Replace user request
    result = replaceAll(result, "{{USER_REQUEST}}", userRequest);
    
    // Replace system context
    std::string systemContext = generateContextInfo(context);
    result = replaceAll(result, "{{SYSTEM_CONTEXT}}", systemContext);
    
    // Replace screen description for text-only models
    std::string screenDescription = context.textDescription.empty() ? generateContextInfo(context) : context.textDescription;
    result = replaceAll(result, "{{SCREEN_DESCRIPTION}}", screenDescription);
    
    // Replace structured context data
    std::string structuredContext = generateStructuredContext(context);
    result = replaceAll(result, "{{STRUCTURED_CONTEXT}}", structuredContext);
    
    return result;
}

std::string LLMConnector::generateStructuredContext(const LLMContext& context) {
    std::ostringstream structured;
    
    if (!context.structuredData.empty()) {
        if (context.structuredData.contains("screen_resolution")) {
            auto res = context.structuredData["screen_resolution"];
            structured << "Screen Resolution: " << res["width"] << "x" << res["height"] << "\n";
        }
        if (context.structuredData.contains("activeWindow")) {
            structured << "Active Window: " << context.structuredData["activeWindow"].get<std::string>() << "\n";
        }
    }
    
    return structured.str();
}

// Removed hardcoded fallback - system now uses configurable templates only

std::string LLMConnector::encodeImageAsBase64(const std::vector<uint8_t>& imageData, const std::string& format) {
    // Basic base64 encoding implementation
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    size_t i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    for (size_t idx = 0; idx < imageData.size(); idx++) {
        char_array_3[i++] = imageData[idx];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                result += chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (size_t j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (size_t j = 0; j < i + 1; j++) {
            result += chars[char_array_4[j]];
        }
        
        while (i++ < 3) {
            result += '=';
        }
    }
    
    return "data:image/" + format + ";base64," + result;
}

void LLMConnector::initializeVisionCapabilities(const nlohmann::json& providerConfig) {
    // Initialize default capabilities (text-only)
    m_visionCapabilities.supportsVision = false;
    m_visionCapabilities.supportedImageFormats.clear();
    m_visionCapabilities.maxImageSize = 0;
    m_visionCapabilities.maxContextLength = 4096;
    m_visionCapabilities.preferredInputMode = "text";
    
    // Load vision capabilities from provider configuration first
    if (!providerConfig.empty() && 
        providerConfig.contains("model_configuration") && 
        providerConfig["model_configuration"].is_object() &&
        providerConfig["model_configuration"].contains("vision_capabilities") &&
        providerConfig["model_configuration"]["vision_capabilities"].is_object()) {
        
        const auto& visionConfig = providerConfig["model_configuration"]["vision_capabilities"];
        
        // Safely get supports_vision
        if (visionConfig.contains("supports_vision") && visionConfig["supports_vision"].is_boolean()) {
            m_visionCapabilities.supportsVision = visionConfig["supports_vision"].get<bool>();
        } else {
            m_visionCapabilities.supportsVision = false;
        }
        
        // Safely get supported image formats
        if (visionConfig.contains("supported_image_formats") && visionConfig["supported_image_formats"].is_array()) {
            m_visionCapabilities.supportedImageFormats = visionConfig["supported_image_formats"].get<std::vector<std::string>>();
        }
        
        // Safely get numeric values
        if (visionConfig.contains("max_image_size") && visionConfig["max_image_size"].is_number()) {
            m_visionCapabilities.maxImageSize = visionConfig["max_image_size"].get<size_t>();
        } else {
            m_visionCapabilities.maxImageSize = 0;
        }
        
        if (visionConfig.contains("max_context_length") && visionConfig["max_context_length"].is_number()) {
            m_visionCapabilities.maxContextLength = visionConfig["max_context_length"].get<size_t>();
        } else {
            m_visionCapabilities.maxContextLength = 4096;
        }
        
        if (visionConfig.contains("preferred_input_mode") && visionConfig["preferred_input_mode"].is_string()) {
            m_visionCapabilities.preferredInputMode = visionConfig["preferred_input_mode"].get<std::string>();
        } else {
            m_visionCapabilities.preferredInputMode = "text";
        }
        
        SLOG_INFO().message("Vision capabilities loaded from provider configuration")
            .context("vision_support", m_visionCapabilities.supportsVision ? "enabled" : "disabled");
    } else {
        // Fallback to model name detection if provider config doesn't specify capabilities
        SLOG_DEBUG().message("No provider vision config found, using model name detection");
        
        std::string lowerModelName = m_modelName;
        std::transform(lowerModelName.begin(), lowerModelName.end(), lowerModelName.begin(), ::tolower);
        
        // GPT-4 Vision models
        if (lowerModelName.find("gpt-4") != std::string::npos && 
            (lowerModelName.find("vision") != std::string::npos || 
             lowerModelName.find("turbo") != std::string::npos ||
             lowerModelName.find("gpt-4o") != std::string::npos)) {
            
            m_visionCapabilities.supportsVision = true;
            m_visionCapabilities.supportedImageFormats = {"png", "jpeg", "webp", "gif"};
            m_visionCapabilities.maxImageSize = 20971520; // 20MB
            m_visionCapabilities.maxContextLength = 128000;
            m_visionCapabilities.preferredInputMode = "hybrid";
            
            SLOG_INFO().message("Vision capabilities auto-detected for GPT-4 Vision model");
        }
        // Claude-3 with vision
        else if (lowerModelName.find("claude-3") != std::string::npos) {
            m_visionCapabilities.supportsVision = true;
            m_visionCapabilities.supportedImageFormats = {"png", "jpeg", "webp", "gif"};
            m_visionCapabilities.maxImageSize = 5242880; // 5MB
            m_visionCapabilities.maxContextLength = 200000;
            m_visionCapabilities.preferredInputMode = "hybrid";
            
            SLOG_INFO().message("Vision capabilities auto-detected for Claude-3 model");
        }
        // Gemini models (Flash, Pro, etc.)
        else if (lowerModelName.find("gemini") != std::string::npos) {
            m_visionCapabilities.supportsVision = true;
            m_visionCapabilities.supportedImageFormats = {"png", "jpeg", "webp", "gif"};
            
            if (lowerModelName.find("flash") != std::string::npos) {
                m_visionCapabilities.maxImageSize = 10485760; // 10MB for Gemini Flash
                m_visionCapabilities.maxContextLength = 1000000; // 1M tokens
                SLOG_INFO().message("Vision capabilities auto-detected for Gemini Flash model");
            } else if (lowerModelName.find("pro") != std::string::npos) {
                m_visionCapabilities.maxImageSize = 20971520; // 20MB for Gemini Pro
                m_visionCapabilities.maxContextLength = 2000000; // 2M tokens
                SLOG_INFO().message("Vision capabilities auto-detected for Gemini Pro model");
            } else {
                m_visionCapabilities.maxImageSize = 4194304; // 4MB default
                m_visionCapabilities.maxContextLength = 32768; // Default context
                SLOG_INFO().message("Vision capabilities auto-detected for Gemini model");
            }
            
            m_visionCapabilities.preferredInputMode = "hybrid";
        }
        // Text-only models (Llama, Mixtral, etc.)
        else {
            SLOG_INFO().message("Text-only model detected - Vision capabilities disabled");
        }
    }
    
    SLOG_DEBUG().message("Vision capabilities initialized")
        .context("vision_support", m_visionCapabilities.supportsVision ? "enabled" : "disabled")
        .context("max_context", m_visionCapabilities.maxContextLength)
        .context("format_count", m_visionCapabilities.supportedImageFormats.size());
}