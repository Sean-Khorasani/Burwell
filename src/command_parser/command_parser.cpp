#include "command_parser.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/input_validator.h"
#include "../task_engine/task_engine.h"
#include "../llm_connector/llm_connector.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cctype>

using namespace burwell;

struct CommandParser::CommandParserImpl {
    // Placeholder for future implementation details
    std::string placeholder;
};

CommandParser::CommandParser()
    : m_impl(std::make_unique<CommandParserImpl>())
    , m_strictMode(false)
    , m_confidenceThreshold(ConfidenceLevel::MEDIUM)
    , m_learningEnabled(true) {
    
    initializePatterns();
    m_statistics = {}; // Initialize statistics
    
    SLOG_INFO().message("CommandParser initialized with NLP capabilities");
}

CommandParser::~CommandParser() = default;

CommandParseResult CommandParser::parseUserInput(const std::string& userInput) {
    CommandParseResult result;
    
    // Validate input first
    if (!InputValidator::isNotEmpty(userInput)) {
        result.success = false;
        result.errorMessage = "Empty or whitespace-only input";
        return result;
    }
    
    // Check for potential injection attacks
    auto validationResult = InputValidator::validateCommand(userInput);
    if (!validationResult.isValid) {
        result.success = false;
        result.errorMessage = validationResult.errorMessage;
        SLOG_WARNING().message("Rejected invalid input")
            .context("error", validationResult.errorMessage);
        return result;
    }
    
    BURWELL_TRY_CATCH({
        std::string normalizedInput = normalizeInput(userInput);
        SLOG_DEBUG().message("Parsing user input")
            .context("input", normalizedInput);
        
        // Step 1: Analyze intent
        result.intent = analyzeIntent(normalizedInput);
        
        // Step 2: Try pattern matching first
        result.commands = applyPatternMatching(normalizedInput, result.intent.type);
        
        // Step 3: If pattern matching fails or confidence is low, use LLM fallback
        if (result.commands.empty() || shouldUseLLMFallback(normalizedInput, result)) {
            if (m_llmConnector) {
                std::string nlpPrompt = buildNLPPrompt(normalizedInput);
                auto llmResponse = m_llmConnector->sendPrompt(nlpPrompt);
                auto llmResult = parseLLMResponse(llmResponse);
                
                if (llmResult.success && !llmResult.commands.empty()) {
                    result.commands = llmResult.commands;
                    result.intent.confidence = ConfidenceLevel::HIGH;
                }
            }
        }
        
        // Step 4: Validate commands
        bool allValid = true;
        for (const auto& command : result.commands) {
            if (!validateCommand(command)) {
                allValid = false;
                break;
            }
        }
        
        result.success = allValid && !result.commands.empty();
        
        // Step 5: Generate suggested task name if this could be saved
        if (result.success && shouldCreateNewTask(result)) {
            result.suggestedTaskName = generateTaskName(normalizedInput);
        }
        
        // Step 6: Extract additional context
        result.context = extractEntities(normalizedInput);
        
        updateStatistics(result);
        logParsingAttempt(normalizedInput, result);
        
    }, "CommandParser::parseUserInput");
    
    return result;
}

CommandParseResult CommandParser::parseLLMPlan(const nlohmann::json& llmPlan) {
    CommandParseResult result;
    
    BURWELL_TRY_CATCH({
        if (!llmPlan.contains("commands") || !llmPlan["commands"].is_array()) {
            result.errorMessage = "Invalid LLM plan format: missing commands array";
            return result;
        }
        
        // Parse each command in the plan
        for (const auto& cmdJson : llmPlan["commands"]) {
            ParsedCommand command;
            
            if (cmdJson.contains("command") && cmdJson["command"].is_string()) {
                command.action = cmdJson["command"];
            }
            
            if (cmdJson.contains("params") && cmdJson["params"].is_object()) {
                command.parameters = cmdJson["params"];
            }
            
            if (cmdJson.contains("description")) {
                command.description = cmdJson["description"];
            }
            
            if (cmdJson.contains("optional")) {
                command.isOptional = cmdJson["optional"];
            }
            
            if (cmdJson.contains("delayAfterMs")) {
                command.delayAfterMs = cmdJson["delayAfterMs"];
            }
            
            if (validateCommand(command)) {
                result.commands.push_back(command);
            } else {
                SLOG_WARNING().message("Invalid command in LLM plan")
                    .context("action", command.action);
            }
        }
        
        result.success = !result.commands.empty();
        
        // Extract metadata if available
        if (llmPlan.contains("reasoning")) {
            result.context["reasoning"] = llmPlan["reasoning"];
        }
        
        if (llmPlan.contains("summary")) {
            result.context["summary"] = llmPlan["summary"];
        }
        
    }, "CommandParser::parseLLMPlan");
    
    return result;
}

