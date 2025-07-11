#ifndef BURWELL_FILE_UTILS_H
#define BURWELL_FILE_UTILS_H

#include <string>
#include <nlohmann/json.hpp>

namespace burwell {
namespace utils {

/**
 * @brief Utility class for file operations with comprehensive validation
 * 
 * This class provides centralized file I/O operations with proper error handling,
 * input validation, and resource management following all development rules.
 */
class FileUtils {
public:
    /**
     * @brief Load JSON from file with comprehensive validation
     * @param filePath Path to JSON file (must not be empty)
     * @param jsonOutput Reference to store parsed JSON (output parameter)
     * @return true if successful, false otherwise
     * @note Validates input, handles file errors, and logs appropriately
     */
    static bool loadJsonFromFile(const std::string& filePath, nlohmann::json& jsonOutput);

    /**
     * @brief Save JSON to file with atomic operation
     * @param filePath Path to save JSON file (must not be empty)
     * @param jsonData JSON data to save (must be valid)
     * @return true if successful, false otherwise
     * @note Creates directories if needed, uses atomic write
     */
    static bool saveJsonToFile(const std::string& filePath, const nlohmann::json& jsonData);

    /**
     * @brief Check if file exists with path validation
     * @param filePath Path to check (must not be empty)
     * @return true if file exists and is accessible, false otherwise
     * @note Validates input path before checking existence
     */
    static bool fileExists(const std::string& filePath);

    /**
     * @brief Create directory if it doesn't exist
     * @param directoryPath Path to directory (must not be empty)
     * @return true if directory exists or was created, false otherwise
     * @note Creates parent directories as needed
     */
    static bool createDirectoryIfNotExists(const std::string& directoryPath);

    /**
     * @brief Read entire file content as string
     * @param filePath Path to file (must not be empty)
     * @param content Reference to store file content (output parameter)
     * @return true if successful, false otherwise
     * @note Validates input and handles large files efficiently
     */
    static bool readFileToString(const std::string& filePath, std::string& content);

    /**
     * @brief Write string content to file atomically
     * @param filePath Path to file (must not be empty)
     * @param content Content to write (can be empty)
     * @return true if successful, false otherwise
     * @note Uses atomic write operation to prevent corruption
     */
    static bool writeStringToFile(const std::string& filePath, const std::string& content);

private:
    // Private helper functions
    static bool validateFilePath(const std::string& filePath);
    static bool ensureParentDirectoryExists(const std::string& filePath);
};

} // namespace utils
} // namespace burwell

#endif // BURWELL_FILE_UTILS_H