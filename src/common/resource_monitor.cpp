#include "resource_monitor.h"
#include "structured_logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <cxxabi.h>
#endif

namespace burwell {

ResourceMonitor::ResourceMonitor() 
    : m_enabled(true)
    , m_totalMemoryUsage(0)
    , m_peakMemoryUsage(0) {
#ifdef DEBUG
    SLOG_DEBUG().message("ResourceMonitor initialized for leak detection");
#endif
}

ResourceMonitor::~ResourceMonitor() {
#ifdef DEBUG
    reportLeaks();
#endif
}

void ResourceMonitor::trackAllocation(ResourceType type, void* address, 
                                    size_t size, const std::string& description) {
    if (!m_enabled || !address) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    ResourceEntry entry;
    entry.info.type = type;
    entry.info.address = address;
    entry.info.size = size;
    entry.info.description = description;
    entry.info.allocTime = std::chrono::steady_clock::now();
    entry.info.stackTrace = captureStackTrace();
    entry.info.isLeaked = false;
    entry.deallocated = false;
    
    m_resources[address] = entry;
    
    // Update statistics
    m_activeCount[type]++;
    m_totalAllocated[type]++;
    
    if (type == ResourceType::MEMORY) {
        m_totalMemoryUsage += size;
        if (m_totalMemoryUsage > m_peakMemoryUsage) {
            m_peakMemoryUsage = m_totalMemoryUsage.load();
        }
    }
    
    updatePeakUsage(type);
    
#ifdef DEBUG
    SLOG_DEBUG().message("Resource allocated")
        .context("description", description)
        .context("address", std::to_string(reinterpret_cast<uintptr_t>(address)));
#endif
}

void ResourceMonitor::trackDeallocation(void* address) {
    if (!m_enabled || !address) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_resources.find(address);
    if (it != m_resources.end()) {
        if (!it->second.deallocated) {
            ResourceType type = it->second.info.type;
            size_t size = it->second.info.size;
            
            it->second.deallocated = true;
            
            // Update statistics
            if (m_activeCount[type] > 0) {
                m_activeCount[type]--;
            }
            m_totalDeallocated[type]++;
            
            if (type == ResourceType::MEMORY) {
                if (m_totalMemoryUsage >= size) {
                    m_totalMemoryUsage -= size;
                }
            }
            
#ifdef DEBUG
            SLOG_DEBUG().message("Resource deallocated")
                .context("description", it->second.info.description)
                .context("address", std::to_string(reinterpret_cast<uintptr_t>(address)));
#endif
        } else {
            SLOG_WARNING().message("Double deallocation detected")
                .context("address", std::to_string(reinterpret_cast<uintptr_t>(address)));
        }
    } else {
        SLOG_WARNING().message("Deallocation of untracked resource")
            .context("address", std::to_string(reinterpret_cast<uintptr_t>(address)));
    }
}

std::vector<ResourceMonitor::ResourceInfo> ResourceMonitor::detectLeaks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ResourceInfo> leaks;
    
    for (const auto& pair : m_resources) {
        if (!pair.second.deallocated) {
            ResourceInfo leak = pair.second.info;
            leak.isLeaked = true;
            leaks.push_back(leak);
        }
    }
    
    return leaks;
}

ResourceMonitor::ResourceStats ResourceMonitor::getStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    ResourceStats stats;
    stats.activeCount = m_activeCount;
    stats.totalAllocated = m_totalAllocated;
    stats.totalDeallocated = m_totalDeallocated;
    stats.peakUsage = m_peakUsage;
    stats.totalMemoryUsage = m_totalMemoryUsage;
    stats.peakMemoryUsage = m_peakMemoryUsage;
    
    return stats;
}

void ResourceMonitor::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_resources.clear();
    m_activeCount.clear();
    m_totalAllocated.clear();
    m_totalDeallocated.clear();
    m_peakUsage.clear();
    m_totalMemoryUsage = 0;
    m_peakMemoryUsage = 0;
}

void ResourceMonitor::reportLeaks() {
    auto leaks = detectLeaks();
    
    if (leaks.empty()) {
        SLOG_INFO().message("No resource leaks detected");
        return;
    }
    
    SLOG_ERROR().message("RESOURCE LEAKS DETECTED").context("leak_count", leaks.size());
    
    std::stringstream report;
    report << "\n=== RESOURCE LEAK REPORT ===\n";
    
    for (const auto& leak : leaks) {
        report << "\nLeak #" << (&leak - &leaks[0] + 1) << ":\n";
        report << "  Type: ";
        
        switch (leak.type) {
            case ResourceType::MEMORY: report << "MEMORY"; break;
            case ResourceType::FILE_HANDLE: report << "FILE_HANDLE"; break;
            case ResourceType::WINDOW_HANDLE: report << "WINDOW_HANDLE"; break;
            case ResourceType::PROCESS_HANDLE: report << "PROCESS_HANDLE"; break;
            case ResourceType::REGISTRY_HANDLE: report << "REGISTRY_HANDLE"; break;
            case ResourceType::SOCKET: report << "SOCKET"; break;
            case ResourceType::THREAD: report << "THREAD"; break;
            case ResourceType::MUTEX: report << "MUTEX"; break;
            default: report << "OTHER"; break;
        }
        
        report << "\n";
        report << "  Address: 0x" << std::hex << reinterpret_cast<uintptr_t>(leak.address) << std::dec << "\n";
        report << "  Size: " << leak.size << " bytes\n";
        report << "  Description: " << leak.description << "\n";
        
        auto duration = std::chrono::steady_clock::now() - leak.allocTime;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        report << "  Allocated for: " << seconds << " seconds\n";
        
        if (!leak.stackTrace.empty()) {
            report << "  Stack trace:\n" << leak.stackTrace << "\n";
        }
    }
    
    report << "\n=== END LEAK REPORT ===\n";
    
    SLOG_ERROR().message("Resource leak report").context("report", report.str());
    
    if (m_leakCallback) {
        m_leakCallback(leaks);
    }
}

std::string ResourceMonitor::captureStackTrace() {
    std::stringstream trace;
    
#ifdef _WIN32
    const int maxFrames = 32;
    void* stack[maxFrames];
    HANDLE process = GetCurrentProcess();
    
    SymInitialize(process, NULL, TRUE);
    
    USHORT frames = CaptureStackBackTrace(0, maxFrames, stack, NULL);
    
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
    for (USHORT i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)(stack[i]);
        SymFromAddr(process, address, 0, symbol);
        
        trace << "    [" << i << "] " << symbol->Name 
              << " + 0x" << std::hex << (address - symbol->Address) << std::dec << "\n";
    }
    
    free(symbol);
    SymCleanup(process);
    
#else
    const int maxFrames = 32;
    void* frames[maxFrames];
    
    int numFrames = backtrace(frames, maxFrames);
    char** symbols = backtrace_symbols(frames, numFrames);
    
    if (symbols) {
        for (int i = 0; i < numFrames; i++) {
            trace << "    [" << i << "] " << symbols[i] << "\n";
        }
        free(symbols);
    }
#endif
    
    return trace.str();
}

void ResourceMonitor::updatePeakUsage(ResourceType type) {
    size_t currentUsage = m_activeCount[type];
    if (currentUsage > m_peakUsage[type]) {
        m_peakUsage[type] = currentUsage;
    }
}

} // namespace burwell