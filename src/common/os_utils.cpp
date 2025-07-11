#include "os_utils.h"
#include "structured_logger.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <vector>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
    #include <comdef.h>
    #include <wbemidl.h>
    #include <iphlpapi.h>
    #include <lmcons.h>
    #pragma comment(lib, "wbemuuid.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/utsname.h>
    #include <sys/statvfs.h>
    #include <dirent.h>
    #include <signal.h>
    #include <ifaddrs.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/sysinfo.h>
    #include <sys/ioctl.h>
#endif

namespace burwell {
namespace os {

// PathUtils implementation
std::string PathUtils::normalize(const std::string& path) {
    if (path.empty()) return "";
    
    std::filesystem::path fsPath(path);
    try {
        return fsPath.lexically_normal().string();
    } catch (...) {
        return path;
    }
}

std::string PathUtils::makeAbsolute(const std::string& path) {
    if (path.empty()) return "";
    
    try {
        std::filesystem::path fsPath(path);
        if (fsPath.is_absolute()) {
            return normalize(path);
        }
        
        std::filesystem::path absolute = std::filesystem::current_path() / fsPath;
        return normalize(absolute.string());
    } catch (...) {
        return path;
    }
}

std::string PathUtils::makeRelative(const std::string& path, const std::string& basePath) {
    try {
        std::filesystem::path pathFs(path);
        std::filesystem::path baseFs(basePath);
        
        if (!pathFs.is_absolute()) pathFs = std::filesystem::absolute(pathFs);
        if (!baseFs.is_absolute()) baseFs = std::filesystem::absolute(baseFs);
        
        return std::filesystem::relative(pathFs, baseFs).string();
    } catch (...) {
        return path;
    }
}

std::string PathUtils::join(const std::vector<std::string>& parts) {
    if (parts.empty()) return "";
    
    std::filesystem::path result(parts[0]);
    for (size_t i = 1; i < parts.size(); ++i) {
        result /= parts[i];
    }
    return normalize(result.string());
}

std::string PathUtils::join(const std::string& part1, const std::string& part2) {
    std::filesystem::path result(part1);
    result /= part2;
    return normalize(result.string());
}

std::string PathUtils::getFileName(const std::string& path) {
    std::filesystem::path fsPath(path);
    return fsPath.filename().string();
}

std::string PathUtils::getDirectory(const std::string& path) {
    std::filesystem::path fsPath(path);
    return fsPath.parent_path().string();
}

std::string PathUtils::getExtension(const std::string& path) {
    std::filesystem::path fsPath(path);
    return fsPath.extension().string();
}

std::string PathUtils::removeExtension(const std::string& path) {
    std::filesystem::path fsPath(path);
    fsPath.replace_extension("");
    return fsPath.string();
}

std::string PathUtils::toNativePath(const std::string& path) {
    std::string result = path;
    
#ifdef _WIN32
    // Convert forward slashes to backslashes on Windows
    std::replace(result.begin(), result.end(), '/', '\\');
#else
    // Convert backslashes to forward slashes on Unix
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    
    return normalize(result);
}

std::string PathUtils::toUnixPath(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::string PathUtils::toWindowsPath(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

bool PathUtils::isAbsolute(const std::string& path) {
    return std::filesystem::path(path).is_absolute();
}

bool PathUtils::isRelative(const std::string& path) {
    return std::filesystem::path(path).is_relative();
}

bool PathUtils::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool PathUtils::isFile(const std::string& path) {
    return std::filesystem::is_regular_file(path);
}

bool PathUtils::isDirectory(const std::string& path) {
    return std::filesystem::is_directory(path);
}

bool PathUtils::isExecutable(const std::string& path) {
#ifdef _WIN32
    std::string ext = getExtension(path);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
#else
    return (access(path.c_str(), X_OK) == 0);
#endif
}

std::string PathUtils::expandEnvironmentVariables(const std::string& path) {
    std::string result = path;
    
#ifdef _WIN32
    wchar_t expanded[MAX_PATH];
    std::wstring wPath = UnicodeUtils::utf8ToWideString(path);
    DWORD size = ExpandEnvironmentStringsW(wPath.c_str(), expanded, MAX_PATH);
    if (size > 0 && size <= MAX_PATH) {
        result = UnicodeUtils::wideStringToUtf8(expanded);
    }
#else
    // Simple implementation for Unix - could be enhanced
    size_t start = 0;
    while ((start = result.find('$', start)) != std::string::npos) {
        size_t end = result.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_", start + 1);
        if (end == std::string::npos) end = result.length();
        
        std::string varName = result.substr(start + 1, end - start - 1);
        const char* varValue = getenv(varName.c_str());
        
        result.replace(start, end - start, varValue ? varValue : "");
        start += varValue ? strlen(varValue) : 0;
    }
#endif
    
    return result;
}

std::string PathUtils::expandUserHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    
    std::string homeDir = SystemInfo::getUserHomeDirectory();
    if (path.length() == 1) return homeDir;
    if (path[1] == '/' || path[1] == '\\') {
        return join(homeDir, path.substr(2));
    }
    
    return path;
}

std::string PathUtils::getCanonicalPath(const std::string& path) {
    try {
        return std::filesystem::canonical(path).string();
    } catch (...) {
        return makeAbsolute(path);
    }
}

bool PathUtils::createDirectory(const std::string& path, bool recursive) {
    try {
        if (recursive) {
            return std::filesystem::create_directories(path);
        } else {
            return std::filesystem::create_directory(path);
        }
    } catch (...) {
        return false;
    }
}

char PathUtils::getPathSeparator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

const char* PathUtils::getPathSeparatorString() {
#ifdef _WIN32
    return "\\";
#else
    return "/";
#endif
}

// SystemInfo implementation
Platform SystemInfo::getCurrentPlatform() {
#ifdef _WIN32
    return Platform::WINDOWS;
#elif defined(__linux__)
    return Platform::LINUX;
#elif defined(__APPLE__)
    return Platform::MACOS;
#else
    return Platform::UNKNOWN;
#endif
}

std::string SystemInfo::getPlatformName() {
    switch (getCurrentPlatform()) {
        case Platform::WINDOWS: return "Windows";
        case Platform::LINUX: return "Linux";
        case Platform::MACOS: return "macOS";
        default: return "Unknown";
    }
}

std::string SystemInfo::getOSVersion() {
#ifdef _WIN32
    OSVERSIONINFOW osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOW));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    
    if (GetVersionExW(&osvi)) {
        std::ostringstream oss;
        oss << osvi.dwMajorVersion << "." << osvi.dwMinorVersion 
            << " Build " << osvi.dwBuildNumber;
        return oss.str();
    }
    return "Unknown Windows Version";
#else
    struct utsname unameData;
    if (uname(&unameData) == 0) {
        return std::string(unameData.sysname) + " " + unameData.release;
    }
    return "Unknown Unix Version";
#endif
}

bool SystemInfo::isWindows() {
    return getCurrentPlatform() == Platform::WINDOWS;
}

bool SystemInfo::isLinux() {
    return getCurrentPlatform() == Platform::LINUX;
}

bool SystemInfo::isMacOS() {
    return getCurrentPlatform() == Platform::MACOS;
}

std::string SystemInfo::getExecutablePath() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD result = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
        return UnicodeUtils::wideStringToUtf8(buffer);
    }
