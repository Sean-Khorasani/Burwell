#ifndef BURWELL_INPUT_VALIDATOR_H
#define BURWELL_INPUT_VALIDATOR_H

#include <string>
#include <vector>
#include <regex>
#include <optional>
#include <functional>

namespace burwell {

/**
 * @class InputValidator
 * @brief Comprehensive input validation framework for security and data integrity
 * 
 * This class provides various validation methods to ensure input data is safe
 * and conforms to expected formats before processing.
 */
class InputValidator {
public:
    // String validation
    static bool isNotEmpty(const std::string& input);
    static bool isAlphanumeric(const std::string& input);
    static bool isAlpha(const std::string& input);
    static bool isNumeric(const std::string& input);
    static bool isValidEmail(const std::string& input);
    static bool isValidUrl(const std::string& input);
    static bool matchesPattern(const std::string& input, const std::string& pattern);
    
    // Length validation
    static bool hasMinLength(const std::string& input, size_t minLength);
    static bool hasMaxLength(const std::string& input, size_t maxLength);
    static bool hasLengthBetween(const std::string& input, size_t minLength, size_t maxLength);
    
    // File path validation
    static bool isValidFilePath(const std::string& path);
    static bool isValidFileName(const std::string& filename);
    static bool hasValidExtension(const std::string& filename, const std::vector<std::string>& allowedExtensions);
    static bool isPathTraversalSafe(const std::string& path);
    static bool isAbsolutePath(const std::string& path);
    
    // Command validation
    static bool isValidCommand(const std::string& command);
    static bool containsShellMetacharacters(const std::string& input);
    static bool isSafeForExecution(const std::string& command);
    
    // JSON validation
    static bool isValidJson(const std::string& jsonStr);
    static bool hasRequiredJsonFields(const std::string& jsonStr, const std::vector<std::string>& requiredFields);
    
    // Number validation
    static bool isInRange(int value, int min, int max);
    static bool isInRange(double value, double min, double max);
    static bool isPositive(int value);
    static bool isPositive(double value);
    static bool isNonNegative(int value);
    static bool isNonNegative(double value);
    
    // Sanitization
    static std::string sanitizeForShell(const std::string& input);
    static std::string sanitizeFilePath(const std::string& path);
    static std::string sanitizeHtml(const std::string& input);
    static std::string removeNonPrintable(const std::string& input);
    static std::string trimWhitespace(const std::string& input);
    
    // Custom validation
    using ValidationFunc = std::function<bool(const std::string&)>;
    static bool validate(const std::string& input, const std::vector<ValidationFunc>& validators);
    
    // Validation result with error message
    struct ValidationResult {
        bool isValid;
        std::string errorMessage;
        
        ValidationResult(bool valid, const std::string& error = "") 
            : isValid(valid), errorMessage(error) {}
        
        operator bool() const { return isValid; }
    };
    
    // Advanced validation with detailed error messages
    static ValidationResult validateFilePath(const std::string& path);
    static ValidationResult validateCommand(const std::string& command);
    static ValidationResult validateJson(const std::string& jsonStr);
    static ValidationResult validateWithRules(const std::string& input, 
                                             const std::vector<std::pair<ValidationFunc, std::string>>& rules);

private:
    // Helper methods
    static bool containsControlCharacters(const std::string& input);
    static bool containsNullByte(const std::string& input);
    static std::string normalizePathSeparators(const std::string& path);
    
    // Regex patterns
    static const std::regex EMAIL_PATTERN;
    static const std::regex URL_PATTERN;
    static const std::regex ALPHANUMERIC_PATTERN;
    static const std::regex ALPHA_PATTERN;
    static const std::regex NUMERIC_PATTERN;
    static const std::regex SHELL_META_PATTERN;
    static const std::regex PATH_TRAVERSAL_PATTERN;
};

/**
 * @class ValidationException
 * @brief Exception thrown when validation fails in critical scenarios
 */
class ValidationException : public std::exception {
public:
    explicit ValidationException(const std::string& message) : m_message(message) {}
    const char* what() const noexcept override { return m_message.c_str(); }

private:
    std::string m_message;
};

} // namespace burwell

#endif // BURWELL_INPUT_VALIDATOR_H