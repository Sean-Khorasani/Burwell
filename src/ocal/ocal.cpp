#include "ocal.h"
#include "../common/structured_logger.h"
#include "process_operations.h"

using namespace burwell;

// MouseControl implementation
bool MouseControl::click(int x, int y, const std::string& button) {
    try {
        ::ocal::mouse::MouseButton mouseButton = ::ocal::mouse::MouseButton::LEFT;
        if (button == "right") mouseButton = ::ocal::mouse::MouseButton::RIGHT;
        else if (button == "middle") mouseButton = ::ocal::mouse::MouseButton::MIDDLE;
        
        ::ocal::mouse::clickAt(x, y, mouseButton, ::ocal::mouse::ClickType::SINGLE);
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Mouse click failed").context("error", e.what());
        return false;
    }
}

bool MouseControl::moveTo(int x, int y) {
    try {
        ::ocal::mouse::move(x, y);
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Mouse move failed").context("error", e.what());
        return false;
    }
}

bool MouseControl::drag(int x1, int y1, int x2, int y2) {
    try {
        ::ocal::mouse::Point start{x1, y1};
        ::ocal::mouse::Point end{x2, y2};
        ::ocal::mouse::drag(start, end, ::ocal::mouse::MouseButton::LEFT);
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Mouse drag failed").context("error", e.what());
        return false;
    }
}

std::pair<int, int> MouseControl::getCurrentPosition() {
    try {
        auto pos = ::ocal::mouse::getPosition();
        return std::make_pair(pos.x, pos.y);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Get mouse position failed").context("error", e.what());
        return std::make_pair(0, 0);
    }
}

// KeyboardControl implementation
bool KeyboardControl::typeText(const std::string& text) {
    try {
        ::ocal::keyboard::type(text);
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Keyboard type failed").context("error", e.what());
        return false;
    }
}

bool KeyboardControl::sendHotkey(const std::vector<std::string>& keys) {
    try {
        // Convert string keys to Key enum (simplified mapping)
        ::ocal::keyboard::KeyCombination combo;
        for (const auto& key : keys) {
            if (key == "ctrl") combo.push_back(::ocal::keyboard::Key::CTRL_LEFT);
            else if (key == "alt") combo.push_back(::ocal::keyboard::Key::ALT_LEFT);
            else if (key == "shift") combo.push_back(::ocal::keyboard::Key::SHIFT_LEFT);
            else if (key == "c") combo.push_back(::ocal::keyboard::Key::C);
            else if (key == "v") combo.push_back(::ocal::keyboard::Key::V);
            // Add more mappings as needed
        }
        
        ::ocal::keyboard::hotkey(combo);
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Keyboard hotkey failed").context("error", e.what());
        return false;
    }
}

bool KeyboardControl::pressKey(const std::string& key) {
    try {
        // Simplified key mapping
        if (key == "enter") ::ocal::keyboard::tap(::ocal::keyboard::Key::ENTER);
        else if (key == "tab") ::ocal::keyboard::tap(::ocal::keyboard::Key::TAB);
        else if (key == "space") ::ocal::keyboard::tap(::ocal::keyboard::Key::SPACE);
        // Add more mappings as needed
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Key press failed").context("error", e.what());
        return false;
    }
}

// WindowManagement implementation
void* WindowManagement::findWindow(const std::string& title) {
    try {
#ifdef _WIN32
        return ::ocal::window::find(title);
#else
        ::ocal::window::WindowHandle handle = ::ocal::window::find(title);
        return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
#endif
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Find window failed").context("error", e.what());
        return nullptr;
    }
}

bool WindowManagement::activateWindow(void* handle) {
    try {
#ifdef _WIN32
        return ::ocal::window::activate(static_cast<::ocal::window::WindowHandle>(handle));
#else
        ::ocal::window::WindowHandle windowHandle = static_cast<::ocal::window::WindowHandle>(reinterpret_cast<uintptr_t>(handle));
        return ::ocal::window::activate(windowHandle);
#endif
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Activate window failed").context("error", e.what());
        return false;
    }
}

bool WindowManagement::resizeWindow(void* handle, int width, int height) {
    try {
#ifdef _WIN32
        return ::ocal::window::resize(static_cast<::ocal::window::WindowHandle>(handle), width, height);
#else
        ::ocal::window::WindowHandle windowHandle = static_cast<::ocal::window::WindowHandle>(reinterpret_cast<uintptr_t>(handle));
        return ::ocal::window::resize(windowHandle, width, height);
#endif
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Resize window failed").context("error", e.what());
        return false;
    }
}

// AppManagement implementation
bool AppManagement::launchApplication(const std::string& appName) {
    try {
#ifdef _WIN32
        std::map<std::string, std::string> processInfo;
        return ::ocal::atomic::process::launchApplication(appName, processInfo);
#else
        (void)appName;
        SLOG_WARNING().message("launchApplication not implemented for non-Windows platforms");
        return false;
#endif
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Launch application failed").context("error", e.what());
        return false;
    }
}

