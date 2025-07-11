#ifndef BURWELL_STRING_UTILS_H
#define BURWELL_STRING_UTILS_H

#include <string>
#include <vector>

namespace burwell {
namespace utils {

/**
 * @brief Utility class for string operations with comprehensive validation
 * 
 * This class provides centralized string manipulation operations with proper
 * input validation and error handling following all development rules.
 */
class StringUtils {
public:
    /**
     * @brief Replace all occurrences of a substring with another string
     * @param str Source string to modify (input parameter)
     * @param from Substring to find (must not be empty)
     * @param to Replacement string (can be empty)
     * @return Modified string with all replacements made
     * @note Validates input parameters and handles edge cases
     */
    static std::string replaceAll(const std::string& str, const std::string& from, const std::string& to);

    /**
     * @brief Remove leading and trailing whitespace from string
     * @param str String to trim (input parameter)
     * @return Trimmed string
     * @note Handles empty strings and all-whitespace strings properly
     */
    static std::string trim(const std::string& str);

    /**
     * @brief Remove leading whitespace from string
     * @param str String to trim (input parameter)
     * @return Left-trimmed string
     */
    static std::string trimLeft(const std::string& str);

    /**
     * @brief Remove trailing whitespace from string
     * @param str String to trim (input parameter)
     * @return Right-trimmed string
     */
    static std::string trimRight(const std::string& str);

    /**
     * @brief Split string by delimiter into vector of strings
     * @param str String to split (input parameter)
     * @param delimiter Delimiter character or string (must not be empty)
     * @return Vector of split strings (empty if input is empty)
     * @note Validates input and handles consecutive delimiters
     */
    static std::vector<std::string> split(const std::string& str, const std::string& delimiter);

    /**
     * @brief Check if string starts with specified prefix
     * @param str String to check (input parameter)
     * @param prefix Prefix to look for (must not be empty)
     * @return true if string starts with prefix, false otherwise
     * @note Case-sensitive comparison, validates input
     */
    static bool startsWith(const std::string& str, const std::string& prefix);

    /**
     * @brief Check if string ends with specified suffix
     * @param str String to check (input parameter)
     * @param suffix Suffix to look for (must not be empty)
     * @return true if string ends with suffix, false otherwise
     * @note Case-sensitive comparison, validates input
     */
    static bool endsWith(const std::string& str, const std::string& suffix);

    /**
     * @brief Convert string to lowercase
     * @param str String to convert (input parameter)
     * @return Lowercase version of string
     * @note Handles Unicode characters properly on Windows
     */
    static std::string toLowerCase(const std::string& str);

    /**
     * @brief Convert string to uppercase
     * @param str String to convert (input parameter)
     * @return Uppercase version of string
     * @note Handles Unicode characters properly on Windows
     */
    static std::string toUpperCase(const std::string& str);

    /**
     * @brief Join vector of strings with delimiter
     * @param strings Vector of strings to join (can be empty)
     * @param delimiter Delimiter to use between strings (can be empty)
     * @return Joined string
     * @note Handles empty vector and empty strings properly
     */
    static std::string join(const std::vector<std::string>& strings, const std::string& delimiter);

    /**
     * @brief Check if string contains only whitespace characters
     * @param str String to check (input parameter)
     * @return true if string is empty or contains only whitespace
     */
    static bool isWhitespaceOnly(const std::string& str);

    /**
     * @brief Escape special characters for JSON string
     * @param str String to escape (input parameter)
     * @return JSON-escaped string
     * @note Escapes quotes, backslashes, and control characters
     */
    static std::string escapeJsonString(const std::string& str);

private:
    // Private helper functions
    static bool isWhitespace(char c);
};

} // namespace utils
} // namespace burwell

#endif // BURWELL_STRING_UTILS_H