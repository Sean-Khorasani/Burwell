#ifndef BURWELL_OS_UTILS_H
#define BURWELL_OS_UTILS_H

#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <functional>

// OS-specific headers and macro management
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shlwapi.h>
    #include <psapi.h>
    #include <tlhelp32.h>
    // Undefine problematic Windows macros that conflict with our code
    #undef ERROR
    #undef max
    #undef min
    #undef GetCurrentDirectory
    #undef CreateDirectory
    #undef DeleteFile
    #undef CopyFile
    #undef MoveFile
    #undef FindFirstFile
    #undef GetUserName
    #pragma comment(lib, "shlwapi.lib")
    #pragma comment(lib, "psapi.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include <limits.h>
    #include <libgen.h>
#endif

namespace burwell {
namespace os {

// Platform detection
enum class Platform {
    WINDOWS,
    LINUX,
    MACOS,
    UNKNOWN
};

// Path handling utilities
class PathUtils {
public:
    // Cross-platform path operations
    static std::string normalize(const std::string& path);
    static std::string makeAbsolute(const std::string& path);
    static std::string makeRelative(const std::string& path, const std::string& basePath);
    static std::string join(const std::vector<std::string>& parts);
    static std::string join(const std::string& part1, const std::string& part2);
    static std::string getFileName(const std::string& path);
    static std::string getDirectory(const std::string& path);
    static std::string getExtension(const std::string& path);
    static std::string removeExtension(const std::string& path);
    
    // Platform-specific path formatting
    static std::string toNativePath(const std::string& path);
    static std::string toUnixPath(const std::string& path);
    static std::string toWindowsPath(const std::string& path);
    
    // Path validation and checking
    static bool isAbsolute(const std::string& path);
    static bool isRelative(const std::string& path);
    static bool exists(const std::string& path);
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static bool isExecutable(const std::string& path);
    
    // Path manipulation
    static std::string expandEnvironmentVariables(const std::string& path);
    static std::string expandUserHome(const std::string& path);
    static std::string getCanonicalPath(const std::string& path);
    
    // Directory operations
    static bool createDirectory(const std::string& path, bool recursive = true);
    static bool removeDirectory(const std::string& path, bool recursive = false);
    static std::vector<std::string> listDirectory(const std::string& path);
    
    // File operations
    static bool copyFile(const std::string& source, const std::string& destination);
    static bool moveFile(const std::string& source, const std::string& destination);
    static bool deleteFile(const std::string& path);
    static uint64_t getFileSize(const std::string& path);
    static std::chrono::system_clock::time_point getLastModified(const std::string& path);
    
private:
    static char getPathSeparator();
    static const char* getPathSeparatorString();
};

// System information utilities
class SystemInfo {
public:
    // Platform detection
    static Platform getCurrentPlatform();
    static std::string getPlatformName();
    static std::string getOSVersion();
    static bool isWindows();
    static bool isLinux();
    static bool isMacOS();
    
    // System paths
    static std::string getExecutablePath();
    static std::string getExecutableDirectory();
    static std::string getCurrentWorkingDirectory();
    static std::string getUserHomeDirectory();
    static std::string getUserDocumentsDirectory();
    static std::string getTempDirectory();
    static std::string getSystemDirectory();
    
    // Environment variables
    static std::string getEnvironmentVariable(const std::string& name);
    static bool setEnvironmentVariable(const std::string& name, const std::string& value);
    static std::vector<std::pair<std::string, std::string>> getAllEnvironmentVariables();
    
    // User information
    static std::string getCurrentUserName();
    static std::string getCurrentUserFullName();
    static bool isRunningAsAdministrator();
    
    // System resources
    static uint64_t getTotalMemory();
    static uint64_t getAvailableMemory();
    static int getProcessorCount();
    static double getCPUUsage();
    
    // Network information
    static std::string getComputerName();
    static std::vector<std::string> getNetworkInterfaces();
    static std::string getLocalIPAddress();
};

// Process management utilities
class ProcessUtils {
public:
    struct ProcessInfo {
        uint32_t processId;
        std::string processName;
        std::string executablePath;
        uint32_t parentProcessId;
        uint64_t memoryUsage;
        double cpuUsage;
        std::chrono::system_clock::time_point startTime;
    };
    
    // Process operations
    static uint32_t getCurrentProcessId();
    static uint32_t getParentProcessId();
    static std::string getCurrentProcessName();
    static std::string getCurrentProcessPath();
    
    // Process enumeration
    static std::vector<ProcessInfo> getRunningProcesses();
    static std::vector<ProcessInfo> findProcessesByName(const std::string& name);
    static ProcessInfo getProcessInfo(uint32_t processId);
    
    // Process control
    static bool terminateProcess(uint32_t processId);
    static bool isProcessRunning(uint32_t processId);
    static bool isProcessRunning(const std::string& processName);
    
