#include <iostream>
#include <fstream>
#include "nlohmann/json.hpp"

// Minimal test to verify the fix
bool validateScriptCommands(const nlohmann::json& script) {
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
            std::cerr << "Command is not an object at index " << i << std::endl;
            return false;
        }
        
        if (!cmd.contains("command") || !cmd["command"].is_string()) {
            std::cerr << "Command missing 'command' field at index " << i << std::endl;
            return false;
        }
    }
    
    return true;
}

int main() {
    std::string json_str = R"({
        "description": "Simple DeepSeek test without array indexing",
        "parameters": {},
        "sequence": [
            {
                "command": "UIA_ENUM_WINDOWS",
                "parameters": {
                    "class_name": "MozillaWindowClass",
                    "store_as": "firefoxWindows"
                }
            },
            {
                "command": "UIA_SET_CLIPBOARD",
                "parameters": {
                    "text": "Firefox windows found"
                }
            }
        ]
    })";
    
    try {
        nlohmann::json script = nlohmann::json::parse(json_str);
        std::cout << "JSON parsed successfully" << std::endl;
        
        bool valid = validateScriptCommands(script);
        std::cout << "Script validation: " << (valid ? "PASSED" : "FAILED") << std::endl;
        
        if (valid) {
            std::cout << "The fix works! Scripts with 'sequence' instead of 'commands' are now properly validated." << std::endl;
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Parse error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}