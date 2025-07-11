#include "file_operations.h"
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include <regex>

namespace ocal {
namespace atomic {
namespace file {

#ifdef _WIN32

bool createDirectory(const std::string& path) {
    // Validate path before creating directory
    auto validationResult = burwell::InputValidator::validateFilePath(path);
    if (!validationResult.isValid) {
        SLOG_ERROR().message("[ATOMIC] Invalid directory path")
            .context("error", validationResult.errorMessage);
        return false;
    }
    
    std::wstring wPath(path.begin(), path.end());
    
    if (CreateDirectoryW(wPath.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        SLOG_DEBUG().message("[ATOMIC] Directory created/exists")
        .context("path", path);
        return true;
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to create directory")
        .context("path", path)
        .context("error_code", GetLastError());
    return false;
}

bool findFilesByPattern(const std::string& directory, const std::string& pattern, std::vector<std::string>& results) {
    // Validate directory path
    auto dirValidation = burwell::InputValidator::validateFilePath(directory);
    if (!dirValidation.isValid) {
        SLOG_ERROR().message("[ATOMIC] Invalid directory path")
            .context("error", dirValidation.errorMessage);
        return false;
    }
    
    // Validate pattern doesn't contain path traversal
    if (pattern.find("..") != std::string::npos || pattern.find("/") != std::string::npos || pattern.find("\\") != std::string::npos) {
        SLOG_ERROR().message("[ATOMIC] Invalid pattern: contains path separators")
            .context("pattern", pattern);
        return false;
    }
    
    std::wstring wSearchPath(directory.begin(), directory.end());
    wSearchPath += L"\\" + std::wstring(pattern.begin(), pattern.end());
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(wSearchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring wFileName = findData.cFileName;
                std::string fileName(wFileName.begin(), wFileName.end());
                results.push_back(directory + "\\" + fileName);
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    
    SLOG_DEBUG().message("[ATOMIC] Found files with pattern")
        .context("count", results.size())
        .context("pattern", pattern);
    return true;
}

bool findFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& results) {
    // Validate directory path
    auto dirValidation = burwell::InputValidator::validateFilePath(directory);
    if (!dirValidation.isValid) {
        SLOG_ERROR().message("[ATOMIC] Invalid directory path")
            .context("error", dirValidation.errorMessage);
        return false;
    }
    
    // Validate extension - allow alphanumeric and dots
    std::regex extensionPattern("^[a-zA-Z0-9.]+$");
    if (!std::regex_match(extension, extensionPattern)) {
        SLOG_ERROR().message("[ATOMIC] Invalid file extension")
            .context("extension", extension);
        return false;
    }
    
    std::string searchExtension = extension;
    
    // Ensure extension starts with dot
    if (!searchExtension.empty() && searchExtension[0] != '.') {
        searchExtension = "." + searchExtension;
    }
    
    std::wstring wSearchPath(directory.begin(), directory.end());
    wSearchPath += L"\\*" + std::wstring(searchExtension.begin(), searchExtension.end());
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(wSearchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring wFileName = findData.cFileName;
                std::string fileName(wFileName.begin(), wFileName.end());
                results.push_back(directory + "\\" + fileName);
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    
    SLOG_DEBUG().message("[ATOMIC] Found files with extension")
        .context("count", results.size())
        .context("extension", extension);
    return true;
}

bool moveFiles(const std::vector<std::string>& sourceFiles, const std::string& targetDirectory,
               std::vector<std::string>& movedFiles, std::vector<std::string>& failedFiles) {
    // Validate target directory
    auto targetValidation = burwell::InputValidator::validateFilePath(targetDirectory);
    if (!targetValidation.isValid) {
        SLOG_ERROR().message("[ATOMIC] Invalid target directory")
            .context("error", targetValidation.errorMessage);
        return false;
    }
    
    // Validate each source file
    for (const auto& sourceFile : sourceFiles) {
        auto sourceValidation = burwell::InputValidator::validateFilePath(sourceFile);
        if (!sourceValidation.isValid) {
            SLOG_WARNING().message("[ATOMIC] Skipping invalid source file")
                .context("file", sourceFile)
                .context("error", sourceValidation.errorMessage);
            failedFiles.push_back(sourceFile);
            continue;
        }
        std::wstring wSourcePath(sourceFile.begin(), sourceFile.end());
        
        // Extract filename from source path
        size_t lastSlash = sourceFile.find_last_of("\\/");
        std::string fileName = (lastSlash != std::string::npos) ? 
            sourceFile.substr(lastSlash + 1) : sourceFile;
        
        std::string targetPath = targetDirectory + "\\" + fileName;
        std::wstring wTargetPath(targetPath.begin(), targetPath.end());
        
        if (MoveFileW(wSourcePath.c_str(), wTargetPath.c_str())) {
            movedFiles.push_back(targetPath);
        } else {
            failedFiles.push_back(sourceFile);
        }
    }
    
    SLOG_DEBUG().message("[ATOMIC] File move operation completed")
        .context("moved_count", movedFiles.size())
        .context("failed_count", failedFiles.size());
    return true;
}

bool deleteFiles(const std::vector<std::string>& filePaths,
                 std::vector<std::string>& deletedFiles, std::vector<std::string>& failedFiles) {
    
    for (const auto& filePath : filePaths) {
        // Validate file path before deletion
        auto validation = burwell::InputValidator::validateFilePath(filePath);
        if (!validation.isValid) {
            SLOG_WARNING().message("[ATOMIC] Skipping invalid file path for deletion")
                .context("file", filePath)
                .context("error", validation.errorMessage);
            failedFiles.push_back(filePath);
            continue;
        }
        std::wstring wFilePath(filePath.begin(), filePath.end());
        
        if (DeleteFileW(wFilePath.c_str())) {
            deletedFiles.push_back(filePath);
        } else {
            failedFiles.push_back(filePath);
        }
    }
    
    SLOG_DEBUG().message("[ATOMIC] File deletion operation completed")
        .context("deleted_count", deletedFiles.size())
        .context("failed_count", failedFiles.size());
    return true;
}

bool getFileInfo(const std::string& filePath, std::map<std::string, std::string>& info) {
    // Validate file path
    auto validation = burwell::InputValidator::validateFilePath(filePath);
    if (!validation.isValid) {
        SLOG_ERROR().message("[ATOMIC] Invalid file path")
            .context("file", filePath)
            .context("error", validation.errorMessage);
        return false;
    }
    
    std::wstring wFilePath(filePath.begin(), filePath.end());
    
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (GetFileAttributesExW(wFilePath.c_str(), GetFileExInfoStandard, &fileData)) {
        ULARGE_INTEGER fileSize;
        fileSize.LowPart = fileData.nFileSizeLow;
        fileSize.HighPart = fileData.nFileSizeHigh;
        
        info["filePath"] = filePath;
        info["size"] = std::to_string(static_cast<long long>(fileSize.QuadPart));
        info["isDirectory"] = (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "true" : "false";
        info["isHidden"] = (fileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) ? "true" : "false";
        info["isReadOnly"] = (fileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? "true" : "false";
        info["attributes"] = std::to_string(fileData.dwFileAttributes);
        
        SLOG_DEBUG().message("[ATOMIC] Retrieved file info")
            .context("file", filePath);
        return true;
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to get file info")
        .context("file", filePath)
        .context("error_code", GetLastError());
    return false;
}

#endif // _WIN32

} // namespace file
} // namespace atomic
} // namespace ocal