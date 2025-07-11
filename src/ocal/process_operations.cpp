#include "process_operations.h"
#include "../common/structured_logger.h"

namespace ocal {
namespace atomic {
namespace process {

#ifdef _WIN32

bool launchApplication(const std::string& path, std::map<std::string, std::string>& processInfo) {
    std::wstring wPath(path.begin(), path.end());
    
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    
    if (CreateProcessW(wPath.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        processInfo["processId"] = std::to_string(static_cast<unsigned long>(pi.dwProcessId));
        processInfo["threadId"] = std::to_string(static_cast<unsigned long>(pi.dwThreadId));
        processInfo["processHandle"] = std::to_string(reinterpret_cast<uintptr_t>(pi.hProcess));
        processInfo["threadHandle"] = std::to_string(reinterpret_cast<uintptr_t>(pi.hThread));
        processInfo["path"] = path;
        
        // Close handles to avoid resource leaks
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        SLOG_DEBUG().message("[ATOMIC] Application launched").context("path", path).context("processId", processInfo["processId"]);
        return true;
    }
    
    DWORD error = GetLastError();
    SLOG_ERROR().message("[ATOMIC] Failed to launch application").context("path", path).context("error", error);
    return false;
}

bool terminateProcess(unsigned long processId) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    
    if (hProcess) {
        BOOL success = TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        
        if (success) {
            SLOG_DEBUG().message("[ATOMIC] Process terminated").context("processId", processId);
            return true;
        }
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to terminate process").context("processId", processId);
    return false;
}

bool shellExecute(const std::string& path, const std::string& operation,
                  const std::string& parameters, const std::string& directory,
                  int showCmd, std::map<std::string, std::string>& executeInfo) {
    
    std::wstring wPath(path.begin(), path.end());
    std::wstring wOperation(operation.begin(), operation.end());
    std::wstring wParameters(parameters.begin(), parameters.end());
    std::wstring wDirectory(directory.begin(), directory.end());
    
    HINSTANCE result = ShellExecuteW(
        NULL,
        wOperation.empty() ? NULL : wOperation.c_str(),
        wPath.c_str(),
        wParameters.empty() ? NULL : wParameters.c_str(),
        wDirectory.empty() ? NULL : wDirectory.c_str(),
        showCmd
    );
    
    uintptr_t resultCode = reinterpret_cast<uintptr_t>(result);
    
    if (resultCode > 32) {
        executeInfo["success"] = "true";
        executeInfo["path"] = path;
        executeInfo["operation"] = operation;
        executeInfo["parameters"] = parameters;
        executeInfo["directory"] = directory;
        executeInfo["showCmd"] = std::to_string(showCmd);
        
        SLOG_DEBUG().message("[ATOMIC] Shell execute successful").context("path", path);
        return true;
    }
    
    executeInfo["success"] = "false";
    executeInfo["errorCode"] = std::to_string(resultCode);
    
    SLOG_ERROR().message("[ATOMIC] Shell execute failed").context("path", path).context("error", resultCode);
    return false;
}

#endif // _WIN32

} // namespace process
} // namespace atomic
} // namespace ocal