#else
    char buffer[PATH_MAX];
    ssize_t result = readlink("/proc/self/exe", buffer, PATH_MAX - 1);
    if (result != -1) {
        buffer[result] = '\0';
        return std::string(buffer);
    }
#endif
    return "";
}

std::string SystemInfo::getExecutableDirectory() {
    std::string exePath = getExecutablePath();
    if (!exePath.empty()) {
        return PathUtils::getDirectory(exePath);
    }
    return getCurrentWorkingDirectory();
}

std::string SystemInfo::getCurrentWorkingDirectory() {
    try {
        return std::filesystem::current_path().string();
    } catch (...) {
        return "";
    }
}

std::string SystemInfo::getUserHomeDirectory() {
#ifdef _WIN32
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) return std::string(userProfile);
    return "C:\\";
#else
    const char* home = getenv("HOME");
    if (home) return home;
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    
    return "/tmp";
#endif
}

std::string SystemInfo::getTempDirectory() {
    try {
        return std::filesystem::temp_directory_path().string();
    } catch (...) {
#ifdef _WIN32
        return "C:\\Temp";
#else
        return "/tmp";
#endif
    }
}

std::string SystemInfo::getEnvironmentVariable(const std::string& name) {
    const char* value = getenv(name.c_str());
    return value ? std::string(value) : "";
}

