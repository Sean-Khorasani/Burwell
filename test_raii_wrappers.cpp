#include "src/common/raii_wrappers.h"
#include <iostream>
#include <mutex>

using namespace burwell::raii;

int main() {
    try {
#ifdef _WIN32
        // Test WindowHandle
        {
            WindowHandle handle(INVALID_HANDLE_VALUE);
            std::cout << "WindowHandle test: " << (handle.isValid() ? "valid" : "invalid") << std::endl;
        }
        
        // Test FileHandle
        {
            FileHandle file;
            std::cout << "FileHandle test: " << (file.isValid() ? "valid" : "invalid") << std::endl;
        }
        
        // Test RegistryHandle
        {
            RegistryHandle reg;
            std::cout << "RegistryHandle test: " << (reg.isValid() ? "valid" : "invalid") << std::endl;
        }
        
        // Test ProcessHandle
        {
            ProcessHandle proc;
            std::cout << "ProcessHandle test: " << (proc.isValid() ? "valid" : "invalid") << std::endl;
        }
        
        // Test HwndWrapper
        {
            HwndWrapper hwnd;
            std::cout << "HwndWrapper test: " << (hwnd.isValid() ? "valid" : "invalid") << std::endl;
        }
#endif
        
        // Test FileWrapper
        {
            FileWrapper file;
            std::cout << "FileWrapper test: " << (file.isValid() ? "valid" : "invalid") << std::endl;
        }
        
        // Test LockGuard
        {
            std::mutex m;
            LockGuard<std::mutex> lock(m);
            std::cout << "LockGuard test: " << (lock.owns() ? "owns" : "doesn't own") << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    
    return 0;
}