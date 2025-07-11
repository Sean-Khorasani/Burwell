#include "system_command.h"
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include "../common/os_utils.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#pragma comment(lib, "psapi.lib")
#undef ERROR  // Undefine Windows ERROR macro to avoid conflict with LogLevel::ERROR_LEVEL
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <dirent.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#endif

using namespace burwell;

namespace burwell {
namespace ocal {
namespace system {

CommandResult executeCommand(const std::string& command, 
                           const std::string& workingDir,
                           const std::map<std::string, std::string>& environment,
                           int timeoutMs,
                           bool captureOutput,
                           bool showWindow,
                           bool elevated) {
    (void)environment; // TODO: Implement environment variable support
    
    CommandResult result;
    result.command = command;
    auto startTime = std::chrono::steady_clock::now();
    
    // Validate command
    auto commandValidation = burwell::InputValidator::validateCommand(command);
    if (!commandValidation.isValid) {
        result.error = "Invalid command: " + commandValidation.errorMessage;
        SLOG_ERROR().message("Invalid command")
            .context("error", commandValidation.errorMessage);
        return result;
    }
    
    // Validate working directory if provided
    if (!workingDir.empty()) {
        auto dirValidation = burwell::InputValidator::validateFilePath(workingDir);
        if (!dirValidation.isValid) {
            result.error = "Invalid working directory: " + dirValidation.errorMessage;
            SLOG_ERROR().message("Invalid working directory")
                .context("error", dirValidation.errorMessage);
            return result;
        }
    }
    
    // Validate timeout
    if (timeoutMs <= 0 || timeoutMs > 3600000) { // Max 1 hour
        result.error = "Invalid timeout value. Must be between 1 and 3600000 ms";
        SLOG_ERROR().message("Invalid timeout value")
            .context("timeout_ms", timeoutMs);
        return result;
    }
    
    SLOG_DEBUG().message("Executing command")
        .context("command", command);
    if (!workingDir.empty()) {
        SLOG_DEBUG().message("Working directory")
            .context("dir", workingDir);
    }
    
    try {
        
#ifdef _WIN32
        // Windows implementation using CreateProcess
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        ZeroMemory(&pi, sizeof(pi));
        si.cb = sizeof(si);
        
        // Set up pipes for output capture
        HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
        HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;
        
        if (captureOutput) {
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;
            
            if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0) ||
                !CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) {
                result.error = "Failed to create pipes for output capture";
                return result;
            }
            
            SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
            
            si.hStdOutput = hStdOutWrite;
            si.hStdError = hStdErrWrite;
            si.dwFlags |= STARTF_USESTDHANDLES;
        }
        
        if (!showWindow) {
            si.dwFlags |= STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
        }
        
        // Build command line with cmd.exe wrapper
        std::string cmdLine = "cmd.exe /C \"" + command + "\"";
        std::wstring wCmdLine = burwell::os::UnicodeUtils::utf8ToWideString(cmdLine);
        
        // Set working directory
        std::wstring wWorkDir = workingDir.empty() ? L"" : burwell::os::UnicodeUtils::utf8ToWideString(workingDir);
        const wchar_t* workDirPtr = workingDir.empty() ? nullptr : wWorkDir.c_str();
        
        // Create process
        BOOL success;
        if (elevated) {
            // Use ShellExecuteEx for elevated execution
            SHELLEXECUTEINFOW shExInfo = {};
            shExInfo.cbSize = sizeof(shExInfo);
            shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
            shExInfo.hwnd = nullptr;
            shExInfo.lpVerb = L"runas";
            shExInfo.lpFile = L"cmd.exe";
            std::wstring wParams = burwell::os::UnicodeUtils::utf8ToWideString("/C \"" + command + "\"");
            shExInfo.lpParameters = wParams.c_str();
            shExInfo.lpDirectory = workDirPtr;
            shExInfo.nShow = showWindow ? SW_NORMAL : SW_HIDE;
            
            success = ShellExecuteExW(&shExInfo);
            if (success) {
                pi.hProcess = shExInfo.hProcess;
                pi.dwProcessId = GetProcessId(shExInfo.hProcess);
            }
        } else {
            success = CreateProcessW(
                nullptr,
                const_cast<wchar_t*>(wCmdLine.c_str()),
                nullptr,
                nullptr,
                captureOutput ? TRUE : FALSE,
                0,
                nullptr,
                workDirPtr,
                &si,
                &pi
            );
        }
        