bool SystemInfo::setEnvironmentVariable(const std::string& name, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

std::string SystemInfo::getCurrentUserName() {
#ifdef _WIN32
    wchar_t username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    if (GetUserNameW(username, &username_len)) {
        return UnicodeUtils::wideStringToUtf8(username);
    }
#else
    const char* username = getenv("USER");
    if (username) return username;
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_name;
#endif
    return "Unknown";
}

bool SystemInfo::isRunningAsAdministrator() {
#ifdef _WIN32
    BOOL isAdmin = FALSE;
    PSID administratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                &administratorsGroup)) {
        CheckTokenMembership(NULL, administratorsGroup, &isAdmin);
        FreeSid(administratorsGroup);
    }
    
    return isAdmin;
#else
    return getuid() == 0;
#endif
}

// ProcessUtils implementation
uint32_t ProcessUtils::getCurrentProcessId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

std::string ProcessUtils::getCurrentProcessName() {
    std::string exePath = SystemInfo::getExecutablePath();
    return PathUtils::getFileName(exePath);
}

std::string ProcessUtils::getCurrentProcessPath() {
    return SystemInfo::getExecutablePath();
}

// FileSystemUtils implementation
bool FileSystemUtils::hasReadPermission(const std::string& path) {
#ifdef _WIN32
    return _access(path.c_str(), 4) == 0;
#else
    return access(path.c_str(), R_OK) == 0;
#endif
}

bool FileSystemUtils::hasWritePermission(const std::string& path) {
#ifdef _WIN32
    return _access(path.c_str(), 2) == 0;
#else
    return access(path.c_str(), W_OK) == 0;
#endif
}

bool FileSystemUtils::hasExecutePermission(const std::string& path) {
#ifdef _WIN32
    return PathUtils::isExecutable(path);
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}

uint64_t FileSystemUtils::getFreeDiskSpace(const std::string& path) {
    try {
        std::filesystem::space_info spaceInfo = std::filesystem::space(path);
        return spaceInfo.available;
    } catch (...) {
        return 0;
    }
}

// More PathUtils implementations
bool PathUtils::removeDirectory(const std::string& path, bool recursive) {
    try {
        if (recursive) {
            return std::filesystem::remove_all(path) > 0;
        } else {
            return std::filesystem::remove(path);
        }
    } catch (...) {
        return false;
    }
}

std::vector<std::string> PathUtils::listDirectory(const std::string& path) {
    std::vector<std::string> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            files.push_back(entry.path().filename().string());
        }
    } catch (...) {
        // Return empty vector on error
    }
    return files;
}

bool PathUtils::copyFile(const std::string& source, const std::string& destination) {
    try {
        return std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        return false;
    }
}

bool PathUtils::moveFile(const std::string& source, const std::string& destination) {
    try {
        std::filesystem::rename(source, destination);
        return true;
    } catch (...) {
        return false;
    }
}

bool PathUtils::deleteFile(const std::string& path) {
    try {
        return std::filesystem::remove(path);
    } catch (...) {
        return false;
    }
}

uint64_t PathUtils::getFileSize(const std::string& path) {
    try {
        return std::filesystem::file_size(path);
    } catch (...) {
        return 0;
    }
}

std::chrono::system_clock::time_point PathUtils::getLastModified(const std::string& path) {
    try {
        auto ftime = std::filesystem::last_write_time(path);
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::system_clock::now() + (ftime - std::filesystem::file_time_type::clock::now())
        );
    } catch (...) {
        return std::chrono::system_clock::time_point{};
    }
}

// More SystemInfo implementations
std::string SystemInfo::getUserDocumentsDirectory() {
#ifdef _WIN32
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        return std::string(userProfile) + "\\Documents";
    }
    return getUserHomeDirectory() + "\\Documents";
#else
    return getUserHomeDirectory() + "/Documents";
#endif
}

std::string SystemInfo::getSystemDirectory() {
#ifdef _WIN32
    wchar_t systemDir[MAX_PATH];
    if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0) {
        return UnicodeUtils::wideStringToUtf8(systemDir);
    }
    return "C:\\Windows\\System32";
#else
    return "/usr/bin";
#endif
}

