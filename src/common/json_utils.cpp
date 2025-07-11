#include "json_utils.h"
#include "structured_logger.h"
#include "string_utils.h"
#include <algorithm>

namespace burwell {
namespace utils {

bool JsonUtils::validateJsonField(const nlohmann::json& json, const std::string& fieldName, const std::string& expectedType) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return false;
    }

    if (expectedType.empty()) {
        SLOG_ERROR().message("Empty expected type in validateJsonField");
        return false;
    }

    try {
        if (!json.contains(fieldName)) {
            SLOG_WARNING().message("Field not found in JSON").context("field", fieldName);
            return false;
        }

        const auto& field = json[fieldName];
        std::string actualType = jsonTypeToString(field);

        if (expectedType == "string" && field.is_string()) {
            return true;
        } else if (expectedType == "number" && field.is_number()) {
            return true;
        } else if (expectedType == "boolean" && field.is_boolean()) {
            return true;
        } else if (expectedType == "object" && field.is_object()) {
            return true;
        } else if (expectedType == "array" && field.is_array()) {
            return true;
        } else if (expectedType == "null" && field.is_null()) {
            return true;
        }

        SLOG_WARNING().message("Field has incorrect type").context("field", fieldName).context("actual_type", actualType).context("expected_type", expectedType);
        return false;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in validateJsonField").context("error", e.what());
        return false;
    }
}

std::string JsonUtils::getStringField(const nlohmann::json& json, const std::string& fieldName, const std::string& defaultValue) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return defaultValue;
    }

    try {
        if (!json.contains(fieldName)) {
            return defaultValue;
        }

        const auto& field = json[fieldName];
        if (field.is_string()) {
            return field.get<std::string>();
        } else if (field.is_number()) {
            // Convert number to string
            return std::to_string(field.get<double>());
        } else if (field.is_boolean()) {
            // Convert boolean to string
            return field.get<bool>() ? "true" : "false";
        } else {
            SLOG_WARNING().message("Field is not a string, returning default value").context("field", fieldName);
            return defaultValue;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getStringField").context("field", fieldName).context("error", e.what());
        return defaultValue;
    }
}

int JsonUtils::getIntField(const nlohmann::json& json, const std::string& fieldName, int defaultValue) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return defaultValue;
    }

    try {
        if (!json.contains(fieldName)) {
            return defaultValue;
        }

        const auto& field = json[fieldName];
        if (field.is_number_integer()) {
            return field.get<int>();
        } else if (field.is_number_float()) {
            // Convert float to int (truncate)
            return static_cast<int>(field.get<double>());
        } else if (field.is_string()) {
            // Try to parse string as integer
            try {
                return std::stoi(field.get<std::string>());
            } catch (const std::exception&) {
                SLOG_WARNING().message("Cannot convert string field to integer, returning default").context("field", fieldName);
                return defaultValue;
            }
        } else {
            SLOG_WARNING().message("Field is not a number, returning default value").context("field", fieldName);
            return defaultValue;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getIntField").context("field", fieldName).context("error", e.what());
        return defaultValue;
    }
}

bool JsonUtils::getBoolField(const nlohmann::json& json, const std::string& fieldName, bool defaultValue) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return defaultValue;
    }

    try {
        if (!json.contains(fieldName)) {
            return defaultValue;
        }

        const auto& field = json[fieldName];
        if (field.is_boolean()) {
            return field.get<bool>();
        } else if (field.is_string()) {
            // Try to parse string as boolean
            std::string strValue = StringUtils::toLowerCase(field.get<std::string>());
            if (strValue == "true" || strValue == "1" || strValue == "yes") {
                return true;
            } else if (strValue == "false" || strValue == "0" || strValue == "no") {
                return false;
            } else {
                SLOG_WARNING().message("Cannot convert string field to boolean, returning default").context("field", fieldName);
                return defaultValue;
            }
        } else if (field.is_number()) {
            // Convert number to boolean (0 = false, non-zero = true)
            return field.get<double>() != 0.0;
        } else {
            SLOG_WARNING().message("Field is not a boolean, returning default value").context("field", fieldName);
            return defaultValue;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getBoolField").context("field", fieldName).context("error", e.what());
        return defaultValue;
    }
}

double JsonUtils::getDoubleField(const nlohmann::json& json, const std::string& fieldName, double defaultValue) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return defaultValue;
    }

    try {
        if (!json.contains(fieldName)) {
            return defaultValue;
        }

        const auto& field = json[fieldName];
        if (field.is_number()) {
            return field.get<double>();
        } else if (field.is_string()) {
            // Try to parse string as double
            try {
                return std::stod(field.get<std::string>());
            } catch (const std::exception&) {
                SLOG_WARNING().message("Cannot convert string field to double, returning default").context("field", fieldName);
                return defaultValue;
            }
        } else {
            SLOG_WARNING().message("Field is not a number, returning default value").context("field", fieldName);
            return defaultValue;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getDoubleField").context("field", fieldName).context("error", e.what());
        return defaultValue;
    }
}

