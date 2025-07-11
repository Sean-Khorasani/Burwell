#ifndef BURWELL_SYSTEM_COMMAND_H
#define BURWELL_SYSTEM_COMMAND_H

#include <string>
#include <map>
#include <vector>
#include <ctime>

namespace burwell {
namespace ocal {
namespace system {

struct CommandResult {
    bool success;
    int exitCode;
    std::string output;
    std::string error;
    std::string command;
    int executionTimeMs;
    
    CommandResult() : success(false), exitCode(-1), executionTimeMs(0) {}
};

/**
 * Execute shell command with full control over environment and capture
 * 
 * @param command The command to execute
 * @param workingDir Working directory for command execution (optional)
 * @param environment Environment variables to set (optional)
 * @param timeoutMs Maximum execution time in milliseconds
 * @param captureOutput Whether to capture stdout/stderr
 * @param showWindow Whether to show command window (Windows)
 * @param elevated Whether to run with elevated privileges (Windows)
 * @return CommandResult with execution details
 */
CommandResult executeCommand(const std::string& command, 
                           const std::string& workingDir = "",
                           const std::map<std::string, std::string>& environment = {},
                           int timeoutMs = 30000,
                           bool captureOutput = true,
                           bool showWindow = false,
                           bool elevated = false);

/**
 * Execute command asynchronously and return immediately
 * 
 * @param command The command to execute
 * @param workingDir Working directory for command execution (optional)
 * @param showWindow Whether to show command window
 * @return Process ID if successful, 0 if failed
 */
unsigned long executeCommandAsync(const std::string& command,
                                 const std::string& workingDir = "",
                                 bool showWindow = false);

/**
 * Check if a process is still running
 * 
 * @param processId Process ID to check
 * @return true if process is running, false otherwise
 */
bool isProcessRunning(unsigned long processId);

/**
 * Wait for process to complete
 * 
 * @param processId Process ID to wait for
 * @param timeoutMs Maximum wait time in milliseconds
 * @return CommandResult with process exit information
 */
CommandResult waitForProcess(unsigned long processId, int timeoutMs = 60000);

/**
 * Terminate a running process
 * 
 * @param processId Process ID to terminate
 * @param force Whether to force termination
 * @return true if successful, false otherwise
 */
bool terminateProcess(unsigned long processId, bool force = false);

/**
 * Get list of running processes
 * 
 * @param nameFilter Optional name filter (substring match)
 * @return Vector of process information
 */
struct ProcessInfo {
    unsigned long processId;
    std::string name;
    std::string commandLine;
    std::string workingDirectory;
    size_t memoryUsage;
    double cpuUsage;
};

std::vector<ProcessInfo> getRunningProcesses(const std::string& nameFilter = "");

/**
 * Check if current process has elevated privileges
 * 
 * @return true if running with admin/root privileges
 */
bool isElevated();

/**
 * Get current working directory
 * 
 * @return Current working directory path
 */
std::string getCurrentWorkingDirectory();

/**
 * Set current working directory
 * 
 * @param path New working directory path
 * @return true if successful, false otherwise
 */
bool setCurrentWorkingDirectory(const std::string& path);

// =================== ENHANCED AUTOMATION CAPABILITIES ===================

/**
 * PowerShell execution types
 */
enum class PowerShellMode {
    COMMAND,        // Single PowerShell command
    SCRIPT_FILE,    // Execute .ps1 script file
    SCRIPT_BLOCK,   // Execute script block
    MODULE_IMPORT   // Import module and execute
};

/**
 * Enhanced command execution with PowerShell support
 * 
 * @param command The command or script to execute
 * @param mode PowerShell execution mode
 * @param workingDir Working directory for execution
 * @param environment Environment variables to set
 * @param timeoutMs Maximum execution time in milliseconds
 * @param captureOutput Whether to capture stdout/stderr
 * @param elevated Whether to run with elevated privileges
 * @param executionPolicy PowerShell execution policy override
 * @return CommandResult with execution details
 */
CommandResult executePowerShellCommand(const std::string& command,
                                     PowerShellMode mode = PowerShellMode::COMMAND,
                                     const std::string& workingDir = "",
                                     const std::map<std::string, std::string>& environment = {},
                                     int timeoutMs = 30000,
                                     bool captureOutput = true,
                                     bool elevated = false,
                                     const std::string& executionPolicy = "Bypass");

/**
 * Execute command with automatic shell detection (CMD, PowerShell, Batch)
 * 
 * @param command The command to execute
 * @param workingDir Working directory for execution
 * @param environment Environment variables to set
 * @param timeoutMs Maximum execution time in milliseconds
 * @param captureOutput Whether to capture stdout/stderr
 * @param elevated Whether to run with elevated privileges
 * @return CommandResult with execution details
 */
CommandResult executeSmartCommand(const std::string& command,
                                const std::string& workingDir = "",
                                const std::map<std::string, std::string>& environment = {},
                                int timeoutMs = 30000,
                                bool captureOutput = true,
                                bool elevated = false);

/**
 * Execute multiple commands in sequence with error handling
 * 
 * @param commands Vector of commands to execute
 * @param workingDir Working directory for all commands
 * @param environment Environment variables to set
 * @param stopOnError Whether to stop on first error
 * @param timeoutMs Maximum execution time per command
 * @param elevated Whether to run with elevated privileges
 * @return Vector of CommandResults for each command
 */
std::vector<CommandResult> executeCommandChain(const std::vector<std::string>& commands,
                                             const std::string& workingDir = "",
                                             const std::map<std::string, std::string>& environment = {},
                                             bool stopOnError = true,
                                             int timeoutMs = 30000,
                                             bool elevated = false);

/**
 * Execute command with input/output redirection and piping
 * 
 * @param command The command with pipes and redirections
 * @param workingDir Working directory for execution
 * @param inputData Input data to send to command stdin
 * @param timeoutMs Maximum execution time in milliseconds
 * @param elevated Whether to run with elevated privileges
 * @return CommandResult with execution details
 */
CommandResult executeCommandWithPiping(const std::string& command,
                                      const std::string& workingDir = "",
                                      const std::string& inputData = "",
                                      int timeoutMs = 30000,
                                      bool elevated = false);

/**
 * Install and manage Windows features using DISM/PowerShell
 * 
 * @param featureName Windows feature name
 * @param enable true to enable, false to disable
 * @param includeManagementTools Include management tools
 * @return CommandResult with operation details
 */
CommandResult manageWindowsFeature(const std::string& featureName,
                                  bool enable = true,
                                  bool includeManagementTools = false);

/**
 * Package management operations (Chocolatey, WinGet, PowerShell Gallery)
 * 
 * @param packageManager Package manager to use (choco, winget, powershell)
 * @param operation Operation to perform (install, uninstall, update, search)
 * @param packageName Package name or search term
 * @param version Specific version to install (optional)
 * @param additionalArgs Additional arguments for package manager
 * @return CommandResult with operation details
 */
CommandResult executePackageOperation(const std::string& packageManager,
                                    const std::string& operation,
                                    const std::string& packageName,
                                    const std::string& version = "",
                                    const std::vector<std::string>& additionalArgs = {});

/**
 * Registry operations using PowerShell
 * 
 * @param operation Operation type (get, set, delete, create)
 * @param keyPath Registry key path
 * @param valueName Registry value name (optional for key operations)
 * @param valueData Data to set (for set operations)
 * @param valueType Registry value type (REG_SZ, REG_DWORD, etc.)
 * @return CommandResult with operation details
 */
CommandResult executeRegistryOperation(const std::string& operation,
                                     const std::string& keyPath,
                                     const std::string& valueName = "",
                                     const std::string& valueData = "",
                                     const std::string& valueType = "REG_SZ");

/**
 * Service management operations
 * 
 * @param serviceName Windows service name
 * @param operation Operation (start, stop, restart, enable, disable, install, uninstall)
 * @param serviceDisplayName Display name for install operation
 * @param servicePath Executable path for install operation
 * @return CommandResult with operation details
 */
CommandResult executeServiceOperation(const std::string& serviceName,
                                    const std::string& operation,
                                    const std::string& serviceDisplayName = "",
                                    const std::string& servicePath = "");

/**
 * Network operations and diagnostics
 * 
 * @param operation Operation type (ping, tracert, nslookup, netstat, ipconfig)
 * @param target Target hostname/IP for network operations
 * @param additionalArgs Additional arguments for the operation
 * @return CommandResult with operation details
 */
CommandResult executeNetworkOperation(const std::string& operation,
                                    const std::string& target = "",
                                    const std::vector<std::string>& additionalArgs = {});

/**
 * Execute script file with appropriate interpreter
 * 
 * @param scriptPath Path to script file (.ps1, .bat, .cmd, .py, etc.)
 * @param arguments Arguments to pass to script
 * @param workingDir Working directory for script execution
 * @param timeoutMs Maximum execution time in milliseconds
 * @param elevated Whether to run with elevated privileges
 * @return CommandResult with execution details
 */
CommandResult executeScriptFile(const std::string& scriptPath,
                               const std::vector<std::string>& arguments = {},
                               const std::string& workingDir = "",
                               int timeoutMs = 60000,
                               bool elevated = false);

/**
 * Check command availability and get version information
 * 
 * @param commandName Command or executable name
 * @return CommandResult with availability and version info
 */
CommandResult checkCommandAvailability(const std::string& commandName);

/**
 * Get system information using various Windows tools
 * 
 * @param infoType Type of information (hardware, software, network, performance)
 * @return CommandResult with system information
 */
CommandResult getSystemInformation(const std::string& infoType = "general");

} // namespace system
} // namespace ocal
} // namespace burwell

#endif // BURWELL_SYSTEM_COMMAND_H