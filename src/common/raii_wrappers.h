#ifndef BURWELL_RAII_WRAPPERS_H
#define BURWELL_RAII_WRAPPERS_H

#include <memory>
#include <functional>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace burwell {
namespace raii {

/**
 * @brief RAII wrapper for Windows handles
 * 
 * Automatically closes handle on destruction using appropriate close function
 */
template<typename HandleType, HandleType InvalidValue>
class HandleWrapper {
public:
    using Deleter = std::function<void(HandleType)>;
    
    HandleWrapper() : m_handle(InvalidValue), m_deleter(nullptr) {}
    
    explicit HandleWrapper(HandleType handle, Deleter deleter)
        : m_handle(handle), m_deleter(deleter) {
        if (!m_deleter) {
            throw std::invalid_argument("Deleter function cannot be null");
        }
    }
    
    // Move constructor
    HandleWrapper(HandleWrapper&& other) noexcept
        : m_handle(other.m_handle), m_deleter(std::move(other.m_deleter)) {
        other.m_handle = InvalidValue;
    }
    
    // Move assignment
    HandleWrapper& operator=(HandleWrapper&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            m_deleter = std::move(other.m_deleter);
            other.m_handle = InvalidValue;
        }
        return *this;
    }
    
    // Delete copy operations
    HandleWrapper(const HandleWrapper&) = delete;
    HandleWrapper& operator=(const HandleWrapper&) = delete;
    
    ~HandleWrapper() {
        reset();
    }
    
    void reset(HandleType handle = InvalidValue) {
        if (m_handle != InvalidValue && m_deleter) {
            m_deleter(m_handle);
        }
        m_handle = handle;
    }
    
    HandleType release() noexcept {
        HandleType temp = m_handle;
        m_handle = InvalidValue;
        return temp;
    }
    
    HandleType get() const noexcept {
        return m_handle;
    }
    
    bool isValid() const noexcept {
        return m_handle != InvalidValue;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
    
    HandleType operator*() const {
        if (!isValid()) {
            throw std::runtime_error("Dereferencing invalid handle");
        }
        return m_handle;
    }
    
private:
    HandleType m_handle;
    Deleter m_deleter;
};

#ifdef _WIN32

/**
 * @brief RAII wrapper for Windows HANDLE
 */
class WindowHandle {
public:
    WindowHandle() : m_handle(INVALID_HANDLE_VALUE) {}
    
    explicit WindowHandle(HANDLE handle) : m_handle(handle) {}
    
    ~WindowHandle() {
        reset();
    }
    
    // Move constructor
    WindowHandle(WindowHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = INVALID_HANDLE_VALUE;
    }
    
    // Move assignment
    WindowHandle& operator=(WindowHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    
    // Delete copy operations
    WindowHandle(const WindowHandle&) = delete;
    WindowHandle& operator=(const WindowHandle&) = delete;
    
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) {
        if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
            ::CloseHandle(m_handle);
        }
        m_handle = handle;
    }
    
    HANDLE release() noexcept {
        HANDLE temp = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return temp;
    }
    
    HANDLE get() const noexcept {
        return m_handle;
    }
    
    bool isValid() const noexcept {
        return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
    
    static WindowHandle create(HANDLE handle) {
        return WindowHandle(handle);
    }
    
private:
    HANDLE m_handle;
};

/**
 * @brief RAII wrapper for file handles
 */
class FileHandle {
public:
    FileHandle() : m_handle(INVALID_HANDLE_VALUE) {}
    
    explicit FileHandle(HANDLE handle) : m_handle(handle) {}
    
    ~FileHandle() {
        reset();
    }
    
    // Move constructor
    FileHandle(FileHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = INVALID_HANDLE_VALUE;
    }
    