IntentAnalysis CommandParser::analyzeIntent(const std::string& userInput) {
    IntentAnalysis analysis;
    
    // Classify intent
    analysis.type = classifyIntent(userInput);
    analysis.confidence = calculateConfidence(userInput, analysis.type);
    
    // Extract entities and keywords
    analysis.entities = extractEntities(userInput);
    analysis.keywords = extractActionVerbs(userInput);
    
    // Set description based on intent type
    switch (analysis.type) {
        case IntentType::AUTOMATION:
            analysis.description = "User wants to automate a task or process";
            break;
        case IntentType::QUERY:
            analysis.description = "User is asking a question or requesting information";
            break;
        case IntentType::SYSTEM:
            analysis.description = "User wants to control system settings or processes";
            break;
        case IntentType::HELP:
            analysis.description = "User needs help or guidance";
            break;
        case IntentType::TASK_MANAGEMENT:
            analysis.description = "User wants to manage existing tasks";
            break;
        default:
            analysis.description = "Intent could not be determined";
            break;
    }
    
    return analysis;
}

IntentType CommandParser::classifyIntent(const std::string& input) {
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    if (containsAutomationKeywords(lowerInput)) {
        return IntentType::AUTOMATION;
    } else if (containsQueryKeywords(lowerInput)) {
        return IntentType::QUERY;
    } else if (containsSystemKeywords(lowerInput)) {
        return IntentType::SYSTEM;
    } else if (containsHelpKeywords(lowerInput)) {
        return IntentType::HELP;
    } else if (lowerInput.find("task") != std::string::npos && 
               (lowerInput.find("list") != std::string::npos || 
                lowerInput.find("show") != std::string::npos ||
                lowerInput.find("delete") != std::string::npos)) {
        return IntentType::TASK_MANAGEMENT;
    }
    
    return IntentType::UNKNOWN;
}

ConfidenceLevel CommandParser::calculateConfidence(const std::string& input, IntentType intent) {
    (void)intent; // TODO: Use intent type in confidence calculation
    int confidenceScore = 0;
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    // Check for specific keywords that increase confidence
    std::vector<std::string> strongKeywords = {
        "click", "type", "open", "close", "run", "execute", "automate",
        "move", "drag", "copy", "paste", "save", "load", "create"
    };
    
    for (const auto& keyword : strongKeywords) {
        if (lowerInput.find(keyword) != std::string::npos) {
            confidenceScore += 2;
        }
    }
    
    // Check for entities (applications, files, coordinates)
    auto entities = extractEntities(input);
    confidenceScore += entities.size();
    
    // Check for quoted strings (often indicate specific text to type)
    if (input.find('"') != std::string::npos) {
        confidenceScore += 1;
    }
    
    // Convert score to confidence level
    if (confidenceScore >= 5) {
        return ConfidenceLevel::HIGH;
    } else if (confidenceScore >= 3) {
        return ConfidenceLevel::MEDIUM;
    } else if (confidenceScore >= 1) {
        return ConfidenceLevel::LOW;
    }
    
    return ConfidenceLevel::NONE;
}