bool JsonUtils::getObjectField(const nlohmann::json& json, const std::string& fieldName, nlohmann::json& result) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return false;
    }

    try {
        if (!json.contains(fieldName)) {
            SLOG_WARNING().message("Object field not found").context("field", fieldName);
            return false;
        }

        const auto& field = json[fieldName];
        if (!field.is_object()) {
            SLOG_WARNING().message("Field is not an object").context("field", fieldName);
            return false;
        }

        result = field;
        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getObjectField").context("field", fieldName).context("error", e.what());
        return false;
    }
}

bool JsonUtils::getArrayField(const nlohmann::json& json, const std::string& fieldName, nlohmann::json& result) {
    // Input validation
    if (!validateFieldName(fieldName)) {
        return false;
    }

    try {
        if (!json.contains(fieldName)) {
            SLOG_WARNING().message("Array field not found").context("field", fieldName);
            return false;
        }

        const auto& field = json[fieldName];
        if (!field.is_array()) {
            SLOG_WARNING().message("Field is not an array").context("field", fieldName);
            return false;
        }

        result = field;
        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getArrayField").context("field", fieldName).context("error", e.what());
        return false;
    }
}

bool JsonUtils::hasRequiredFields(const nlohmann::json& json, const std::vector<std::string>& requiredFields) {
    // Input validation
    if (requiredFields.empty()) {
        SLOG_ERROR().message("Empty required fields list in hasRequiredFields");
        return false;
    }

    try {
        for (const auto& fieldName : requiredFields) {
            if (!validateFieldName(fieldName)) {
                return false;
            }

            if (!json.contains(fieldName)) {
                SLOG_WARNING().message("Required field missing from JSON").context("field", fieldName);
                return false;
            }
        }

        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in hasRequiredFields").context("error", e.what());
        return false;
    }
}

bool JsonUtils::validateJsonSchema(const nlohmann::json& json, const nlohmann::json& schema) {
    try {
        // Basic schema validation - check required fields and types
        if (schema.contains("required") && schema["required"].is_array()) {
            for (const auto& requiredField : schema["required"]) {
                if (requiredField.is_string()) {
                    std::string fieldName = requiredField.get<std::string>();
                    if (!json.contains(fieldName)) {
                        SLOG_WARNING().message("Schema validation failed: missing required field").context("field", fieldName);
                        return false;
                    }
                }
            }
        }

        // Check field types if specified
        if (schema.contains("properties") && schema["properties"].is_object()) {
            for (const auto& [fieldName, fieldSchema] : schema["properties"].items()) {
                if (json.contains(fieldName) && fieldSchema.contains("type") && fieldSchema["type"].is_string()) {
                    std::string expectedType = fieldSchema["type"].get<std::string>();
                    if (!validateJsonField(json, fieldName, expectedType)) {
                        SLOG_WARNING().message("Schema validation failed: field has wrong type").context("field", fieldName);
                        return false;
                    }
                }
            }
        }

        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in validateJsonSchema").context("error", e.what());
        return false;
    }
}

bool JsonUtils::mergeJsonObjects(const nlohmann::json& base, const nlohmann::json& overlay, nlohmann::json& result) {
    try {
        // Input validation
        if (!base.is_object() || !overlay.is_object()) {
            SLOG_ERROR().message("Both base and overlay must be JSON objects in mergeJsonObjects");
            return false;
        }

        // Start with base object
        result = base;

        // Merge overlay values
        for (const auto& [key, value] : overlay.items()) {
            result[key] = value;
        }

        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in mergeJsonObjects").context("error", e.what());
        return false;
    }
}

std::string JsonUtils::getNestedStringField(const nlohmann::json& json, const std::string& path, const std::string& defaultValue) {
    // Input validation
    if (path.empty()) {
        SLOG_ERROR().message("Empty path in getNestedStringField");
        return defaultValue;
    }

    try {
        std::vector<std::string> pathParts = StringUtils::split(path, ".");
        if (pathParts.empty()) {
            return defaultValue;
        }

        nlohmann::json current = json;

        // Traverse the path
        for (const auto& part : pathParts) {
            if (!current.is_object() || !current.contains(part)) {
                return defaultValue;
            }
            current = current[part];
        }

        // Convert final value to string
        if (current.is_string()) {
            return current.get<std::string>();
        } else if (current.is_number()) {
            return std::to_string(current.get<double>());
        } else if (current.is_boolean()) {
            return current.get<bool>() ? "true" : "false";
        } else {
            return defaultValue;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in getNestedStringField").context("path", path).context("error", e.what());
        return defaultValue;
    }
}

// Private helper functions
bool JsonUtils::validateFieldName(const std::string& fieldName) {
    if (fieldName.empty()) {
        SLOG_ERROR().message("Empty field name provided to JSON utility function");
        return false;
    }
    return true;
}

std::string JsonUtils::jsonTypeToString(const nlohmann::json& json) {
    if (json.is_null()) return "null";
    if (json.is_boolean()) return "boolean";
    if (json.is_number_integer()) return "integer";
    if (json.is_number_float()) return "float";
    if (json.is_string()) return "string";
    if (json.is_array()) return "array";
    if (json.is_object()) return "object";
    return "unknown";
}

} // namespace utils
} // namespace burwell