        if (captureOutput) {
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdErrWrite);
        }
        
        if (!success) {
            result.error = "Failed to create process. Error code: " + std::to_string(GetLastError());
            if (captureOutput) {
                CloseHandle(hStdOutRead);
                CloseHandle(hStdErrRead);
            }
            return result;
        }
        
        // Wait for process completion with timeout
        DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
        
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            result.error = "Command timed out after " + std::to_string(timeoutMs) + "ms";
            result.exitCode = -1;
        } else if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode;
            if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
                result.exitCode = static_cast<int>(exitCode);
                result.success = (exitCode == 0);
            }
        } else {
            result.error = "Error waiting for process completion";
        }
        
        // Read output if capturing
        if (captureOutput && (hStdOutRead || hStdErrRead)) {
            DWORD bytesRead;
            char buffer[4096];
            
            // Read stdout
            if (hStdOutRead) {
                std::string output;
                while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    output += buffer;
                }
                result.output = output;
                CloseHandle(hStdOutRead);
            }
            
            // Read stderr
            if (hStdErrRead) {
                std::string error;
                while (ReadFile(hStdErrRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    error += buffer;
                }
                if (!error.empty()) {
                    result.error += (result.error.empty() ? "" : "; ") + error;
                }
                CloseHandle(hStdErrRead);
            }
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
#else
        // Linux/Unix implementation using popen or system
        if (captureOutput) {
            std::string fullCommand = command;
            if (!workingDir.empty()) {
                fullCommand = "cd \"" + workingDir + "\" && " + command;
            }
            
            FILE* pipe = popen(fullCommand.c_str(), "r");
            if (!pipe) {
                result.error = "Failed to create pipe for command execution";
                return result;
            }
            
            char buffer[4096];
            std::string output;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                output += buffer;
            }
            
            int exitCode = pclose(pipe);
            result.exitCode = WEXITSTATUS(exitCode);
            result.success = (result.exitCode == 0);
            result.output = output;
        } else {
            std::string fullCommand = command;
            if (!workingDir.empty()) {
                fullCommand = "cd \"" + workingDir + "\" && " + command;
            }
            
            int exitCode = std::system(fullCommand.c_str());
            result.exitCode = WEXITSTATUS(exitCode);
            result.success = (result.exitCode == 0);
        }
#endif
        
    } catch (const std::exception& e) {
        result.error = "Exception during command execution: " + std::string(e.what());
        result.success = false;
    }
    
    auto endTime = std::chrono::steady_clock::now();
    result.executionTimeMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()
    );
    
    SLOG_DEBUG().message("Command completed")
        .context("success", result.success)
        .context("exit_code", result.exitCode)
        .context("execution_time_ms", result.executionTimeMs);
    
    if (!result.error.empty()) {
        SLOG_WARNING().message("Command error")
            .context("error", result.error);
    }
    
    return result;
}

unsigned long executeCommandAsync(const std::string& command,
                                 const std::string& workingDir,
                                 bool showWindow) {
    // Validate command
    auto commandValidation = burwell::InputValidator::validateCommand(command);
    if (!commandValidation.isValid) {
        SLOG_ERROR().message("Invalid command for async execution")
            .context("error", commandValidation.errorMessage);
        return 0;
    }
    
    // Validate working directory if provided
    if (!workingDir.empty()) {
        auto dirValidation = burwell::InputValidator::validateFilePath(workingDir);
        if (!dirValidation.isValid) {
            SLOG_ERROR().message("Invalid working directory for async execution")
                .context("error", dirValidation.errorMessage);
            return 0;
        }
    }
    
    SLOG_DEBUG().message("Executing command asynchronously")
        .context("command", command);
    
#ifdef _WIN32
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    
    if (!showWindow) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    
    std::string cmdLine = "cmd.exe /C \"" + command + "\"";
    std::wstring wCmdLine = burwell::os::UnicodeUtils::utf8ToWideString(cmdLine);
    std::wstring wWorkDir = workingDir.empty() ? L"" : burwell::os::UnicodeUtils::utf8ToWideString(workingDir);
    const wchar_t* workDirPtr = workingDir.empty() ? nullptr : wWorkDir.c_str();
    
    BOOL success = CreateProcessW(
        nullptr,
        const_cast<wchar_t*>(wCmdLine.c_str()),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workDirPtr,
        &si,
        &pi
    );
    
    if (success) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId;
    }
    
    return 0;