std::map<std::string, std::string> CommandParser::extractEntities(const std::string& input) {
    std::map<std::string, std::string> entities;
    
    // Extract application names
    auto apps = extractApplicationNames(input);
    if (!apps.empty()) {
        entities["applications"] = apps[0]; // Take the first one
    }
    
    // Extract file names
    auto files = extractFileNames(input);
    if (!files.empty()) {
        entities["files"] = files[0]; // Take the first one
    }
    
    // Extract coordinates using regex
    std::regex coordRegex(R"(\b(\d+)\s*,\s*(\d+)\b)");
    std::smatch match;
    if (std::regex_search(input, match, coordRegex)) {
        entities["x"] = match[1];
        entities["y"] = match[2];
    }
    
    // Extract quoted strings (text to type)
    std::regex quotedRegex("\"([^\"]+)\"");
    std::sregex_iterator iter(input.begin(), input.end(), quotedRegex);
    std::sregex_iterator end;
    if (iter != end) {
        entities["text"] = (*iter)[1];
    }
    
    return entities;
}

std::vector<std::string> CommandParser::extractApplicationNames(const std::string& input) {
    std::vector<std::string> apps;
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    // Common application names
    std::vector<std::string> commonApps = {
        "notepad", "calculator", "chrome", "firefox", "edge", "explorer",
        "word", "excel", "powerpoint", "outlook", "teams", "slack",
        "vscode", "visual studio", "photoshop", "gimp", "vlc", "spotify"
    };
    
    for (const auto& app : commonApps) {
        if (lowerInput.find(app) != std::string::npos) {
            apps.push_back(app);
        }
    }
    
    return apps;
}

std::vector<std::string> CommandParser::extractFileNames(const std::string& input) {
    std::vector<std::string> files;
    
    // Look for file extensions
    std::regex fileRegex(R"(\b\w+\.(txt|doc|docx|pdf|jpg|jpeg|png|gif|mp3|mp4|avi|mkv|zip|rar|exe|bat|ps1)\b)");
    std::sregex_iterator iter(input.begin(), input.end(), fileRegex);
    std::sregex_iterator end;
    
    while (iter != end) {
        files.push_back((*iter)[0]);
        ++iter;
    }
    
    return files;
}

std::vector<std::string> CommandParser::extractActionVerbs(const std::string& input) {
    std::vector<std::string> verbs;
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    std::vector<std::string> actionVerbs = {
        "click", "type", "open", "close", "run", "execute", "launch",
        "move", "drag", "copy", "paste", "cut", "save", "load", "create",
        "delete", "remove", "install", "uninstall", "download", "upload",
        "resize", "minimize", "maximize", "focus", "switch", "find", "search"
    };
    
    for (const auto& verb : actionVerbs) {
        if (lowerInput.find(verb) != std::string::npos) {
            verbs.push_back(verb);
        }
    }
    
    return verbs;
}

bool CommandParser::validateCommand(const ParsedCommand& command) {
    return isValidAction(command.action) && 
           hasRequiredParameters(command.action, command.parameters);
}

std::vector<std::string> CommandParser::getValidationErrors(const ParsedCommand& command) {
    std::vector<std::string> errors;
    
    if (command.action.empty()) {
        errors.push_back("Command action cannot be empty");
    }
    
    if (!isValidAction(command.action)) {
        errors.push_back("Unknown command action: " + command.action);
    }
    
    auto requiredParams = getRequiredParameters(command.action);
    for (const auto& param : requiredParams) {
        if (!command.parameters.contains(param)) {
            errors.push_back("Missing required parameter: " + param);
        }
    }
    
    return errors;
}

std::vector<ParsedCommand> CommandParser::matchAutomationPatterns(const std::string& input) {
    std::vector<ParsedCommand> commands;
    
    // Try different pattern matching approaches
    auto fileCommands = parseFileOperationCommand(input);
    if (!fileCommands.empty()) {
        commands.insert(commands.end(), fileCommands.begin(), fileCommands.end());
    }
    
    auto appCommands = parseApplicationCommand(input);
    if (!appCommands.empty()) {
        commands.insert(commands.end(), appCommands.begin(), appCommands.end());
    }
    
    auto uiCommands = parseUIInteractionCommand(input);
    if (!uiCommands.empty()) {
        commands.insert(commands.end(), uiCommands.begin(), uiCommands.end());
    }
    
    auto sysCommands = parseSystemCommand(input);
    if (!sysCommands.empty()) {
        commands.insert(commands.end(), sysCommands.begin(), sysCommands.end());
    }
    
    return commands;
}

