#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>
#include <vector>
#include <map>

namespace ocal {
namespace atomic {
namespace file {

#ifdef _WIN32

/**
 * Create a directory
 * @param path Directory path to create
 * @return true if directory created successfully
 */
bool createDirectory(const std::string& path);

/**
 * Find files by pattern in a directory
 * @param directory Directory to search in
 * @param pattern File pattern (e.g., "*.txt")
 * @param results Vector to store found file paths
 * @return true if search completed successfully
 */
bool findFilesByPattern(const std::string& directory, const std::string& pattern, std::vector<std::string>& results);

/**
 * Find files by extension in a directory
 * @param directory Directory to search in
 * @param extension File extension (e.g., ".txt")
 * @param results Vector to store found file paths
 * @return true if search completed successfully
 */
bool findFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& results);

/**
 * Move files to target directory
 * @param sourceFiles Vector of source file paths
 * @param targetDirectory Target directory path
 * @param movedFiles Output vector of successfully moved files
 * @param failedFiles Output vector of files that failed to move
 * @return true if operation completed (check individual results in output vectors)
 */
bool moveFiles(const std::vector<std::string>& sourceFiles, const std::string& targetDirectory, 
               std::vector<std::string>& movedFiles, std::vector<std::string>& failedFiles);

/**
 * Delete specified files
 * @param filePaths Vector of file paths to delete
 * @param deletedFiles Output vector of successfully deleted files
 * @param failedFiles Output vector of files that failed to delete
 * @return true if operation completed (check individual results in output vectors)
 */
bool deleteFiles(const std::vector<std::string>& filePaths, 
                 std::vector<std::string>& deletedFiles, std::vector<std::string>& failedFiles);

/**
 * Get file information
 * @param filePath Path to file
 * @param info Output map containing file properties
 * @return true if file information retrieved successfully
 */
bool getFileInfo(const std::string& filePath, std::map<std::string, std::string>& info);

#endif // _WIN32

} // namespace file
} // namespace atomic
} // namespace ocal