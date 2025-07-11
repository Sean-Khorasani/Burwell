#include "input_validator.h"
#include "string_utils.h"
#include "json_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace burwell {

using burwell::utils::StringUtils;

// Initialize static regex patterns
const std::regex InputValidator::EMAIL_PATTERN(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
const std::regex InputValidator::URL_PATTERN(R"(^https?://[a-zA-Z0-9.-]+(?:\.[a-zA-Z]{2,})+(?:/[^?#]*)?(?:\?[^#]*)?(?:#.*)?$)");
const std::regex InputValidator::ALPHANUMERIC_PATTERN("^[a-zA-Z0-9]+$");
const std::regex InputValidator::ALPHA_PATTERN("^[a-zA-Z]+$");
const std::regex InputValidator::NUMERIC_PATTERN("^[0-9]+$");
const std::regex InputValidator::SHELL_META_PATTERN(R"([;&|`$<>\\'"()\[\]{}*?~])");
const std::regex InputValidator::PATH_TRAVERSAL_PATTERN(R"(\.\.[\\/])");

// String validation methods
bool InputValidator::isNotEmpty(const std::string& input) {
    return !input.empty() && !StringUtils::isWhitespaceOnly(input);
}

bool InputValidator::isAlphanumeric(const std::string& input) {
    return !input.empty() && std::regex_match(input, ALPHANUMERIC_PATTERN);
}

bool InputValidator::isAlpha(const std::string& input) {
    return !input.empty() && std::regex_match(input, ALPHA_PATTERN);
}

bool InputValidator::isNumeric(const std::string& input) {
    return !input.empty() && std::regex_match(input, NUMERIC_PATTERN);
}

bool InputValidator::isValidEmail(const std::string& input) {
    return !input.empty() && std::regex_match(input, EMAIL_PATTERN);
}

bool InputValidator::isValidUrl(const std::string& input) {
    return !input.empty() && std::regex_match(input, URL_PATTERN);
}

bool InputValidator::matchesPattern(const std::string& input, const std::string& pattern) {
    try {
        std::regex customPattern(pattern);
        return std::regex_match(input, customPattern);
    } catch (const std::regex_error&) {
        return false;
    }
}

// Length validation methods
bool InputValidator::hasMinLength(const std::string& input, size_t minLength) {
    return input.length() >= minLength;
}

bool InputValidator::hasMaxLength(const std::string& input, size_t maxLength) {
    return input.length() <= maxLength;
}

bool InputValidator::hasLengthBetween(const std::string& input, size_t minLength, size_t maxLength) {
    return input.length() >= minLength && input.length() <= maxLength;
}

// File path validation methods
bool InputValidator::isValidFilePath(const std::string& path) {
    if (path.empty()) return false;
    
    // Check for null bytes
    if (containsNullByte(path)) return false;
    
    // Check for path traversal attempts
    if (!isPathTraversalSafe(path)) return false;
    
    // Check for invalid characters (Windows)
#ifdef _WIN32
    const std::string invalidChars = "<>:\"|?*";
    if (path.find_first_of(invalidChars) != std::string::npos) {
        // Exception: Allow : for drive letters and \\ for UNC paths
        if (path.length() >= 2 && path[1] == ':' && std::isalpha(path[0])) {
            // This is okay - it's a drive letter
        } else if (path.substr(0, 2) == "\\\\") {
            // This is okay - it's a UNC path
        } else {
            return false;
        }
    }
#endif
    
    return true;
}

bool InputValidator::isValidFileName(const std::string& filename) {
    if (filename.empty()) return false;
    
    // Check for directory separators in filename
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        return false;
    }
    
    // Check for reserved names on Windows
#ifdef _WIN32
    std::string upperName = StringUtils::toUpperCase(filename);
    std::vector<std::string> reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                        "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    
    for (const auto& res : reserved) {
        if (upperName == res || StringUtils::startsWith(upperName, res + ".")) {
            return false;
        }
    }
#endif
    
    return isValidFilePath(filename);
}

bool InputValidator::hasValidExtension(const std::string& filename, const std::vector<std::string>& allowedExtensions) {
    if (allowedExtensions.empty()) return true;
    
    size_t lastDot = filename.find_last_of('.');
    if (lastDot == std::string::npos) return false;
    
    std::string extension = StringUtils::toLowerCase(filename.substr(lastDot));
    
    for (const auto& allowed : allowedExtensions) {
        std::string normalizedAllowed = allowed;
        if (!StringUtils::startsWith(normalizedAllowed, ".")) {
            normalizedAllowed = "." + normalizedAllowed;
        }
        if (StringUtils::toLowerCase(normalizedAllowed) == extension) {
            return true;
        }
    }
    
    return false;
}

bool InputValidator::isPathTraversalSafe(const std::string& path) {
    // Check for .. sequences
    if (std::regex_search(path, PATH_TRAVERSAL_PATTERN)) {
        return false;
    }
    
    // Additional checks for encoded traversal attempts
    if (path.find("%2e%2e") != std::string::npos || 
        path.find("%252e%252e") != std::string::npos) {
        return false;
    }
    
    return true;
}

bool InputValidator::isAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
    