#else
    // Linux implementation using fork/exec
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (!workingDir.empty()) {
            chdir(workingDir.c_str());
        }
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        exit(1);
    } else if (pid > 0) {
        return static_cast<unsigned long>(pid);
    }
    
    return 0;
#endif
}

bool isProcessRunning(unsigned long processId) {
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (hProcess == nullptr) {
        return false;
    }
    
    DWORD exitCode;
    BOOL result = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);
    
    return result && (exitCode == STILL_ACTIVE);
#else
    return kill(static_cast<pid_t>(processId), 0) == 0;
#endif
}

CommandResult waitForProcess(unsigned long processId, int timeoutMs) {
    CommandResult result;
    result.command = "Wait for process " + std::to_string(processId);
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (hProcess == nullptr) {
        result.error = "Failed to open process for waiting";
        return result;
    }
    
    DWORD waitResult = WaitForSingleObject(hProcess, timeoutMs);
    
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode;
        if (GetExitCodeProcess(hProcess, &exitCode)) {
            result.exitCode = static_cast<int>(exitCode);
            result.success = true;
        }
    } else if (waitResult == WAIT_TIMEOUT) {
        result.error = "Process wait timed out";
    } else {
        result.error = "Error waiting for process";
    }
    
    CloseHandle(hProcess);
#else
    // Linux implementation
    int status;
    pid_t waitResult = waitpid(static_cast<pid_t>(processId), &status, 0);
    
    if (waitResult == processId) {
        result.exitCode = WEXITSTATUS(status);
        result.success = true;
    } else {
        result.error = "Failed to wait for process";
    }
#endif
    
    return result;
}

bool terminateProcess(unsigned long processId, bool force) {
    // Validate process ID
    if (processId == 0) {
        SLOG_ERROR().message("Invalid process ID for termination")
            .context("pid", processId);
        return false;
    }
    
    SLOG_WARNING().message("Terminating process")
        .context("pid", processId)
        .context("force", force);
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (hProcess == nullptr) {
        return false;
    }
    
    BOOL result = TerminateProcess(hProcess, force ? 1 : 0);
    CloseHandle(hProcess);
    return result != FALSE;
#else
    int signal = force ? SIGKILL : SIGTERM;
    return kill(static_cast<pid_t>(processId), signal) == 0;
#endif
}

std::vector<ProcessInfo> getRunningProcesses(const std::string& nameFilter) {
    std::vector<ProcessInfo> processes;
    
    // Validate name filter if provided
    if (!nameFilter.empty() && nameFilter.length() > 256) {
        SLOG_WARNING().message("Process name filter too long")
            .context("length", nameFilter.length())
            .context("max_length", 256);
        return processes;
    }
    
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }
    
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            std::wstring wname(pe32.szExeFile);
            std::string name(wname.begin(), wname.end());
            if (nameFilter.empty() || name.find(nameFilter) != std::string::npos) {
                
                ProcessInfo info;
                info.processId = pe32.th32ProcessID;
                info.name = name;
                
                // Try to get additional process information
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        info.memoryUsage = pmc.WorkingSetSize;
                    }
                    CloseHandle(hProcess);
                }
                
                processes.push_back(info);
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
#else
    // Linux implementation - read from /proc
    DIR* proc = opendir("/proc");
    if (proc) {
        struct dirent* entry;
        while ((entry = readdir(proc)) != nullptr) {
            if (strspn(entry->d_name, "0123456789") == strlen(entry->d_name)) {
                std::string statFile = "/proc/" + std::string(entry->d_name) + "/stat";
                std::ifstream file(statFile);
                if (file.is_open()) {
                    ProcessInfo info;
                    info.processId = std::stoul(entry->d_name);
                    
                    std::string line;
                    if (std::getline(file, line)) {
                        std::istringstream iss(line);
                        std::string pid, comm;
                        iss >> pid >> comm;
                        info.name = comm;
                        
                        if (nameFilter.empty() || info.name.find(nameFilter) != std::string::npos) {
                            processes.push_back(info);
                        }
                    }
                }
            }
        }
        closedir(proc);
    }
