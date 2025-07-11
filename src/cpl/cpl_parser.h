#ifndef BURWELL_CPL_PARSER_H
#define BURWELL_CPL_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <regex>
#include <nlohmann/json.hpp>

namespace burwell {
namespace cpl {

struct CPLCommand {
    std::string type;                           // e.g., "MOUSE_CLICK"
    std::map<std::string, std::string> parameters; // e.g., {"x": "100", "y": "200"}
    std::map<std::string, std::string> metadata;   // e.g., {"id": "click_button"}
    std::string originalText;                   // Original CPL command text
    int lineNumber;                            // Line number in script
    bool isValid;                              // Validation result
    std::string validationError;               // Error message if invalid
};

struct CPLParseResult {
    std::vector<CPLCommand> commands;
    bool success;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

class CPLParser {
public:
    CPLParser();
    ~CPLParser() = default;
    
    // Main parsing interface
    CPLParseResult parse(const std::string& cplScript);
    CPLParseResult parseLine(const std::string& line, int lineNumber = 1);
    
    // Command validation
    bool validateCommand(CPLCommand& command);
    
    // LLM response conversion
    CPLParseResult convertFromLLMResponse(const std::string& llmResponse);
    
    // Command templates management
    void loadCommandTemplates(const nlohmann::json& templates);
    nlohmann::json getCommandTemplates() const;
    
    // Syntax checking
    bool isValidCommandSyntax(const std::string& commandText);
    std::vector<std::string> extractCommandTypes() const;
    
    // Error handling
    std::string getLastError() const;
    std::vector<std::string> getWarnings() const;

private:
    // Core parsing methods
    bool parseCommandLine(const std::string& line, CPLCommand& command);
    bool extractCommandType(const std::string& line, std::string& type);
    bool extractParameters(const std::string& line, std::map<std::string, std::string>& params);
    bool extractMetadata(const std::string& line, std::map<std::string, std::string>& metadata);
    
    // Parameter parsing helpers
    std::string extractParameterSection(const std::string& line);
    std::string extractMetadataSection(const std::string& line);
    std::map<std::string, std::string> parseKeyValuePairs(const std::string& section);
    
    // Validation methods
    bool validateParameterTypes(const CPLCommand& command);
    bool validateRequiredParameters(const CPLCommand& command);
    bool validateParameterValues(const CPLCommand& command);
    bool validateParameterValue(const std::string& paramName, const std::string& paramValue, const nlohmann::json& paramDef);
    
    // Template management
    void loadDefaultTemplates();
    
    // Utility methods
    std::string trim(const std::string& str);
    std::string toLowerCase(const std::string& str);
    bool isComment(const std::string& line);
    bool isEmpty(const std::string& line);
    
    // Member variables
    nlohmann::json m_commandTemplates;
    std::string m_lastError;
    std::vector<std::string> m_warnings;
    
    // Regular expressions for parsing
    std::regex m_commandRegex;
    std::regex m_parameterRegex;
    std::regex m_metadataRegex;
    std::regex m_keyValueRegex;
};

// Utility functions
std::string cplCommandToString(const CPLCommand& command);
nlohmann::json cplCommandToJson(const CPLCommand& command);
CPLCommand cplCommandFromJson(const nlohmann::json& json);

} // namespace cpl
} // namespace burwell

#endif // BURWELL_CPL_PARSER_H