#ifdef _WIN32
    // Windows absolute paths: C:\ or \\server\share
    return (path.length() >= 3 && std::isalpha(path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
           (path.length() >= 2 && path[0] == '\\' && path[1] == '\\');
#else
    // Unix absolute paths start with /
    return path[0] == '/';
#endif
}

// Command validation methods
bool InputValidator::isValidCommand(const std::string& command) {
    if (command.empty()) return false;
    
    // Check for null bytes
    if (containsNullByte(command)) return false;
    
    // Check for excessive length
    if (command.length() > 8192) return false;
    
    return true;
}

bool InputValidator::containsShellMetacharacters(const std::string& input) {
    return std::regex_search(input, SHELL_META_PATTERN);
}

bool InputValidator::isSafeForExecution(const std::string& command) {
    return isValidCommand(command) && !containsShellMetacharacters(command);
}

// JSON validation methods
bool InputValidator::isValidJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        (void)j; // Explicitly ignore the parsed result
        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

bool InputValidator::hasRequiredJsonFields(const std::string& jsonStr, const std::vector<std::string>& requiredFields) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        for (const auto& field : requiredFields) {
            if (!j.contains(field)) {
                return false;
            }
        }
        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

// Number validation methods
bool InputValidator::isInRange(int value, int min, int max) {
    return value >= min && value <= max;
}

bool InputValidator::isInRange(double value, double min, double max) {
    return value >= min && value <= max;
}

bool InputValidator::isPositive(int value) {
    return value > 0;
}

bool InputValidator::isPositive(double value) {
    return value > 0.0;
}

bool InputValidator::isNonNegative(int value) {
    return value >= 0;
}

bool InputValidator::isNonNegative(double value) {
    return value >= 0.0;
}

// Sanitization methods
std::string InputValidator::sanitizeForShell(const std::string& input) {
    std::string result;
    result.reserve(input.length() * 2);
    
    for (char c : input) {
        if (std::regex_match(std::string(1, c), SHELL_META_PATTERN)) {
            result += '\\';
        }
        result += c;
    }
    
    return result;
}

std::string InputValidator::sanitizeFilePath(const std::string& path) {
    std::string result = normalizePathSeparators(path);
    
    // Remove any null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
    
    // Remove trailing dots and spaces (Windows)
#ifdef _WIN32
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
        result.pop_back();
    }
#endif
    
    return result;
}

std::string InputValidator::sanitizeHtml(const std::string& input) {
    std::string result;
    result.reserve(input.length() * 1.5);
    
    for (char c : input) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c;
        }
    }
    
    return result;
}

std::string InputValidator::removeNonPrintable(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        if (std::isprint(static_cast<unsigned char>(c)) || c == '\n' || c == '\r' || c == '\t') {
            result += c;
        }
    }
    
    return result;
}

std::string InputValidator::trimWhitespace(const std::string& input) {
    return StringUtils::trim(input);
}

// Custom validation
bool InputValidator::validate(const std::string& input, const std::vector<ValidationFunc>& validators) {
    for (const auto& validator : validators) {
        if (!validator(input)) {
            return false;
        }
    }
    return true;
}

// Advanced validation with error messages
InputValidator::ValidationResult InputValidator::validateFilePath(const std::string& path) {
    if (path.empty()) {
        return ValidationResult(false, "File path cannot be empty");
    }
    
    if (containsNullByte(path)) {
        return ValidationResult(false, "File path contains null bytes");
    }
    
    if (!isPathTraversalSafe(path)) {
        return ValidationResult(false, "File path contains directory traversal attempt");
    }
    
    if (!isValidFilePath(path)) {
        return ValidationResult(false, "File path contains invalid characters");
    }
    
    return ValidationResult(true);
}

InputValidator::ValidationResult InputValidator::validateCommand(const std::string& command) {
    if (command.empty()) {
        return ValidationResult(false, "Command cannot be empty");
    }
    
    if (containsNullByte(command)) {
        return ValidationResult(false, "Command contains null bytes");
    }
    
    if (command.length() > 8192) {
        return ValidationResult(false, "Command exceeds maximum length");
    }
    
    if (containsShellMetacharacters(command)) {
        return ValidationResult(false, "Command contains shell metacharacters");
    }
    
    return ValidationResult(true);
}

InputValidator::ValidationResult InputValidator::validateJson(const std::string& jsonStr) {
    if (jsonStr.empty()) {
        return ValidationResult(false, "JSON string cannot be empty");
    }
    
    try {
        auto j = nlohmann::json::parse(jsonStr);
        (void)j; // Explicitly ignore the parsed result
        return ValidationResult(true);
    } catch (const nlohmann::json::parse_error& e) {
        return ValidationResult(false, "Invalid JSON: " + std::string(e.what()));
    }
}

InputValidator::ValidationResult InputValidator::validateWithRules(const std::string& input, 
                                                                  const std::vector<std::pair<ValidationFunc, std::string>>& rules) {
    for (const auto& [validator, errorMessage] : rules) {
        if (!validator(input)) {
            return ValidationResult(false, errorMessage);
        }
    }
    return ValidationResult(true);
}

// Helper methods
bool InputValidator::containsControlCharacters(const std::string& input) {
    return std::any_of(input.begin(), input.end(), [](char c) {
        return std::iscntrl(static_cast<unsigned char>(c)) && c != '\n' && c != '\r' && c != '\t';
    });
}

bool InputValidator::containsNullByte(const std::string& input) {
    return input.find('\0') != std::string::npos;
}

std::string InputValidator::normalizePathSeparators(const std::string& path) {
    std::string result = path;
    
#ifdef _WIN32
    // On Windows, normalize to backslashes
    std::replace(result.begin(), result.end(), '/', '\\');
#else
    // On Unix, normalize to forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    
    return result;
}

} // namespace burwell