    // Process launching
    static uint32_t launchProcess(const std::string& executablePath, 
                                 const std::vector<std::string>& arguments = {},
                                 const std::string& workingDirectory = "",
                                 bool elevated = false,
                                 bool waitForExit = false);
};

// File system utilities
class FileSystemUtils {
public:
    // Permission management
    static bool hasReadPermission(const std::string& path);
    static bool hasWritePermission(const std::string& path);
    static bool hasExecutePermission(const std::string& path);
    static bool setFilePermissions(const std::string& path, int permissions);
    
    // File attributes
    static bool isHidden(const std::string& path);
    static bool isReadOnly(const std::string& path);
    static bool isSystemFile(const std::string& path);
    static bool setHidden(const std::string& path, bool hidden);
    static bool setReadOnly(const std::string& path, bool readOnly);
    
    // Disk information
    static uint64_t getFreeDiskSpace(const std::string& path);
    static uint64_t getTotalDiskSpace(const std::string& path);
    static std::string getDiskVolumeName(const std::string& path);
    
    // File watching
    static bool watchDirectory(const std::string& path, 
                              std::function<void(const std::string&, const std::string&)> callback);
    static void stopWatchingDirectory(const std::string& path);
};

// Registry utilities (Windows only)
#ifdef _WIN32
class RegistryUtils {
public:
    enum class RootKey {
        CLASSES_ROOT,
        CURRENT_USER,
        LOCAL_MACHINE,
        USERS,
        CURRENT_CONFIG
    };
    
    static bool readString(RootKey root, const std::string& subKey, 
                          const std::string& valueName, std::string& value);
    static bool writeString(RootKey root, const std::string& subKey,
                           const std::string& valueName, const std::string& value);
    static bool readDWord(RootKey root, const std::string& subKey,
                         const std::string& valueName, uint32_t& value);
    static bool writeDWord(RootKey root, const std::string& subKey,
                          const std::string& valueName, uint32_t value);
    static bool deleteKey(RootKey root, const std::string& subKey);
    static bool deleteValue(RootKey root, const std::string& subKey, const std::string& valueName);
    static std::vector<std::string> enumerateSubKeys(RootKey root, const std::string& subKey);
    static std::vector<std::string> enumerateValues(RootKey root, const std::string& subKey);
};
#endif

// Error handling utilities
class ErrorUtils {
public:
    static std::string getLastSystemError();
    static std::string formatSystemError(int errorCode);
    static void logSystemError(const std::string& operation);
    static bool isNetworkError(int errorCode);
    static bool isPermissionError(int errorCode);
    static bool isFileNotFoundError(int errorCode);
};

// Console utilities
class ConsoleUtils {
public:
    // Console control
    static bool setConsoleTitle(const std::string& title);
    static std::string getConsoleTitle();
    static bool clearConsole();
    static bool isConsoleApplication();
    
    // Console colors (cross-platform)
    enum class Color {
        BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE,
        BRIGHT_BLACK, BRIGHT_RED, BRIGHT_GREEN, BRIGHT_YELLOW,
        BRIGHT_BLUE, BRIGHT_MAGENTA, BRIGHT_CYAN, BRIGHT_WHITE
    };
    
    static void setTextColor(Color color);
    static void setBackgroundColor(Color color);
    static void resetColors();
    
    // Console size and position
    static std::pair<int, int> getConsoleSize();
    static bool setConsoleSize(int width, int height);
    static std::pair<int, int> getCursorPosition();
    static bool setCursorPosition(int x, int y);
};

// Utility macros for OS-specific code
#define OS_WINDOWS_ONLY(code) do { if (os::SystemInfo::isWindows()) { code } } while(0)
#define OS_LINUX_ONLY(code) do { if (os::SystemInfo::isLinux()) { code } } while(0)
#define OS_MACOS_ONLY(code) do { if (os::SystemInfo::isMacOS()) { code } } while(0)

// Path manipulation macros
#define NATIVE_PATH(path) os::PathUtils::toNativePath(path)
#define ABSOLUTE_PATH(path) os::PathUtils::makeAbsolute(path)
#define JOIN_PATH(a, b) os::PathUtils::join(a, b)
#define NORMALIZE_PATH(path) os::PathUtils::normalize(path)

// Unicode/UTF-8 conversion utilities for Windows
#ifdef _WIN32
class UnicodeUtils {
public:
    // Convert UTF-16 (Windows Unicode) to UTF-8
    static std::string wideStringToUtf8(const wchar_t* wideStr);
    static std::string wideStringToUtf8(const std::wstring& wideStr);
    
    // Convert UTF-8 to UTF-16 (Windows Unicode)
    static std::wstring utf8ToWideString(const std::string& utf8Str);
    static std::wstring utf8ToWideString(const char* utf8Str);
};
#endif

} // namespace os
} // namespace burwell

#endif // BURWELL_OS_UTILS_H