#endif
    
    return processes;
}

bool isElevated() {
#ifdef _WIN32
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    
    return isElevated != FALSE;
#else
    return geteuid() == 0;
#endif
}

std::string getCurrentWorkingDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD result = GetCurrentDirectoryA(MAX_PATH, buffer);
    if (result > 0 && result <= MAX_PATH) {
        return std::string(buffer);
    }
    return "";
#else
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer);
    }
    return "";
#endif
}

bool setCurrentWorkingDirectory(const std::string& path) {
    // Validate path
    auto validation = burwell::InputValidator::validateFilePath(path);
    if (!validation.isValid) {
        SLOG_ERROR().message("Invalid directory path")
            .context("error", validation.errorMessage);
        return false;
    }
    
#ifdef _WIN32
    return SetCurrentDirectoryA(path.c_str()) != FALSE;
#else
    return chdir(path.c_str()) == 0;
#endif
}

// =================== ENHANCED AUTOMATION CAPABILITIES ===================

CommandResult executePowerShellCommand(const std::string& command,
                                     PowerShellMode mode,
                                     const std::string& workingDir,
                                     const std::map<std::string, std::string>& environment,
                                     int timeoutMs,
                                     bool captureOutput,
                                     bool elevated,
                                     const std::string& executionPolicy) {
    
    std::string psCommand;
    
    switch (mode) {
        case PowerShellMode::COMMAND:
            psCommand = "powershell.exe -ExecutionPolicy " + executionPolicy + " -Command \"" + command + "\"";
            break;
            
        case PowerShellMode::SCRIPT_FILE:
            psCommand = "powershell.exe -ExecutionPolicy " + executionPolicy + " -File \"" + command + "\"";
            break;
            
        case PowerShellMode::SCRIPT_BLOCK:
            psCommand = "powershell.exe -ExecutionPolicy " + executionPolicy + " -Command \"& {" + command + "}\"";
            break;
            
        case PowerShellMode::MODULE_IMPORT:
            psCommand = "powershell.exe -ExecutionPolicy " + executionPolicy + " -Command \"Import-Module " + command + "\"";
            break;
    }
    
    SLOG_DEBUG().message("Executing PowerShell")
        .context("command", psCommand)
        .context("mode", static_cast<int>(mode));
    
    return executeCommand(psCommand, workingDir, environment, timeoutMs, captureOutput, false, elevated);
}

CommandResult executeSmartCommand(const std::string& command,
                                const std::string& workingDir,
                                const std::map<std::string, std::string>& environment,
                                int timeoutMs,
                                bool captureOutput,
                                bool elevated) {
    
    std::string lowerCommand = command;
    std::transform(lowerCommand.begin(), lowerCommand.end(), lowerCommand.begin(), ::tolower);
    
    // Detect PowerShell commands
    if (lowerCommand.find("get-") == 0 || lowerCommand.find("set-") == 0 || 
        lowerCommand.find("new-") == 0 || lowerCommand.find("remove-") == 0 ||
        lowerCommand.find("invoke-") == 0 || lowerCommand.find("start-") == 0 ||
        lowerCommand.find("stop-") == 0 || lowerCommand.find("restart-") == 0 ||
        lowerCommand.find("$") != std::string::npos || 
        lowerCommand.find("foreach") != std::string::npos ||
        lowerCommand.find("where-object") != std::string::npos) {
        
        SLOG_DEBUG().message("Detected PowerShell command, using PowerShell execution");
        return executePowerShellCommand(command, PowerShellMode::COMMAND, workingDir, environment, timeoutMs, captureOutput, elevated);
    }
    
    // Detect batch file execution
    if (lowerCommand.find(".bat") != std::string::npos || lowerCommand.find(".cmd") != std::string::npos) {
        SLOG_DEBUG().message("Detected batch file, using direct execution");
        return executeCommand(command, workingDir, environment, timeoutMs, captureOutput, false, elevated);
    }
    
    // Default to CMD execution
    SLOG_DEBUG().message("Using CMD execution for command");
    return executeCommand(command, workingDir, environment, timeoutMs, captureOutput, false, elevated);
}

