#include "filesystem_operations.h"
#include "../common/structured_logger.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <io.h>
#include <direct.h>
#pragma comment(lib, "shlwapi.lib")
#undef ERROR  // Undefine Windows ERROR macro to avoid conflict with LogLevel::ERROR_LEVEL
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <ftw.h>
#include <utime.h>
#include <fcntl.h>
#endif

using namespace burwell;

namespace burwell {
namespace ocal {
namespace filesystem {

bool fileExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0) && S_ISREG(buffer.st_mode);
#endif
}

bool directoryExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode);
#endif
}

FileInfo getFileInfo(const std::string& path) {
    FileInfo info;
    info.fullPath = getAbsolutePath(path);
    info.name = getFilename(path);
    info.extension = getExtension(path);
    
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileData)) {
        info.exists = true;
        info.isDirectory = (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.isFile = !info.isDirectory;
        
        // Convert FILETIME to time_t
        ULARGE_INTEGER uli;
        uli.LowPart = fileData.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fileData.ftLastWriteTime.dwHighDateTime;
        info.lastModified = static_cast<std::time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
        
        uli.LowPart = fileData.ftCreationTime.dwLowDateTime;
        uli.HighPart = fileData.ftCreationTime.dwHighDateTime;
        info.created = static_cast<std::time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
        
        if (!info.isDirectory) {
            info.size = (static_cast<size_t>(fileData.nFileSizeHigh) << 32) | fileData.nFileSizeLow;
        }
    }
#else
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0) {
        info.exists = true;
        info.isDirectory = S_ISDIR(buffer.st_mode);
        info.isFile = S_ISREG(buffer.st_mode);
        info.size = static_cast<size_t>(buffer.st_size);
        info.lastModified = buffer.st_mtime;
        info.created = buffer.st_ctime;
    }
#endif
    
    return info;
}