std::vector<ParsedCommand> CommandParser::parseUIInteractionCommand(const std::string& input) {
    std::vector<ParsedCommand> commands;
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    // Extract coordinates
    std::regex coordRegex(R"(\b(\d+)\s*,\s*(\d+)\b)");
    std::smatch coordMatch;
    
    // Extract quoted text
    std::regex quotedRegex("\"([^\"]+)\"");
    std::smatch textMatch;
    
    if (lowerInput.find("click") != std::string::npos && 
        std::regex_search(input, coordMatch, coordRegex)) {
        
        int x = std::stoi(coordMatch[1]);
        int y = std::stoi(coordMatch[2]);
        commands.push_back(createMouseClickCommand(x, y));
        
    } else if (lowerInput.find("type") != std::string::npos && 
               std::regex_search(input, textMatch, quotedRegex)) {
        
        std::string text = textMatch[1];
        commands.push_back(createKeyboardCommand(text));
        
    } else if (lowerInput.find("press") != std::string::npos) {
        // Handle keyboard shortcuts
        if (lowerInput.find("ctrl") != std::string::npos && lowerInput.find("c") != std::string::npos) {
            ParsedCommand cmd;
            cmd.action = "keyboard.hotkey";
            cmd.parameters = nlohmann::json{{"keys", nlohmann::json::array({"ctrl", "c"})}};
            cmd.description = "Press Ctrl+C";
            commands.push_back(cmd);
        }
    }
    
    return commands;
}

std::vector<ParsedCommand> CommandParser::parseApplicationCommand(const std::string& input) {
    std::vector<ParsedCommand> commands;
    auto apps = extractApplicationNames(input);
    
    if (!apps.empty()) {
        std::string app = apps[0];
        std::string lowerInput = input;
        std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
        
        if (lowerInput.find("open") != std::string::npos || lowerInput.find("launch") != std::string::npos) {
            commands.push_back(createApplicationCommand(app, "launch"));
        } else if (lowerInput.find("close") != std::string::npos) {
            commands.push_back(createApplicationCommand(app, "close"));
        }
    }
    
    return commands;
}

// Implementation methods continue with proper integration
void CommandParser::initializePatterns() {
    // Initialize regex patterns for different intent types
    m_automationPatterns = {
        std::regex(R"(\b(click|type|open|close|run|execute)\b)", std::regex_constants::icase),
        std::regex(R"(\b(automate|script|macro|task)\b)", std::regex_constants::icase)
    };
    
    m_queryPatterns = {
        std::regex(R"(\b(what|where|when|why|how|which)\b)", std::regex_constants::icase),
        std::regex(R"(\b(show|list|display|find)\b)", std::regex_constants::icase)
    };
    
    m_systemPatterns = {
        std::regex(R"(\b(shutdown|restart|sleep|hibernate)\b)", std::regex_constants::icase),
        std::regex(R"(\b(volume|brightness|wifi|bluetooth)\b)", std::regex_constants::icase)
    };
    
    // Initialize entity extraction patterns
    m_fileNamePattern = std::regex(R"(\b\w+\.\w+\b)");
    m_applicationNamePattern = std::regex(R"(\b(notepad|calculator|chrome|firefox)\b)", std::regex_constants::icase);
    m_coordinatePattern = std::regex(R"(\b(\d+)\s*,\s*(\d+)\b)");
}