std::vector<std::pair<std::string, std::string>> SystemInfo::getAllEnvironmentVariables() {
    std::vector<std::pair<std::string, std::string>> vars;
#ifdef _WIN32
    LPCH envStrings = GetEnvironmentStrings();
    if (envStrings) {
        LPTSTR var = (LPTSTR)envStrings;
        while (*var) {
            std::string envVar(var);
            size_t pos = envVar.find('=');
            if (pos != std::string::npos) {
                vars.emplace_back(envVar.substr(0, pos), envVar.substr(pos + 1));
            }
            var += envVar.length() + 1;
        }
        FreeEnvironmentStrings(envStrings);
    }
#else
    extern char **environ;
    for (char **env = environ; *env != nullptr; env++) {
        std::string envVar(*env);
        size_t pos = envVar.find('=');
        if (pos != std::string::npos) {
            vars.emplace_back(envVar.substr(0, pos), envVar.substr(pos + 1));
        }
    }
#endif
    return vars;
}

std::string SystemInfo::getCurrentUserFullName() {
#ifdef _WIN32
    char fullName[UNLEN + 1];
    DWORD fullName_len = UNLEN + 1;
    if (GetUserNameA(fullName, &fullName_len)) {
        return std::string(fullName);
    }
#else
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_gecos) {
        std::string gecos(pw->pw_gecos);
        size_t pos = gecos.find(',');
        if (pos != std::string::npos) {
            return gecos.substr(0, pos);
        }
        return gecos;
    }
#endif
    return getCurrentUserName();
}

uint64_t SystemInfo::getTotalMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullTotalPhys;
    }
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return pages * page_size;
    }
#endif
    return 0;
}

uint64_t SystemInfo::getAvailableMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullAvailPhys;
    }
#else
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return pages * page_size;
    }
#endif
    return 0;
}

int SystemInfo::getProcessorCount() {
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

double SystemInfo::getCPUUsage() {
    // This is a simplified implementation
    // A full implementation would require monitoring over time
    return 0.0;
}

std::string SystemInfo::getComputerName() {
#ifdef _WIN32
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(wchar_t);
    if (GetComputerNameW(computerName, &size)) {
        return UnicodeUtils::wideStringToUtf8(computerName);
    }
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
#endif
    return "Unknown";
}

std::vector<std::string> SystemInfo::getNetworkInterfaces() {
    std::vector<std::string> interfaces;
    // Simplified implementation - could be enhanced
#ifdef _WIN32
    // Windows implementation would use GetAdaptersInfo
    interfaces.push_back("Ethernet");
    interfaces.push_back("Wi-Fi");
#else
    // Unix implementation would use getifaddrs
    interfaces.push_back("eth0");
    interfaces.push_back("wlan0");
#endif
    return interfaces;
}

std::string SystemInfo::getLocalIPAddress() {
    return "127.0.0.1"; // Simplified implementation
}

// ProcessUtils implementations
uint32_t ProcessUtils::getParentProcessId() {
#ifdef _WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        DWORD currentPid = GetCurrentProcessId();
        
        if (Process32First(snapshot, &pe)) {
            do {
                if (pe.th32ProcessID == currentPid) {
                    CloseHandle(snapshot);
                    return pe.th32ParentProcessID;
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return 0;
#else
    return getppid();
#endif
}

std::vector<ProcessUtils::ProcessInfo> ProcessUtils::getRunningProcesses() {
    std::vector<ProcessInfo> processes;
    // Simplified implementation
    return processes;
}

std::vector<ProcessUtils::ProcessInfo> ProcessUtils::findProcessesByName(const std::string& name) {
    std::vector<ProcessInfo> processes;
    // Simplified implementation
    // TODO: Implement process search by name
    (void)name; // Suppress unused parameter warning
    return processes;
}

ProcessUtils::ProcessInfo ProcessUtils::getProcessInfo(uint32_t processId) {
    ProcessInfo info{};
    info.processId = processId;
    return info;
}

bool ProcessUtils::terminateProcess(uint32_t processId) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (process) {
        BOOL result = TerminateProcess(process, 1);
        CloseHandle(process);
        return result;
    }
    return false;
#else
    return kill(processId, SIGTERM) == 0;
#endif
}

bool ProcessUtils::isProcessRunning(uint32_t processId) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (process) {
        DWORD exitCode;
        BOOL result = GetExitCodeProcess(process, &exitCode);
        CloseHandle(process);
        return result && exitCode == STILL_ACTIVE;
    }
    return false;
#else
    return kill(processId, 0) == 0;
#endif
}

bool ProcessUtils::isProcessRunning(const std::string& processName) {
    auto processes = findProcessesByName(processName);
    return !processes.empty();
}

uint32_t ProcessUtils::launchProcess(const std::string& executablePath, 
                                   const std::vector<std::string>& arguments,
                                   const std::string& workingDirectory,
                                   bool elevated,
                                   bool waitForExit) {
    (void)elevated; // TODO: Implement elevated process launch
#ifdef _WIN32
    std::string cmdLine = executablePath;
    for (const auto& arg : arguments) {
        cmdLine += " \"" + arg + "\"";
    }
    
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    
    std::wstring wCmdLine = UnicodeUtils::utf8ToWideString(cmdLine);
    std::wstring wWorkDir = workingDirectory.empty() ? L"" : UnicodeUtils::utf8ToWideString(workingDirectory);
    const wchar_t* workDirPtr = workingDirectory.empty() ? nullptr : wWorkDir.c_str();
    
    if (CreateProcessW(nullptr, const_cast<wchar_t*>(wCmdLine.c_str()), nullptr, nullptr, 
                      FALSE, 0, nullptr, workDirPtr, &si, &pi)) {
        if (waitForExit) {
            WaitForSingleObject(pi.hProcess, INFINITE);
        }
        DWORD processId = pi.dwProcessId;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return processId;
    }
    return 0;
#else
    // Unix implementation would use fork/exec
    (void)executablePath;
    (void)arguments;
    (void)workingDirectory;
    (void)waitForExit;
    return 0;
#endif
}

// Additional FileSystemUtils implementations
bool FileSystemUtils::setFilePermissions(const std::string& path, int permissions) {
#ifdef _WIN32
    // Windows doesn't use Unix-style permissions
    (void)path; // Suppress unused parameter warning on Windows
    (void)permissions; // Suppress unused parameter warning on Windows
    return true;
#else
    return chmod(path.c_str(), permissions) == 0;
#endif
}

bool FileSystemUtils::isHidden(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_HIDDEN);
#else
    std::string filename = PathUtils::getFileName(path);
    return !filename.empty() && filename[0] == '.';
#endif
}

bool FileSystemUtils::isReadOnly(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_READONLY);
#else
    return !hasWritePermission(path);
#endif
}

