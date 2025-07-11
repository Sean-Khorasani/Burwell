#include "file_utils.h"
#include "structured_logger.h"
#include "os_utils.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace burwell {
namespace utils {

bool FileUtils::loadJsonFromFile(const std::string& filePath, nlohmann::json& jsonOutput) {
    // Input validation
    if (!validateFilePath(filePath)) {
        SLOG_ERROR().message("Invalid file path provided to loadJsonFromFile");
        return false;
    }

    try {
        // Check file existence
        if (!fileExists(filePath)) {
            SLOG_ERROR().message("File not found").context("path", filePath);
            return false;
        }

        // Open file with proper error handling
        std::ifstream file(filePath);
        if (!file.is_open()) {
            SLOG_ERROR().message("Cannot open file for reading").context("path", filePath);
            return false;
        }

        // Parse JSON with validation
        try {
            file >> jsonOutput;
            if (file.fail()) {
                SLOG_ERROR().message("Failed to read JSON from file").context("path", filePath);
                return false;
            }
        } catch (const nlohmann::json::parse_error& e) {
            SLOG_ERROR().message("JSON parse error").context("path", filePath).context("error", e.what());
            return false;
        }

        // Validate output
        if (jsonOutput.is_null()) {
            SLOG_WARNING().message("Loaded empty JSON from file").context("path", filePath);
        }

        SLOG_INFO().message("Successfully loaded JSON from file").context("path", filePath);
        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in loadJsonFromFile").context("path", filePath).context("error", e.what());
        return false;
    }
}

bool FileUtils::saveJsonToFile(const std::string& filePath, const nlohmann::json& jsonData) {
    // Input validation
    if (!validateFilePath(filePath)) {
        SLOG_ERROR().message("Invalid file path provided to saveJsonToFile");
        return false;
    }

    try {
        // Ensure parent directory exists
        if (!ensureParentDirectoryExists(filePath)) {
            SLOG_ERROR().message("Cannot create parent directory").context("path", filePath);
            return false;
        }

        // Use atomic write operation (write to temp file, then rename)
        std::string tempFilePath = filePath + ".tmp";
        
        // Write to temporary file
        std::ofstream tempFile(tempFilePath);
        if (!tempFile.is_open()) {
            SLOG_ERROR().message("Cannot create temporary file").context("temp_path", tempFilePath);
            return false;
        }

        try {
            tempFile << jsonData.dump(2); // Pretty print with 2-space indentation
            tempFile.flush();
            
            if (tempFile.fail()) {
                SLOG_ERROR().message("Failed to write JSON to temporary file").context("temp_path", tempFilePath);
                tempFile.close();
                std::filesystem::remove(tempFilePath); // Cleanup
                return false;
            }
        } catch (const std::exception& e) {
            SLOG_ERROR().message("JSON serialization error").context("error", e.what());
            tempFile.close();
            std::filesystem::remove(tempFilePath); // Cleanup
            return false;
        }

        tempFile.close();

        // Atomic rename operation
        try {
            std::filesystem::rename(tempFilePath, filePath);
            SLOG_INFO().message("Successfully saved JSON to file").context("path", filePath);
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR().message("Failed to rename temporary file").context("error", e.what());
            std::filesystem::remove(tempFilePath); // Cleanup
            return false;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in saveJsonToFile").context("path", filePath).context("error", e.what());
        return false;
    }
}

bool FileUtils::fileExists(const std::string& filePath) {
    // Input validation
    if (!validateFilePath(filePath)) {
        return false;
    }

    try {
        return std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception checking file existence").context("path", filePath).context("error", e.what());
        return false;
    }
}

bool FileUtils::createDirectoryIfNotExists(const std::string& directoryPath) {
    // Input validation
    if (directoryPath.empty()) {
        SLOG_ERROR().message("Empty directory path provided to createDirectoryIfNotExists");
        return false;
    }

    try {
        if (std::filesystem::exists(directoryPath)) {
            if (std::filesystem::is_directory(directoryPath)) {
                return true; // Directory already exists
            } else {
                SLOG_ERROR().message("Path exists but is not a directory").context("path", directoryPath);
                return false;
            }
        }

        // Create directory and all parent directories
        if (std::filesystem::create_directories(directoryPath)) {
            SLOG_INFO().message("Created directory").context("path", directoryPath);
            return true;
        } else {
            SLOG_ERROR().message("Could not create directory").context("path", directoryPath);
            return false;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception creating directory").context("path", directoryPath).context("error", e.what());
        return false;
    }
}

bool FileUtils::readFileToString(const std::string& filePath, std::string& content) {
    // Input validation
    if (!validateFilePath(filePath)) {
        SLOG_ERROR().message("Invalid file path provided to readFileToString");
        return false;
    }

    try {
        // Check file existence
        if (!fileExists(filePath)) {
            SLOG_ERROR().message("File not found").context("path", filePath);
            return false;
        }

        // Open file
        std::ifstream file(filePath);
        if (!file.is_open()) {
            SLOG_ERROR().message("Cannot open file for reading").context("path", filePath);
            return false;
        }

        // Read content efficiently
        std::stringstream buffer;
        buffer << file.rdbuf();
        
        if (file.fail() && !file.eof()) {
            SLOG_ERROR().message("Error reading file content").context("path", filePath);
            return false;
        }

        content = buffer.str();
        SLOG_INFO().message("Successfully read file content").context("path", filePath).context("bytes", content.length());
        return true;

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in readFileToString").context("path", filePath).context("error", e.what());
        return false;
    }
}

bool FileUtils::writeStringToFile(const std::string& filePath, const std::string& content) {
    // Input validation
    if (!validateFilePath(filePath)) {
        SLOG_ERROR().message("Invalid file path provided to writeStringToFile");
        return false;
    }

    try {
        // Ensure parent directory exists
        if (!ensureParentDirectoryExists(filePath)) {
            SLOG_ERROR().message("Cannot create parent directory").context("path", filePath);
            return false;
        }

        // Use atomic write operation
        std::string tempFilePath = filePath + ".tmp";
        
        // Write to temporary file
        std::ofstream tempFile(tempFilePath);
        if (!tempFile.is_open()) {
            SLOG_ERROR().message("Cannot create temporary file").context("temp_path", tempFilePath);
            return false;
        }

        tempFile << content;
        tempFile.flush();
        
        if (tempFile.fail()) {
            SLOG_ERROR().message("Failed to write content to temporary file").context("temp_path", tempFilePath);
            tempFile.close();
            std::filesystem::remove(tempFilePath); // Cleanup
            return false;
        }

        tempFile.close();

        // Atomic rename operation
        try {
            std::filesystem::rename(tempFilePath, filePath);
            SLOG_INFO().message("Successfully wrote content to file").context("path", filePath).context("bytes", content.length());
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR().message("Failed to rename temporary file").context("error", e.what());
            std::filesystem::remove(tempFilePath); // Cleanup
            return false;
        }

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in writeStringToFile").context("path", filePath).context("error", e.what());
        return false;
    }
}

// Private helper functions
bool FileUtils::validateFilePath(const std::string& filePath) {
    if (filePath.empty()) {
        SLOG_ERROR().message("Empty file path provided");
        return false;
    }

    // Check for invalid characters (Windows-specific, but exclude colon for drive letters)
    const std::string invalidChars = "<>\"|?*";
    for (char c : invalidChars) {
        if (filePath.find(c) != std::string::npos) {
            SLOG_ERROR().message("Invalid character in file path").context("path", filePath);
            return false;
        }
    }
    
    // Special handling for colon - only allow as drive separator (position 1)
    size_t colonPos = filePath.find(':');
    if (colonPos != std::string::npos && colonPos != 1) {
        SLOG_ERROR().message("Invalid colon position in file path").context("path", filePath);
        return false;
    }

    // Check path length (Windows MAX_PATH limitation)
    if (filePath.length() > 260) {
        SLOG_ERROR().message("File path too long (>260 characters)").context("path", filePath);
        return false;
    }

    return true;
}

bool FileUtils::ensureParentDirectoryExists(const std::string& filePath) {
    try {
        std::filesystem::path path(filePath);
        std::filesystem::path parentPath = path.parent_path();
        
        if (parentPath.empty()) {
            return true; // No parent directory needed
        }

        return createDirectoryIfNotExists(parentPath.string());

    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception getting parent directory").context("path", filePath).context("error", e.what());
        return false;
    }
}

} // namespace utils
} // namespace burwell