    // Move assignment
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    
    // Delete copy operations
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) {
        if (m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
        m_handle = handle;
    }
    
    HANDLE release() noexcept {
        HANDLE temp = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return temp;
    }
    
    HANDLE get() const noexcept {
        return m_handle;
    }
    
    bool isValid() const noexcept {
        return m_handle != INVALID_HANDLE_VALUE;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
          
    static FileHandle openFile(const std::string& path, DWORD access, DWORD shareMode, DWORD creation) {
        HANDLE h = ::CreateFileA(path.c_str(), access, shareMode, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        return FileHandle(h);
    }
    
    bool read(void* buffer, DWORD size, DWORD* bytesRead) {
        return ::ReadFile(get(), buffer, size, bytesRead, nullptr) != 0;
    }
    
    bool write(const void* buffer, DWORD size, DWORD* bytesWritten) {
        return ::WriteFile(get(), buffer, size, bytesWritten, nullptr) != 0;
    }
    
private:
    HANDLE m_handle;
};

/**
 * @brief RAII wrapper for registry keys
 */
class RegistryHandle {
public:
    RegistryHandle() : m_handle(nullptr) {}
    
    explicit RegistryHandle(HKEY handle) : m_handle(handle) {}
    
    ~RegistryHandle() {
        reset();
    }
    
    // Move constructor
    RegistryHandle(RegistryHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }
    
    // Move assignment
    RegistryHandle& operator=(RegistryHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    RegistryHandle(const RegistryHandle&) = delete;
    RegistryHandle& operator=(const RegistryHandle&) = delete;
    
    void reset(HKEY handle = nullptr) {
        if (m_handle != nullptr) {
            ::RegCloseKey(m_handle);
        }
        m_handle = handle;
    }
    
    HKEY release() noexcept {
        HKEY temp = m_handle;
        m_handle = nullptr;
        return temp;
    }
    
    HKEY get() const noexcept {
        return m_handle;
    }
    
    bool isValid() const noexcept {
        return m_handle != nullptr;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
          
    static RegistryHandle openKey(HKEY parent, const std::string& subKey, REGSAM access) {
        HKEY hKey = nullptr;
        LONG result = ::RegOpenKeyExA(parent, subKey.c_str(), 0, access, &hKey);
        if (result != ERROR_SUCCESS) {
            throw std::runtime_error("Failed to open registry key: " + std::to_string(result));
        }
        return RegistryHandle(hKey);
    }
    
    static RegistryHandle createKey(HKEY parent, const std::string& subKey, REGSAM access) {
        HKEY hKey = nullptr;
        DWORD disposition = 0;
        LONG result = ::RegCreateKeyExA(parent, subKey.c_str(), 0, nullptr, 
                                       REG_OPTION_NON_VOLATILE, access, nullptr, &hKey, &disposition);
        if (result != ERROR_SUCCESS) {
            throw std::runtime_error("Failed to create registry key: " + std::to_string(result));
        }
        return RegistryHandle(hKey);
    }
    
private:
    HKEY m_handle;
};

/**
 * @brief RAII wrapper for process handles
 */
class ProcessHandle {
public:
    ProcessHandle() : m_handle(nullptr) {}
    
    explicit ProcessHandle(HANDLE handle) : m_handle(handle) {}
    
    ~ProcessHandle() {
        reset();
    }
    
    // Move constructor
    ProcessHandle(ProcessHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }
    
    // Move assignment
    ProcessHandle& operator=(ProcessHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;
    
    void reset(HANDLE handle = nullptr) {
        if (m_handle != nullptr) {
            ::CloseHandle(m_handle);
        }
        m_handle = handle;
    }
    
    HANDLE release() noexcept {
        HANDLE temp = m_handle;
        m_handle = nullptr;
        return temp;
    }
    
    HANDLE get() const noexcept {
        return m_handle;
    }
    
    bool isValid() const noexcept {
        return m_handle != nullptr;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
          
    static ProcessHandle openProcess(DWORD access, BOOL inherit, DWORD pid) {
        HANDLE h = ::OpenProcess(access, inherit, pid);
        if (h == nullptr) {
            throw std::runtime_error("Failed to open process: " + std::to_string(::GetLastError()));
        }
        return ProcessHandle(h);
    }
    
    bool terminate(UINT exitCode) {
        return ::TerminateProcess(get(), exitCode) != 0;
    }
    
    DWORD waitForExit(DWORD timeout = INFINITE) {
        return ::WaitForSingleObject(get(), timeout);
    }
    
    bool getExitCode(DWORD* exitCode) {
        return ::GetExitCodeProcess(get(), exitCode) != 0;
    }
    
private:
    HANDLE m_handle;
};

/**
 * @brief RAII wrapper for HWND (window handles)
 */
class HwndWrapper {
public:
    HwndWrapper() : m_hwnd(nullptr), m_shouldDestroy(false) {}
    
    explicit HwndWrapper(HWND hwnd, bool shouldDestroy = false)
        : m_hwnd(hwnd), m_shouldDestroy(shouldDestroy) {}
        
    ~HwndWrapper() {
        reset();
    }
    
    // Move constructor
    HwndWrapper(HwndWrapper&& other) noexcept
        : m_hwnd(other.m_hwnd), m_shouldDestroy(other.m_shouldDestroy) {
        other.m_hwnd = nullptr;
        other.m_shouldDestroy = false;
    }
    
    // Move assignment
    HwndWrapper& operator=(HwndWrapper&& other) noexcept {
        if (this != &other) {
            reset();
            m_hwnd = other.m_hwnd;
            m_shouldDestroy = other.m_shouldDestroy;
            other.m_hwnd = nullptr;
            other.m_shouldDestroy = false;
        }
        return *this;
    }
    
    // Delete copy operations
    HwndWrapper(const HwndWrapper&) = delete;
    HwndWrapper& operator=(const HwndWrapper&) = delete;
    
    void reset(HWND hwnd = nullptr) {
        if (m_hwnd && m_shouldDestroy) {
            ::DestroyWindow(m_hwnd);
        }
        m_hwnd = hwnd;
    }
    
    HWND release() noexcept {
        HWND temp = m_hwnd;
        m_hwnd = nullptr;
        m_shouldDestroy = false;
        return temp;
    }
    
    HWND get() const noexcept {
        return m_hwnd;
    }
    
    bool isValid() const noexcept {
        return m_hwnd != nullptr && ::IsWindow(m_hwnd);
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
    
private:
    HWND m_hwnd;
    bool m_shouldDestroy;
};

#endif // _WIN32

/**
 * @brief RAII wrapper for mutex locks
 */
template<typename Mutex>
class LockGuard {
public:
    explicit LockGuard(Mutex& mutex) : m_mutex(mutex), m_owns(false) {
        lock();
    }
    
    ~LockGuard() {
        if (m_owns) {
            unlock();
        }
    }
    
    // Delete copy operations
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    
    void lock() {
        if (!m_owns) {
            m_mutex.lock();
            m_owns = true;
        }
    }
    
    void unlock() {
        if (m_owns) {
            m_mutex.unlock();
            m_owns = false;
        }
    }
    
    bool owns() const noexcept {
        return m_owns;
    }
    
private:
    Mutex& m_mutex;
    bool m_owns;
};

/**
 * @brief RAII wrapper for FILE* handles
 */
class FileWrapper {
public:
    FileWrapper() : m_file(nullptr) {}
    
    explicit FileWrapper(FILE* file) : m_file(file) {}
    
    explicit FileWrapper(const std::string& filename, const std::string& mode) {
        m_file = std::fopen(filename.c_str(), mode.c_str());
        if (!m_file) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
    }
    
    ~FileWrapper() {
        reset();
    }
    
    // Move constructor
    FileWrapper(FileWrapper&& other) noexcept : m_file(other.m_file) {
        other.m_file = nullptr;
    }
    
    // Move assignment
    FileWrapper& operator=(FileWrapper&& other) noexcept {
        if (this != &other) {
            reset();
            m_file = other.m_file;
            other.m_file = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    FileWrapper(const FileWrapper&) = delete;
    FileWrapper& operator=(const FileWrapper&) = delete;
    
    void reset(FILE* file = nullptr) {
        if (m_file) {
            std::fclose(m_file);
        }
        m_file = file;
    }
    
    FILE* release() noexcept {
        FILE* temp = m_file;
        m_file = nullptr;
        return temp;
    }
    
    FILE* get() const noexcept {
        return m_file;
    }
    
    bool isValid() const noexcept {
        return m_file != nullptr;
    }
    
    explicit operator bool() const noexcept {
        return isValid();
    }
    
    // File operations
    size_t read(void* buffer, size_t size, size_t count) {
        return std::fread(buffer, size, count, m_file);
    }
    
    size_t write(const void* buffer, size_t size, size_t count) {
        return std::fwrite(buffer, size, count, m_file);
    }
    
    bool flush() {
        return std::fflush(m_file) == 0;
    }
    
    bool seek(long offset, int origin) {
        return std::fseek(m_file, offset, origin) == 0;
    }
    
    long tell() {
        return std::ftell(m_file);
    }
    
private:
    FILE* m_file;
};

/**
 * @brief Deleter for use with unique_ptr
 */
template<typename T>
struct ArrayDeleter {
    void operator()(T* p) const {
        delete[] p;
    }
};

/**
 * @brief Create a unique_ptr for arrays
 */
template<typename T>
using unique_array = std::unique_ptr<T[], ArrayDeleter<T>>;

/**
 * @brief Helper to create unique_array
 */
template<typename T>
unique_array<T> make_unique_array(size_t size) {
    return unique_array<T>(new T[size]());
}

} // namespace raii
} // namespace burwell

#endif // BURWELL_RAII_WRAPPERS_H