bool FileSystemUtils::isSystemFile(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_SYSTEM);
#else
    (void)path;
    return false; // No direct equivalent on Unix
#endif
}

bool FileSystemUtils::setHidden(const std::string& path, bool hidden) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    
    if (hidden) {
        attrs |= FILE_ATTRIBUTE_HIDDEN;
    } else {
        attrs &= ~FILE_ATTRIBUTE_HIDDEN;
    }
    return SetFileAttributesA(path.c_str(), attrs);
#else
    // Unix doesn't have a hidden attribute, files starting with . are hidden
    (void)path;
    (void)hidden;
    return true;
#endif
}

bool FileSystemUtils::setReadOnly(const std::string& path, bool readOnly) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    
    if (readOnly) {
        attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    return SetFileAttributesA(path.c_str(), attrs);
#else
    mode_t mode = readOnly ? 0444 : 0644;
    return chmod(path.c_str(), mode) == 0;
#endif
}

uint64_t FileSystemUtils::getTotalDiskSpace(const std::string& path) {
    try {
        std::filesystem::space_info spaceInfo = std::filesystem::space(path);
        return spaceInfo.capacity;
    } catch (...) {
        return 0;
    }
}

std::string FileSystemUtils::getDiskVolumeName(const std::string& path) {
#ifdef _WIN32
    wchar_t wVolumeNameW[MAX_PATH + 1];
    wchar_t wRootPath[4] = L"C:\\";
    if (!path.empty()) {
        wRootPath[0] = path[0];
    }
    
    if (GetVolumeInformationW(wRootPath, wVolumeNameW, sizeof(wVolumeNameW) / sizeof(wchar_t), 
                             nullptr, nullptr, nullptr, nullptr, 0)) {
        return UnicodeUtils::wideStringToUtf8(wVolumeNameW);
    }
#else
    (void)path;
#endif
    return "Unknown";
}

bool FileSystemUtils::watchDirectory(const std::string& path, 
                                    std::function<void(const std::string&, const std::string&)> callback) {
    // Simplified implementation - would need platform-specific file watching
    (void)path; // TODO: Implement directory watching
    (void)callback; // TODO: Implement directory watching
    return false;
}