std::vector<CommandResult> executeCommandChain(const std::vector<std::string>& commands,
                                             const std::string& workingDir,
                                             const std::map<std::string, std::string>& environment,
                                             bool stopOnError,
                                             int timeoutMs,
                                             bool elevated) {
    
    std::vector<CommandResult> results;
    SLOG_INFO().message("Executing command chain")
        .context("command_count", commands.size());
    
    for (size_t i = 0; i < commands.size(); ++i) {
        SLOG_DEBUG().message("Executing command in chain")
            .context("index", i + 1)
            .context("total", commands.size())
            .context("command", commands[i]);
        
        CommandResult result = executeSmartCommand(commands[i], workingDir, environment, timeoutMs, true, elevated);
        results.push_back(result);
        
        if (!result.success && stopOnError) {
            SLOG_ERROR().message("Command chain stopped due to error")
                .context("failed_index", i + 1)
                .context("error", result.error);
            break;
        }
        
        // Brief delay between commands
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return results;
}

CommandResult executeCommandWithPiping(const std::string& command,
                                      const std::string& workingDir,
                                      const std::string& inputData,
                                      int timeoutMs,
                                      bool elevated) {
    
    SLOG_DEBUG().message("Executing command with piping")
        .context("command", command);
    
    // For Windows, we'll use PowerShell for complex piping
    std::string psCommand = "powershell.exe -Command \"" + command + "\"";
    
    CommandResult result = executeCommand(psCommand, workingDir, {}, timeoutMs, true, false, elevated);
    
    // If we have input data, we need to handle it differently (would require process communication)
    if (!inputData.empty()) {
        SLOG_DEBUG().message("Input data provided for piped command")
            .context("data_length", inputData.length());
    }
    
    return result;
}

CommandResult manageWindowsFeature(const std::string& featureName,
                                  bool enable,
                                  bool includeManagementTools) {
    // Validate feature name
    if (!burwell::InputValidator::isNotEmpty(featureName)) {
        CommandResult result;
        result.error = "Empty feature name provided";
        SLOG_ERROR().message("Empty Windows feature name");
        return result;
    }
    
    std::string operation = enable ? "Enable-WindowsOptionalFeature" : "Disable-WindowsOptionalFeature";
    std::string psCommand = operation + " -Online -FeatureName " + featureName;
    
    if (enable && includeManagementTools) {
        psCommand += " -IncludeManagementTools";
    }
    
    SLOG_INFO().message(enable ? "Enabling Windows feature" : "Disabling Windows feature")
        .context("feature", featureName)
        .context("include_tools", includeManagementTools);
    
    return executePowerShellCommand(psCommand, PowerShellMode::COMMAND, "", {}, 300000, true, true);
}

CommandResult executePackageOperation(const std::string& packageManager,
                                    const std::string& operation,
                                    const std::string& packageName,
                                    const std::string& version,
                                    const std::vector<std::string>& additionalArgs) {
    // Validate inputs
    if (!burwell::InputValidator::isNotEmpty(packageManager) ||
        !burwell::InputValidator::isNotEmpty(operation) ||
        !burwell::InputValidator::isNotEmpty(packageName)) {
        CommandResult result;
        result.error = "Package manager, operation, and package name are required";
        SLOG_ERROR().message("Missing required package operation parameters");
        return result;
    }
    
    std::string command;
    
    if (packageManager == "choco" || packageManager == "chocolatey") {
        command = "choco " + operation + " " + packageName;
        if (!version.empty()) {
            command += " --version=" + version;
        }
        command += " -y"; // Auto-confirm
        
    } else if (packageManager == "winget") {
        command = "winget " + operation + " " + packageName;
        if (!version.empty()) {
            command += " --version " + version;
        }
        
    } else if (packageManager == "powershell" || packageManager == "ps") {
        if (operation == "install") {
            command = "Install-Module -Name " + packageName + " -Force -AllowClobber";
        } else if (operation == "uninstall") {
            command = "Uninstall-Module -Name " + packageName + " -Force";
        } else if (operation == "update") {
            command = "Update-Module -Name " + packageName + " -Force";
        }
        
        if (!version.empty()) {
            command += " -RequiredVersion " + version;
        }
    }
    
    // Add additional arguments
    for (const auto& arg : additionalArgs) {
        command += " " + arg;
    }
    
    SLOG_INFO().message("Package operation")
        .context("manager", packageManager)
        .context("operation", operation)
        .context("package", packageName)
        .context("version", version);
    
    // Package operations typically need elevation
    if (packageManager == "powershell" || packageManager == "ps") {
        return executePowerShellCommand(command, PowerShellMode::COMMAND, "", {}, 300000, true, true);
    } else {
        return executeCommand(command, "", {}, 300000, true, false, true);
    }
}

CommandResult executeRegistryOperation(const std::string& operation,
                                     const std::string& keyPath,
                                     const std::string& valueName,
                                     const std::string& valueData,
                                     const std::string& valueType) {
    (void)valueType; // TODO: Implement value type handling for registry operations
    // Validate operation and key path
    if (!burwell::InputValidator::isNotEmpty(operation) ||
        !burwell::InputValidator::isNotEmpty(keyPath)) {
        CommandResult result;
        result.error = "Registry operation and key path are required";
        SLOG_ERROR().message("Missing required registry operation parameters");
        return result;
    }
    
    // Validate operation type
    static const std::vector<std::string> validOps = {"get", "set", "delete", "create"};
    if (std::find(validOps.begin(), validOps.end(), operation) == validOps.end()) {
        CommandResult result;
        result.error = "Invalid registry operation. Must be: get, set, delete, or create";
        SLOG_ERROR().message("Invalid registry operation")
            .context("operation", operation);
        return result;
    }
    
    std::string psCommand;
    
    if (operation == "get") {
        if (valueName.empty()) {
            psCommand = "Get-ItemProperty -Path 'Registry::" + keyPath + "'";
        } else {
            psCommand = "Get-ItemPropertyValue -Path 'Registry::" + keyPath + "' -Name '" + valueName + "'";
        }
        
    } else if (operation == "set") {
        psCommand = "Set-ItemProperty -Path 'Registry::" + keyPath + "' -Name '" + valueName + "' -Value '" + valueData + "'";
        
    } else if (operation == "delete") {
        if (valueName.empty()) {
            psCommand = "Remove-Item -Path 'Registry::" + keyPath + "' -Force";
        } else {
            psCommand = "Remove-ItemProperty -Path 'Registry::" + keyPath + "' -Name '" + valueName + "' -Force";
        }
        
    } else if (operation == "create") {
        psCommand = "New-Item -Path 'Registry::" + keyPath + "' -Force";
    }
    
    SLOG_INFO().message("Registry operation")
        .context("operation", operation)
        .context("key_path", keyPath)
        .context("value_name", valueName);
    
    // Registry operations typically need elevation
    return executePowerShellCommand(psCommand, PowerShellMode::COMMAND, "", {}, 30000, true, true);
}

CommandResult executeServiceOperation(const std::string& serviceName,
                                    const std::string& operation,
                                    const std::string& serviceDisplayName,
                                    const std::string& servicePath) {
    // Validate service name and operation
    if (!burwell::InputValidator::isNotEmpty(serviceName) ||
        !burwell::InputValidator::isNotEmpty(operation)) {
        CommandResult result;
        result.error = "Service name and operation are required";
        SLOG_ERROR().message("Missing required service operation parameters");
        return result;
    }
    
    // Validate operation type
    static const std::vector<std::string> validOps = {"start", "stop", "restart", "enable", "disable", "install", "uninstall"};
    if (std::find(validOps.begin(), validOps.end(), operation) == validOps.end()) {
        CommandResult result;
        result.error = "Invalid service operation";
        SLOG_ERROR().message("Invalid service operation")
            .context("operation", operation);
        return result;
    }
    
    std::string command;
    
    if (operation == "start") {
        command = "Start-Service -Name '" + serviceName + "'";
    } else if (operation == "stop") {
        command = "Stop-Service -Name '" + serviceName + "' -Force";
    } else if (operation == "restart") {
        command = "Restart-Service -Name '" + serviceName + "' -Force";
    } else if (operation == "enable") {
        command = "Set-Service -Name '" + serviceName + "' -StartupType Automatic";
    } else if (operation == "disable") {
        command = "Set-Service -Name '" + serviceName + "' -StartupType Disabled";
    } else if (operation == "install") {
        command = "New-Service -Name '" + serviceName + "' -BinaryPathName '" + servicePath + "'";
        if (!serviceDisplayName.empty()) {
            command += " -DisplayName '" + serviceDisplayName + "'";
        }
    } else if (operation == "uninstall") {
        command = "Remove-Service -Name '" + serviceName + "'";
    }
    
    SLOG_INFO().message("Service operation")
        .context("operation", operation)
        .context("service", serviceName);
    
    // Service operations need elevation
    return executePowerShellCommand(command, PowerShellMode::COMMAND, "", {}, 60000, true, true);
}

CommandResult executeNetworkOperation(const std::string& operation,
                                    const std::string& target,
                                    const std::vector<std::string>& additionalArgs) {
    
    std::string command = operation;
    
    if (!target.empty()) {
        command += " " + target;
    }
    
    // Add additional arguments
    for (const auto& arg : additionalArgs) {
        command += " " + arg;
    }
    
    SLOG_DEBUG().message("Network operation")
        .context("command", command);
    
    return executeCommand(command, "", {}, 30000, true, false, false);
}

CommandResult executeScriptFile(const std::string& scriptPath,
                               const std::vector<std::string>& arguments,
                               const std::string& workingDir,
                               int timeoutMs,
                               bool elevated) {
    // Validate script path
    auto pathValidation = burwell::InputValidator::validateFilePath(scriptPath);
    if (!pathValidation.isValid) {
        CommandResult result;
        result.error = "Invalid script path: " + pathValidation.errorMessage;
        SLOG_ERROR().message("Invalid script path")
            .context("error", pathValidation.errorMessage);
        return result;
    }
    
    std::string extension = scriptPath.substr(scriptPath.find_last_of(".") + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    std::string command;
    
    if (extension == "ps1") {
        command = "powershell.exe -ExecutionPolicy Bypass -File \"" + scriptPath + "\"";
    } else if (extension == "bat" || extension == "cmd") {
        command = "\"" + scriptPath + "\"";
    } else if (extension == "py") {
        command = "python \"" + scriptPath + "\"";
    } else if (extension == "js") {
        command = "node \"" + scriptPath + "\"";
    } else {
        command = "\"" + scriptPath + "\"";
    }
    
    // Add arguments
    for (const auto& arg : arguments) {
        command += " \"" + arg + "\"";
    }
    
    SLOG_INFO().message("Executing script file")
        .context("path", scriptPath)
        .context("extension", extension)
        .context("arg_count", arguments.size());
    
    return executeCommand(command, workingDir, {}, timeoutMs, true, false, elevated);
}

CommandResult checkCommandAvailability(const std::string& commandName) {
    std::string command = "where " + commandName;
    CommandResult result = executeCommand(command, "", {}, 10000, true, false, false);
    
    if (!result.success) {
        // Try PowerShell Get-Command
        std::string psCommand = "Get-Command " + commandName + " -ErrorAction SilentlyContinue";
        result = executePowerShellCommand(psCommand, PowerShellMode::COMMAND, "", {}, 10000, true, false);
    }
    
    return result;
}

CommandResult getSystemInformation(const std::string& infoType) {
    std::string command;
    
    if (infoType == "hardware") {
        command = "Get-ComputerInfo | Select-Object TotalPhysicalMemory, CsProcessors, CsSystemType";
    } else if (infoType == "software") {
        command = "Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, WindowsBuildLabEx";
    } else if (infoType == "network") {
        command = "Get-NetAdapter | Where-Object Status -eq 'Up' | Select-Object Name, InterfaceDescription, LinkSpeed";
    } else if (infoType == "performance") {
        command = "Get-Counter '\\Processor(_Total)\\% Processor Time','\\Memory\\Available MBytes' -SampleInterval 1 -MaxSamples 1";
    } else {
        command = "systeminfo";
    }
    
    SLOG_DEBUG().message("Getting system information")
        .context("type", infoType);
    
    if (infoType != "general") {
        return executePowerShellCommand(command, PowerShellMode::COMMAND, "", {}, 30000, true, false);
    } else {
        return executeCommand(command, "", {}, 30000, true, false, false);
    }
}

} // namespace system
} // namespace ocal
} // namespace burwell