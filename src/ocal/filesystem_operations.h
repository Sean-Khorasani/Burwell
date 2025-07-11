#ifndef BURWELL_FILESYSTEM_OPERATIONS_H
#define BURWELL_FILESYSTEM_OPERATIONS_H

#include <string>
#include <vector>
#include <ctime>

namespace burwell {
namespace ocal {
namespace filesystem {

struct FileInfo {
    bool exists;
    bool isDirectory;
    bool isFile;
    size_t size;
    std::time_t lastModified;
    std::time_t created;
    std::string fullPath;
    std::string name;
    std::string extension;
    
    FileInfo() : exists(false), isDirectory(false), isFile(false), size(0), lastModified(0), created(0) {}
};

/**
 * Check if file exists
 * 
 * @param path File path to check
 * @return true if file exists, false otherwise
 */
bool fileExists(const std::string& path);

/**
 * Check if directory exists
 * 
 * @param path Directory path to check
 * @return true if directory exists, false otherwise
 */
bool directoryExists(const std::string& path);

/**
 * Get detailed file information
 * 
 * @param path File or directory path
 * @return FileInfo structure with detailed information
 */
FileInfo getFileInfo(const std::string& path);

/**
 * Copy file from source to destination
 * 
 * @param source Source file path
 * @param destination Destination file path
 * @param overwrite Whether to overwrite if destination exists
 * @param preserveAttributes Whether to preserve file attributes and timestamps
 * @return true if successful, false otherwise
 */
bool copyFile(const std::string& source, 
              const std::string& destination, 
              bool overwrite = false,
              bool preserveAttributes = true);

/**
 * Copy directory and its contents
 * 
 * @param source Source directory path
 * @param destination Destination directory path
 * @param recursive Whether to copy subdirectories
 * @param overwrite Whether to overwrite existing files
 * @param preserveAttributes Whether to preserve file attributes and timestamps
 * @return true if successful, false otherwise
 */
bool copyDirectory(const std::string& source, 
                   const std::string& destination, 
                   bool recursive = true,
                   bool overwrite = false,
                   bool preserveAttributes = true);

/**
 * Delete file
 * 
 * @param path File path to delete
 * @return true if successful, false otherwise
 */
bool deleteFile(const std::string& path);

/**
 * Delete directory
 * 
 * @param path Directory path to delete
 * @param recursive Whether to delete subdirectories and files
 * @return true if successful, false otherwise
 */
bool deleteDirectory(const std::string& path, bool recursive = false);

/**
 * Create directory (including parent directories if needed)
 * 
 * @param path Directory path to create
 * @param createParents Whether to create parent directories
 * @return true if successful, false otherwise
 */
bool createDirectory(const std::string& path, bool createParents = true);

/**
 * Move/rename file or directory
 * 
 * @param source Source path
 * @param destination Destination path
 * @param overwrite Whether to overwrite if destination exists
 * @return true if successful, false otherwise
 */
bool move(const std::string& source, const std::string& destination, bool overwrite = false);

/**
 * Wait for file to exist or be modified
 * 
 * @param path File path to monitor
 * @param timeoutMs Maximum wait time in milliseconds
 * @param checkIntervalMs How often to check in milliseconds
 * @param waitFor What to wait for (exists, modified, size_change)
 * @return true if condition was met, false if timeout
 */
enum class WaitCondition {
    EXISTS,
    MODIFIED,
    SIZE_CHANGE
};

bool waitForFile(const std::string& path, 
                int timeoutMs = 60000, 
                int checkIntervalMs = 1000,
                WaitCondition waitFor = WaitCondition::EXISTS);

/**
 * List files and directories in a directory
 * 
 * @param path Directory path to list
 * @param recursive Whether to list subdirectories recursively
 * @param includeHidden Whether to include hidden files
 * @return Vector of FileInfo for each item found
 */
std::vector<FileInfo> listDirectory(const std::string& path, 
                                   bool recursive = false,
                                   bool includeHidden = false);

/**
 * Get file size in bytes
 * 
 * @param path File path
 * @return File size in bytes, 0 if file doesn't exist
 */
size_t getFileSize(const std::string& path);

/**
 * Get last modification time
 * 
 * @param path File path
 * @return Last modification time, 0 if file doesn't exist
 */
std::time_t getLastModified(const std::string& path);

/**
 * Read entire file content as string
 * 
 * @param path File path to read
 * @return File content as string, empty if failed
 */
std::string readFile(const std::string& path);

/**
 * Write string content to file
 * 
 * @param path File path to write
 * @param content Content to write
 * @param append Whether to append or overwrite
 * @return true if successful, false otherwise
 */
bool writeFile(const std::string& path, const std::string& content, bool append = false);

/**
 * Get absolute path from relative path
 * 
 * @param path Relative or absolute path
 * @return Absolute path
 */
std::string getAbsolutePath(const std::string& path);

/**
 * Get directory name from path
 * 
 * @param path File or directory path
 * @return Directory portion of path
 */
std::string getDirectory(const std::string& path);

/**
 * Get filename from path
 * 
 * @param path File path
 * @return Filename portion of path
 */
std::string getFilename(const std::string& path);

/**
 * Get file extension from path
 * 
 * @param path File path
 * @return File extension (without dot)
 */
std::string getExtension(const std::string& path);

} // namespace filesystem
} // namespace ocal
} // namespace burwell

#endif // BURWELL_FILESYSTEM_OPERATIONS_H