void FileSystemUtils::stopWatchingDirectory(const std::string& path) {
    // Implementation for stopping file watching
    (void)path; // TODO: Implement directory watching
}

// ErrorUtils implementation
std::string ErrorUtils::getLastSystemError() {
#ifdef _WIN32
    DWORD errorCode = GetLastError();
    return formatSystemError(errorCode);
#else
    return strerror(errno);
#endif
}

std::string ErrorUtils::formatSystemError(int errorCode) {
#ifdef _WIN32
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
#else
    return strerror(errorCode);
#endif
}

void ErrorUtils::logSystemError(const std::string& operation) {
    (void)operation; // TODO: Use operation in error logging
    std::string error = getLastSystemError();
    // Would normally log this, but avoiding circular dependency with logger
    // Logger::log(LogLevel::ERROR_LEVEL, operation + " failed: " + error);
}

bool ErrorUtils::isNetworkError(int errorCode) {
#ifdef _WIN32
    return (errorCode == WSAECONNRESET || errorCode == WSAECONNABORTED || 
            errorCode == WSAENETDOWN || errorCode == WSAENETUNREACH);
#else
    return (errorCode == ENETDOWN || errorCode == ENETUNREACH || 
            errorCode == ECONNRESET || errorCode == ECONNABORTED);
#endif
}

bool ErrorUtils::isPermissionError(int errorCode) {
#ifdef _WIN32
    return (errorCode == ERROR_ACCESS_DENIED || errorCode == ERROR_PRIVILEGE_NOT_HELD);
#else
    return (errorCode == EACCES || errorCode == EPERM);
#endif
}

bool ErrorUtils::isFileNotFoundError(int errorCode) {
#ifdef _WIN32
    return (errorCode == ERROR_FILE_NOT_FOUND || errorCode == ERROR_PATH_NOT_FOUND);
#else
    return (errorCode == ENOENT);
#endif
}

// ConsoleUtils implementation
std::string ConsoleUtils::getConsoleTitle() {
#ifdef _WIN32
    wchar_t title[MAX_PATH];
    if (GetConsoleTitleW(title, MAX_PATH) > 0) {
        return UnicodeUtils::wideStringToUtf8(title);
    }
#endif
    return "";
}

bool ConsoleUtils::isConsoleApplication() {
#ifdef _WIN32
    return GetConsoleWindow() != NULL;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::pair<int, int> ConsoleUtils::getConsoleSize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return {csbi.srWindow.Right - csbi.srWindow.Left + 1, 
                csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return {w.ws_col, w.ws_row};
    }
#endif
    return {80, 25}; // Default console size
}

bool ConsoleUtils::setConsoleSize(int width, int height) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD newSize = {static_cast<SHORT>(width), static_cast<SHORT>(height)};
    return SetConsoleScreenBufferSize(hConsole, newSize);
#else
    // Unix terminals typically can't be resized programmatically
    (void)width;
    (void)height;
    return false;
#endif
}

std::pair<int, int> ConsoleUtils::getCursorPosition() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return {csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y};
    }
#else
    // Unix implementation would be more complex
    return {0, 0};
#endif
    return {0, 0};
}

bool ConsoleUtils::setCursorPosition(int x, int y) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    return SetConsoleCursorPosition(hConsole, pos);
#else
    std::cout << "\033[" << y << ";" << x << "H";
    return true;
#endif
}

#ifdef _WIN32
// Unicode/UTF-8 conversion utilities implementation
std::string UnicodeUtils::wideStringToUtf8(const wchar_t* wideStr) {
    if (!wideStr) return "";
    
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return "";
    
    std::vector<char> utf8Buffer(utf8Length);
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8Buffer.data(), utf8Length, nullptr, nullptr);
    return std::string(utf8Buffer.data());
}

std::string UnicodeUtils::wideStringToUtf8(const std::wstring& wideStr) {
    return wideStringToUtf8(wideStr.c_str());
}

std::wstring UnicodeUtils::utf8ToWideString(const std::string& utf8Str) {
    return utf8ToWideString(utf8Str.c_str());
}

std::wstring UnicodeUtils::utf8ToWideString(const char* utf8Str) {
    if (!utf8Str) return L"";
    
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, nullptr, 0);
    if (wideLength <= 0) return L"";
    
    std::vector<wchar_t> wideBuffer(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideBuffer.data(), wideLength);
    return std::wstring(wideBuffer.data());
}
#endif

} // namespace os
} // namespace burwell