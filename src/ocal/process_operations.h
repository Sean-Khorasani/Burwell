#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <string>
#include <map>

namespace ocal {
namespace atomic {
namespace process {

#ifdef _WIN32

/**
 * Launch an application
 * @param path Application path to launch
 * @param processInfo Output map containing process information
 * @return true if application launched successfully
 */
bool launchApplication(const std::string& path, std::map<std::string, std::string>& processInfo);

/**
 * Terminate a process by process ID
 * @param processId Process ID to terminate
 * @return true if process terminated successfully
 */
bool terminateProcess(unsigned long processId);

/**
 * Execute shell command/application
 * @param path Path to executable or command
 * @param operation Operation to perform ("open", "edit", "print", etc.)
 * @param parameters Command line parameters
 * @param directory Working directory
 * @param showCmd Show command flag
 * @param executeInfo Output map containing execution information
 * @return true if shell execute successful
 */
bool shellExecute(const std::string& path, const std::string& operation, 
                  const std::string& parameters, const std::string& directory, 
                  int showCmd, std::map<std::string, std::string>& executeInfo);

#endif // _WIN32

} // namespace process
} // namespace atomic
} // namespace ocal