// Helper method implementations
bool CommandParser::containsAutomationKeywords(const std::string& input) {
    std::vector<std::string> keywords = {"click", "type", "open", "close", "run", "execute", "automate", "script"};
    for (const auto& keyword : keywords) {
        if (input.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool CommandParser::containsQueryKeywords(const std::string& input) {
    std::vector<std::string> keywords = {"what", "where", "when", "why", "how", "which", "show", "list", "display"};
    for (const auto& keyword : keywords) {
        if (input.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool CommandParser::containsSystemKeywords(const std::string& input) {
    std::vector<std::string> keywords = {"shutdown", "restart", "sleep", "volume", "brightness", "wifi", "bluetooth"};
    for (const auto& keyword : keywords) {
        if (input.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool CommandParser::containsHelpKeywords(const std::string& input) {
    std::vector<std::string> keywords = {"help", "guide", "tutorial", "how to", "instructions"};
    for (const auto& keyword : keywords) {
        if (input.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

ParsedCommand CommandParser::createMouseClickCommand(int x, int y, const std::string& button) {
    ParsedCommand command;
    command.action = "mouse.click";
    command.parameters = nlohmann::json{
        {"x", x}, 
        {"y", y}, 
        {"button", button}
    };
    command.description = "Click at coordinates (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    return command;
}

ParsedCommand CommandParser::createKeyboardCommand(const std::string& text) {
    ParsedCommand command;
    command.action = "keyboard.type";
    command.parameters = nlohmann::json{{"text", text}};
    command.description = "Type text: " + text;
    return command;
}

ParsedCommand CommandParser::createApplicationCommand(const std::string& appName, const std::string& action) {
    ParsedCommand command;
    command.action = "application." + action;
    command.parameters = nlohmann::json{{"name", appName}};
    command.description = action + " application: " + appName;
    return command;
}

std::string CommandParser::normalizeInput(const std::string& input) {
    std::string normalized = input;
    
    // Remove extra whitespace
    std::regex whitespaceRegex(R"(\s+)");
    normalized = std::regex_replace(normalized, whitespaceRegex, " ");
    
    // Trim leading/trailing whitespace
    normalized.erase(0, normalized.find_first_not_of(" \t\n\r"));
    normalized.erase(normalized.find_last_not_of(" \t\n\r") + 1);
    
    return normalized;
}

std::string CommandParser::generateTaskName(const std::string& input) {
    std::string taskName = input.substr(0, std::min(input.length(), size_t(30)));
    
    // Replace spaces with underscores
    std::replace(taskName.begin(), taskName.end(), ' ', '_');
    
    // Remove special characters
    taskName.erase(std::remove_if(taskName.begin(), taskName.end(), 
                                  [](char c) { return !std::isalnum(c) && c != '_'; }), 
                   taskName.end());
    
    return taskName;
}

// Stub implementations for remaining methods
void CommandParser::setTaskEngine(std::shared_ptr<TaskEngine> taskEngine) { m_taskEngine = taskEngine; }
void CommandParser::setLLMConnector(std::shared_ptr<LLMConnector> llmConnector) { m_llmConnector = llmConnector; }
CommandParser::ParsingStatistics CommandParser::getStatistics() const { return m_statistics; }
void CommandParser::resetStatistics() { m_statistics = {}; }
std::vector<ParsedCommand> CommandParser::parseFileOperationCommand(const std::string& input) { 
    (void)input; // TODO: Implement file operation command parsing
    return {}; 
}
std::vector<ParsedCommand> CommandParser::parseSystemCommand(const std::string& input) { 
    (void)input; // TODO: Implement system command parsing
    return {}; 
}
std::vector<ParsedCommand> CommandParser::applyPatternMatching(const std::string& input, IntentType intent) { 
    (void)intent; // TODO: Use intent type in pattern matching
    return matchAutomationPatterns(input); 
}
std::string CommandParser::buildNLPPrompt(const std::string& userInput) { return "Parse this automation request: " + userInput; }
CommandParseResult CommandParser::parseLLMResponse(const nlohmann::json& response) { 
    (void)response; // TODO: Implement LLM response parsing
    return CommandParseResult(); 
}
bool CommandParser::shouldUseLLMFallback(const std::string& input, const CommandParseResult& initialResult) { 
    (void)input; // TODO: Use input in LLM fallback decision
    return initialResult.commands.empty(); 
}
bool CommandParser::shouldCreateNewTask(const CommandParseResult& result) { return result.commands.size() > 1; }
bool CommandParser::isValidAction(const std::string& action) { return !action.empty(); }
bool CommandParser::hasRequiredParameters(const std::string& action, const nlohmann::json& params) { 
    (void)action; // TODO: Implement parameter requirement checking
    (void)params; // TODO: Validate required parameters
    return true; 
}
std::vector<std::string> CommandParser::getRequiredParameters(const std::string& action) { 
    (void)action; // TODO: Return required parameters for action
    return {}; 
}
void CommandParser::updateStatistics(const CommandParseResult& result) { m_statistics.totalParses++; if (result.success) m_statistics.successfulParses++; else m_statistics.failedParses++; }
void CommandParser::logParsingAttempt(const std::string& input, const CommandParseResult& result) { 
    SLOG_DEBUG().message("Parsing attempt")
        .context("input", input)
        .context("commands_count", result.commands.size())
        .context("success", result.success);
}
ParsedCommand CommandParser::normalizeCommand(const ParsedCommand& command) { return command; }
std::vector<std::string> CommandParser::suggestExistingTasks(const std::string& input) { 
    (void)input; // TODO: Implement task suggestion based on input
    return {}; 
}
void CommandParser::addCustomPattern(const std::string& pattern, const std::string& commandTemplate) { m_customPatterns[pattern] = commandTemplate; }
void CommandParser::setStrictMode(bool enabled) { m_strictMode = enabled; }
void CommandParser::setConfidenceThreshold(ConfidenceLevel threshold) { m_confidenceThreshold = threshold; }
void CommandParser::enableLearning(bool enabled) { m_learningEnabled = enabled; }
void CommandParser::provideFeedback(const std::string& originalInput, const CommandParseResult& result, bool wasCorrect) { 
    (void)originalInput; // TODO: Use original input for feedback
    (void)result; // TODO: Use result for feedback
    (void)wasCorrect; // TODO: Use correctness for learning
}
void CommandParser::saveUserCorrection(const std::string& input, const std::vector<ParsedCommand>& correctedCommands) { 
    (void)input; // TODO: Save user corrections for learning
    (void)correctedCommands; // TODO: Store corrected command patterns
}
void CommandParser::loadCustomPatterns() { }
void CommandParser::saveCustomPatterns() { }
ParsedCommand CommandParser::createCommandFromPattern(const std::string& pattern, const std::map<std::string, std::string>& matches) { 
    (void)pattern; // TODO: Use pattern to create command
    (void)matches; // TODO: Use matches to fill command parameters
    return ParsedCommand(); 
}
void CommandParser::updatePatternsFromFeedback(const std::string& input, const std::vector<ParsedCommand>& commands) { 
    (void)input; // TODO: Update patterns based on input
    (void)commands; // TODO: Learn from successful commands
}
void CommandParser::adjustConfidenceWeights(bool wasCorrect) { 
    (void)wasCorrect; // TODO: Adjust confidence calculation weights
}
std::vector<std::string> CommandParser::tokenizeInput(const std::string& input) { 
    (void)input; // TODO: Implement input tokenization
    return {}; 
}
std::string CommandParser::extractQuotedStrings(const std::string& input) { 
    (void)input; // TODO: Extract quoted strings from input
    return ""; 
}
std::map<std::string, std::string> CommandParser::parseKeyValuePairs(const std::string& input) { 
    (void)input; // TODO: Parse key-value pairs from input
    return {}; 
}
ParsedCommand CommandParser::createFileCommand(const std::string& fileName, const std::string& action) { 
    (void)fileName; // TODO: Use file name in command
    (void)action; // TODO: Use action type in command
    return ParsedCommand(); 
}
ParsedCommand CommandParser::createDelayCommand(int milliseconds) { ParsedCommand cmd; cmd.action = "system.sleep"; cmd.parameters = nlohmann::json{{"ms", milliseconds}}; return cmd; }


