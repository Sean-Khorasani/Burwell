#ifndef BURWELL_JSON_UTILS_H
#define BURWELL_JSON_UTILS_H

#include <string>
#include <nlohmann/json.hpp>

namespace burwell {
namespace utils {

/**
 * @brief Utility class for JSON operations with comprehensive validation
 * 
 * This class provides centralized JSON validation and extraction operations
 * with proper input validation and error handling following all development rules.
 */
class JsonUtils {
public:
    /**
     * @brief Validate that JSON contains a field of expected type
     * @param json JSON object to check (input parameter)
     * @param fieldName Name of field to validate (must not be empty)
     * @param expectedType Expected JSON type (string, number, boolean, object, array)
     * @return true if field exists and has correct type, false otherwise
     * @note Logs validation failures for debugging
     */
    static bool validateJsonField(const nlohmann::json& json, const std::string& fieldName, const std::string& expectedType);

    /**
     * @brief Get string field from JSON with validation and default value
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param defaultValue Default value if field missing or invalid (can be empty)
     * @return Field value as string, or default value if not found/invalid
     * @note Validates input and handles type conversion errors
     */
    static std::string getStringField(const nlohmann::json& json, const std::string& fieldName, const std::string& defaultValue = "");

    /**
     * @brief Get integer field from JSON with validation and default value
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param defaultValue Default value if field missing or invalid
     * @return Field value as integer, or default value if not found/invalid
     * @note Validates input and handles type conversion errors
     */
    static int getIntField(const nlohmann::json& json, const std::string& fieldName, int defaultValue = 0);

    /**
     * @brief Get boolean field from JSON with validation and default value
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param defaultValue Default value if field missing or invalid
     * @return Field value as boolean, or default value if not found/invalid
     * @note Validates input and handles type conversion errors
     */
    static bool getBoolField(const nlohmann::json& json, const std::string& fieldName, bool defaultValue = false);

    /**
     * @brief Get double field from JSON with validation and default value
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param defaultValue Default value if field missing or invalid
     * @return Field value as double, or default value if not found/invalid
     * @note Validates input and handles type conversion errors
     */
    static double getDoubleField(const nlohmann::json& json, const std::string& fieldName, double defaultValue = 0.0);

    /**
     * @brief Get object field from JSON with validation
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param result Reference to store extracted object (output parameter)
     * @return true if field exists and is object, false otherwise
     * @note Validates input and handles missing fields
     */
    static bool getObjectField(const nlohmann::json& json, const std::string& fieldName, nlohmann::json& result);

    /**
     * @brief Get array field from JSON with validation
     * @param json JSON object to extract from (input parameter)
     * @param fieldName Name of field to extract (must not be empty)
     * @param result Reference to store extracted array (output parameter)
     * @return true if field exists and is array, false otherwise
     * @note Validates input and handles missing fields
     */
    static bool getArrayField(const nlohmann::json& json, const std::string& fieldName, nlohmann::json& result);

    /**
     * @brief Check if JSON has all required fields
     * @param json JSON object to check (input parameter)
     * @param requiredFields Vector of required field names (must not be empty)
     * @return true if all fields exist, false otherwise
     * @note Logs missing fields for debugging
     */
    static bool hasRequiredFields(const nlohmann::json& json, const std::vector<std::string>& requiredFields);

    /**
     * @brief Validate JSON structure against expected schema
     * @param json JSON object to validate (input parameter)
     * @param schema JSON schema definition (input parameter)
     * @return true if JSON matches schema, false otherwise
     * @note Basic schema validation for common use cases
     */
    static bool validateJsonSchema(const nlohmann::json& json, const nlohmann::json& schema);

    /**
     * @brief Safely merge two JSON objects
     * @param base Base JSON object (input parameter)
     * @param overlay JSON object to merge into base (input parameter)
     * @param result Reference to store merged result (output parameter)
     * @return true if merge successful, false otherwise
     * @note Overlay values take precedence over base values
     */
    static bool mergeJsonObjects(const nlohmann::json& base, const nlohmann::json& overlay, nlohmann::json& result);

    /**
     * @brief Get nested field from JSON using dot notation
     * @param json JSON object to extract from (input parameter)
     * @param path Dot-separated path (e.g., "config.database.host")
     * @param defaultValue Default value if field not found
     * @return Field value as string, or default value if not found
     * @note Handles nested object traversal safely
     */
    static std::string getNestedStringField(const nlohmann::json& json, const std::string& path, const std::string& defaultValue = "");

private:
    // Private helper functions
    static bool validateFieldName(const std::string& fieldName);
    static std::string jsonTypeToString(const nlohmann::json& json);
};

} // namespace utils
} // namespace burwell

#endif // BURWELL_JSON_UTILS_H