bool copyFile(const std::string& source, 
              const std::string& destination, 
              bool overwrite,
              bool preserveAttributes) {
    
    SLOG_DEBUG().message("Copying file").context("source", source).context("destination", destination);
    
    if (!fileExists(source)) {
        SLOG_ERROR().message("Source file does not exist").context("source", source);
        return false;
    }
    
    if (fileExists(destination) && !overwrite) {
        SLOG_WARNING().message("Destination file exists and overwrite is false").context("destination", destination);
        return false;
    }
    
#ifdef _WIN32
    BOOL result = CopyFileA(source.c_str(), destination.c_str(), !overwrite);
    if (!result) {
        SLOG_ERROR().message("Failed to copy file").context("error_code", GetLastError());
        return false;
    }
    
    if (!preserveAttributes) {
        // Reset attributes to normal
        SetFileAttributesA(destination.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    
    return true;
#else
    std::ifstream src(source, std::ios::binary);
    std::ofstream dst(destination, std::ios::binary);
    
    if (!src.is_open() || !dst.is_open()) {
        SLOG_ERROR().message("Failed to open files for copying");
        return false;
    }
    
    dst << src.rdbuf();
    
    if (!src.good() || !dst.good()) {
        SLOG_ERROR().message("Error during file copy");
        return false;
    }
    
    src.close();
    dst.close();
    
    if (preserveAttributes) {
        struct stat sourceStat;
        if (stat(source.c_str(), &sourceStat) == 0) {
            chmod(destination.c_str(), sourceStat.st_mode);
            
            struct utimbuf times;
            times.actime = sourceStat.st_atime;
            times.modtime = sourceStat.st_mtime;
            utime(destination.c_str(), &times);
        }
    }
    
    return true;
#endif
}

bool copyDirectory(const std::string& source, 
                   const std::string& destination, 
                   bool recursive,
                   bool overwrite,
                   bool preserveAttributes) {
    
    SLOG_DEBUG().message("Copying directory").context("source", source).context("destination", destination);
    
    if (!directoryExists(source)) {
        SLOG_ERROR().message("Source directory does not exist").context("source", source);
        return false;
    }
    
    if (!createDirectory(destination)) {
        SLOG_ERROR().message("Failed to create destination directory").context("destination", destination);
        return false;
    }
    
    auto files = listDirectory(source, false, true);
    for (const auto& file : files) {
        std::string srcPath = source + "/" + file.name;
        std::string dstPath = destination + "/" + file.name;
        
        if (file.isDirectory) {
            if (recursive) {
                if (!copyDirectory(srcPath, dstPath, recursive, overwrite, preserveAttributes)) {
                    return false;
                }
            }
        } else {
            if (!copyFile(srcPath, dstPath, overwrite, preserveAttributes)) {
                return false;
            }
        }
    }
    
    return true;
}

bool deleteFile(const std::string& path) {
    SLOG_DEBUG().message("Deleting file").context("path", path);
    
#ifdef _WIN32
    return DeleteFileA(path.c_str()) != FALSE;
#else
    return unlink(path.c_str()) == 0;
#endif
}

bool deleteDirectory(const std::string& path, bool recursive) {
    SLOG_DEBUG().message("Deleting directory").context("path", path).context("recursive", recursive);
    
    if (recursive) {
        auto files = listDirectory(path, false, true);
        for (const auto& file : files) {
            std::string fullPath = path + "/" + file.name;
            if (file.isDirectory) {
                if (!deleteDirectory(fullPath, true)) {
                    return false;
                }
            } else {
                if (!deleteFile(fullPath)) {
                    return false;
                }
            }
        }
    }
    
#ifdef _WIN32
    return RemoveDirectoryA(path.c_str()) != FALSE;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

bool createDirectory(const std::string& path, bool createParents) {
    if (directoryExists(path)) {
        return true;
    }
    
    if (createParents) {
        std::string parent = getDirectory(path);
        if (!parent.empty() && parent != path) {
            if (!createDirectory(parent, true)) {
                return false;
            }
        }
    }
    
    SLOG_DEBUG().message("Creating directory").context("path", path);
    
#ifdef _WIN32
    return CreateDirectoryA(path.c_str(), nullptr) != FALSE;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool move(const std::string& source, const std::string& destination, bool overwrite) {
    SLOG_DEBUG().message("Moving").context("source", source).context("destination", destination);
    
    if (fileExists(destination) && !overwrite) {
        SLOG_WARNING().message("Destination exists and overwrite is false").context("destination", destination);
        return false;
    }
    
#ifdef _WIN32
    DWORD flags = MOVEFILE_COPY_ALLOWED;
    if (overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    return MoveFileExA(source.c_str(), destination.c_str(), flags) != FALSE;
#else
    if (overwrite && (fileExists(destination) || directoryExists(destination))) {
        if (fileExists(destination)) {
            if (!deleteFile(destination)) return false;
        } else {
            if (!deleteDirectory(destination, true)) return false;
        }
    }
    return rename(source.c_str(), destination.c_str()) == 0;
#endif
}

bool waitForFile(const std::string& path, 
                int timeoutMs, 
                int checkIntervalMs,
                WaitCondition waitFor) {
    
    SLOG_DEBUG().message("Waiting for file").context("path", path);
    
    auto startTime = std::chrono::steady_clock::now();
    FileInfo lastInfo = getFileInfo(path);
    
    while (true) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
        
        if (elapsedMs >= timeoutMs) {
            SLOG_WARNING().message("File wait timeout").context("path", path);
            return false;
        }
        
        FileInfo currentInfo = getFileInfo(path);
        
        switch (waitFor) {
            case WaitCondition::EXISTS:
                if (currentInfo.exists) {
                    SLOG_DEBUG().message("File now exists").context("path", path);
                    return true;
                }
                break;
                
            case WaitCondition::MODIFIED:
                if (currentInfo.exists && currentInfo.lastModified > lastInfo.lastModified) {
                    SLOG_DEBUG().message("File modified").context("path", path);
                    return true;
                }
                break;
                
            case WaitCondition::SIZE_CHANGE:
                if (currentInfo.exists && currentInfo.size != lastInfo.size) {
                    SLOG_DEBUG().message("File size changed").context("path", path);
                    return true;
                }
                break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }
}

std::vector<FileInfo> listDirectory(const std::string& path, 
                                   bool recursive,
                                   bool includeHidden) {
    std::vector<FileInfo> files;
    
#ifdef _WIN32
    std::string searchPath = path + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fileName = findData.cFileName;
            if (fileName == "." || fileName == "..") continue;
            
            if (!includeHidden && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) {
                continue;
            }
            
            FileInfo info;
            info.name = fileName;
            info.fullPath = path + "\\" + fileName;
            info.exists = true;
            info.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            info.isFile = !info.isDirectory;
            info.extension = getExtension(fileName);
            
            if (!info.isDirectory) {
                info.size = (static_cast<size_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
            }
            
            files.push_back(info);
            
            if (recursive && info.isDirectory) {
                auto subFiles = listDirectory(info.fullPath, true, includeHidden);
                files.insert(files.end(), subFiles.begin(), subFiles.end());
            }
            
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fileName = entry->d_name;
            if (fileName == "." || fileName == "..") continue;
            
            if (!includeHidden && fileName[0] == '.') {
                continue;
            }
            
            std::string fullPath = path + "/" + fileName;
            FileInfo info = getFileInfo(fullPath);
            info.name = fileName;
            
            files.push_back(info);
            
            if (recursive && info.isDirectory) {
                auto subFiles = listDirectory(fullPath, true, includeHidden);
                files.insert(files.end(), subFiles.begin(), subFiles.end());
            }
        }
        closedir(dir);
    }
#endif
    
    return files;
}

size_t getFileSize(const std::string& path) {
    FileInfo info = getFileInfo(path);
    return info.exists ? info.size : 0;
}

std::time_t getLastModified(const std::string& path) {
    FileInfo info = getFileInfo(path);
    return info.exists ? info.lastModified : 0;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SLOG_ERROR().message("Failed to open file for reading").context("path", path);
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    if (!file.good() && !file.eof()) {
        SLOG_ERROR().message("Error reading file").context("path", path);
        return "";
    }
    
    return content;
}

bool writeFile(const std::string& path, const std::string& content, bool append) {
    std::ios::openmode mode = std::ios::binary;
    if (append) {
        mode |= std::ios::app;
    } else {
        mode |= std::ios::trunc;
    }
    
    std::ofstream file(path, mode);
    if (!file.is_open()) {
        SLOG_ERROR().message("Failed to open file for writing").context("path", path);
        return false;
    }
    
    file << content;
    
    if (!file.good()) {
        SLOG_ERROR().message("Error writing file").context("path", path);
        return false;
    }
    
    return true;
}

std::string getAbsolutePath(const std::string& path) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD result = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
    if (result > 0 && result <= MAX_PATH) {
        return std::string(buffer);
    }
    return path;
#else
    char buffer[PATH_MAX];
    if (realpath(path.c_str(), buffer) != nullptr) {
        return std::string(buffer);
    }
    return path;
#endif
}

std::string getDirectory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    return "";
}

std::string getFilename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

std::string getExtension(const std::string& path) {
    std::string filename = getFilename(path);
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos && pos > 0) {
        return filename.substr(pos + 1);
    }
    return "";
}

} // namespace filesystem
} // namespace ocal
} // namespace burwell