bool AppManagement::closeApplication(const std::string& appName) {
    (void)appName; // Deprecated function - use atomic operations
    try {
        // Note: This is a simplified implementation - finding by name requires enumeration
        // For now, return false as this functionality should use atomic operations + scripts
        SLOG_WARNING().message("closeApplication deprecated - use atomic operations with scripts");
        return false;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Close application failed").context("error", e.what());
        return false;
    }
}

unsigned long AppManagement::getProcessId(const std::string& appName) {
    (void)appName; // Deprecated function - use atomic window operations
    try {
        // Note: This is deprecated functionality - use atomic window enumeration instead
        SLOG_WARNING().message("getProcessId deprecated - use atomic window operations");
        return 0;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Get process ID failed").context("error", e.what());
        return 0;
    }
}

// ServiceManagement implementation
bool ServiceManagement::startService(const std::string& serviceName) {
    try {
        return ::ocal::service::start(serviceName);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Start service failed").context("error", e.what());
        return false;
    }
}

bool ServiceManagement::stopService(const std::string& serviceName) {
    try {
        return ::ocal::service::stop(serviceName);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Stop service failed").context("error", e.what());
        return false;
    }
}

bool ServiceManagement::restartService(const std::string& serviceName) {
    try {
        return ::ocal::service::restart(serviceName);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Restart service failed").context("error", e.what());
        return false;
    }
}

// SystemCommand implementation
SystemCommand::CommandResult SystemCommand::executeCommand(const std::string& command, 
                                                         const std::string& workingDir,
                                                         const std::map<std::string, std::string>& environment,
                                                         int timeoutMs,
                                                         bool captureOutput,
                                                         bool showWindow) {
    CommandResult result;
    try {
        auto ocalResult = ::burwell::ocal::system::executeCommand(command, workingDir, environment, timeoutMs, captureOutput, showWindow);
        
        result.success = ocalResult.success;
        result.exitCode = ocalResult.exitCode;
        result.output = ocalResult.output;
        result.error = ocalResult.error;
        result.command = ocalResult.command;
        
        return result;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("System command execution failed").context("error", e.what());
        result.error = e.what();
        return result;
    }
}

// FilesystemOperations implementation
bool FilesystemOperations::fileExists(const std::string& path) {
    try {
        return ::burwell::ocal::filesystem::fileExists(path);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("File exists check failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::directoryExists(const std::string& path) {
    try {
        return ::burwell::ocal::filesystem::directoryExists(path);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Directory exists check failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::copyFile(const std::string& source, const std::string& destination, bool overwrite) {
    try {
        return ::burwell::ocal::filesystem::copyFile(source, destination, overwrite);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("File copy failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::copyDirectory(const std::string& source, const std::string& destination, bool recursive, bool overwrite) {
    try {
        return ::burwell::ocal::filesystem::copyDirectory(source, destination, recursive, overwrite);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Directory copy failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::deleteFile(const std::string& path) {
    try {
        return ::burwell::ocal::filesystem::deleteFile(path);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("File delete failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::deleteDirectory(const std::string& path, bool recursive) {
    try {
        return ::burwell::ocal::filesystem::deleteDirectory(path, recursive);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Directory delete failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::createDirectory(const std::string& path) {
    try {
        return ::burwell::ocal::filesystem::createDirectory(path);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Directory create failed").context("error", e.what());
        return false;
    }
}

bool FilesystemOperations::waitForFile(const std::string& path, int timeoutMs, int checkIntervalMs) {
    try {
        return ::burwell::ocal::filesystem::waitForFile(path, timeoutMs, checkIntervalMs);
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Wait for file failed").context("error", e.what());
        return false;
    }
}

FilesystemOperations::FileInfo FilesystemOperations::getFileInfo(const std::string& path) {
    FileInfo info;
    try {
        auto ocalInfo = ::burwell::ocal::filesystem::getFileInfo(path);
        
        info.exists = ocalInfo.exists;
        info.isDirectory = ocalInfo.isDirectory;
        info.size = ocalInfo.size;
        info.lastModified = ocalInfo.lastModified;
        info.fullPath = ocalInfo.fullPath;
        
        return info;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Get file info failed").context("error", e.what());
        return info;
    }
}

// OCAL main class implementation
OCAL::OCAL() : m_initialized(false) {
    SLOG_INFO().message("OCAL initialized");
}

OCAL::~OCAL() {
    shutdown();
}

bool OCAL::initialize() {
    if (m_initialized) {
        return true;
    }
    
    try {
        // Initialize all control modules
        // Individual modules handle their own initialization
        m_initialized = true;
        SLOG_INFO().message("OCAL initialization completed successfully");
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("OCAL initialization failed").context("error", e.what());
        return false;
    }
}

void OCAL::shutdown() {
    if (m_initialized) {
        SLOG_INFO().message("OCAL shutting down");
        m_initialized = false;
    }
}


