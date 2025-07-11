#ifndef BURWELL_ENVIRONMENTAL_PERCEPTION_H
#define BURWELL_ENVIRONMENTAL_PERCEPTION_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace burwell {

struct ScreenRegion {
    int x, y;           // Top-left coordinates
    int width, height;  // Dimensions
    
    ScreenRegion(int x = 0, int y = 0, int w = 0, int h = 0) 
        : x(x), y(y), width(w), height(h) {}
    
    bool isValid() const { return width > 0 && height > 0; }
};

struct Screenshot {
    std::vector<uint8_t> data;
    int width, height;
    int bitsPerPixel;
    std::string format;
    std::string filePath;
    
    Screenshot() : width(0), height(0), bitsPerPixel(0), format("RGB") {}
    
    bool isValid() const { return !data.empty() && width > 0 && height > 0; }
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
};

struct UIElement {
    std::string type;           // "button", "textbox", "image", "link", etc.
    std::string text;           // Visible text or label
    std::string id;             // Element ID if available
    std::string className;      // CSS class or control class
    ScreenRegion bounds;        // Element position and size
    bool isVisible;
    bool isEnabled;
    bool isClickable;
    std::map<std::string, std::string> attributes;
    
    UIElement() : isVisible(false), isEnabled(false), isClickable(false) {}
};

struct WindowInfo {
    void* handle;               // Window handle (HWND on Windows)
    std::string title;
    std::string className;
    std::string processName;
    unsigned long processId;
    ScreenRegion bounds;
    bool isVisible;
    bool isMinimized;
    bool isMaximized;
    int zOrder;                // Z-order (depth)
    
    WindowInfo() : handle(nullptr), processId(0), isVisible(false), 
                   isMinimized(false), isMaximized(false), zOrder(0) {}
};

struct OCRResult {
    std::string text;
    float confidence;
    bool success;
    std::string errorMessage;
    std::vector<ScreenRegion> wordBounds;
    std::vector<std::string> words;
    std::map<std::string, std::string> metadata;
    
    OCRResult() : confidence(0.0f), success(false) {}
    
    bool hasText() const { return !text.empty(); }
    bool isConfident(float threshold = 0.8f) const { return confidence >= threshold; }
};

class EnvironmentalPerception {
public:
    EnvironmentalPerception();
    ~EnvironmentalPerception();
    
    // Screen capture functionality
    Screenshot captureScreen();
    Screenshot captureWindow(void* windowHandle);
    Screenshot captureRegion(const ScreenRegion& region);
    bool saveScreenshot(const Screenshot& screenshot, const std::string& filePath);
    
    // Legacy interface (for compatibility)
    std::string takeScreenshot(const std::string& outputPath, const std::string& region = "");
    
    // Window enumeration and management
    std::vector<WindowInfo> enumerateWindows();
    std::vector<WindowInfo> getVisibleWindows();
    std::vector<WindowInfo> getWindowsByProcess(const std::string& processName);
    WindowInfo getActiveWindow();
    WindowInfo findWindow(const std::string& title);
    
    // Legacy interface (for compatibility)
    std::vector<std::map<std::string, std::string>> listWindows();
    
    // OCR (Optical Character Recognition)
    OCRResult performOCR(const Screenshot& screenshot);
    OCRResult performOCR(const std::string& imagePath);
    OCRResult performOCROnRegion(const ScreenRegion& region);
    std::vector<std::string> extractTextLines(const Screenshot& screenshot);
    
    // UI element detection and analysis
    std::vector<UIElement> detectUIElements(const Screenshot& screenshot);
    std::vector<UIElement> findElementsByText(const std::string& text);
    std::vector<UIElement> findElementsByType(const std::string& type);
    UIElement findElementAt(int x, int y);
    
    // Legacy interface (for compatibility)
    std::map<std::string, std::string> detectUIElement(const std::map<std::string, std::string>& properties);
    
    // Advanced analysis
    bool compareScreenshots(const Screenshot& screenshot1, const Screenshot& screenshot2, float threshold = 0.95f);
    std::vector<ScreenRegion> findImageTemplates(const Screenshot& screenshot, const Screenshot& templateImage, float threshold = 0.8f);
    std::vector<ScreenRegion> detectChanges(const Screenshot& before, const Screenshot& after);
    
    // Environment information gathering (used by Orchestrator)
    nlohmann::json gatherEnvironmentInfo();
    
    // Configuration and settings
    void setOCRLanguage(const std::string& language);
    void setOCRConfidenceThreshold(float threshold);
    void enableUIElementCaching(bool enable);
    void setScreenshotFormat(const std::string& format); // "BMP", "PNG", "JPEG"
    void setScreenshotQuality(int quality); // 1-100 for JPEG
    
    // Performance and caching
    void clearCache();
    void enablePerformanceMode(bool enable);
    void setMaxCacheSize(size_t maxSizeMB);
    
    // Event monitoring
    using WindowEventCallback = std::function<void(const WindowInfo&, const std::string& event)>;
    bool startWindowMonitoring(WindowEventCallback callback);
    void stopWindowMonitoring();
    
    // Accessibility support
    std::vector<UIElement> getAccessibleElements(void* windowHandle);
    std::string getElementAccessibleName(const UIElement& element);
    bool isElementAccessible(const UIElement& element);
    
    // System information
    ScreenRegion getDesktopBounds();
    std::vector<ScreenRegion> getMonitorBounds();
    int getCurrentDPI();
    float getDisplayScaling();

private:
    struct EnvironmentalPerceptionImpl;
    std::unique_ptr<EnvironmentalPerceptionImpl> m_impl;
    
    // Configuration
    std::string m_ocrLanguage;
    float m_ocrConfidenceThreshold;
    bool m_uiElementCaching;
    std::string m_screenshotFormat;
    int m_screenshotQuality;
    bool m_performanceMode;
    size_t m_maxCacheSize;
    
    // Caching
    std::map<std::string, Screenshot> m_screenshotCache;
    std::map<std::string, std::vector<UIElement>> m_uiElementCache;
    std::map<std::string, OCRResult> m_ocrCache;
    
    // Internal methods
    bool initializeOCR();
    bool initializeScreenCapture();
    void cleanupCache();
    std::string generateCacheKey(const std::string& type, const std::string& data);
    
    // Platform-specific implementations
    Screenshot captureScreenWindows();
    Screenshot captureWindowWindows(void* windowHandle);
    Screenshot captureRegionWindows(const ScreenRegion& region);
    std::vector<WindowInfo> enumerateWindowsWindows();
    OCRResult performOCRWindows(const Screenshot& screenshot);
    std::vector<UIElement> detectUIElementsWindows(const Screenshot& screenshot);
    
    // Utility methods
    ScreenRegion parseRegionString(const std::string& regionStr);
    std::string formatRegionString(const ScreenRegion& region);
    bool validateScreenRegion(const ScreenRegion& region);
    Screenshot convertToFormat(const Screenshot& screenshot, const std::string& format);
    
    // Text-based screen description for text-only LLMs
    std::string generateTextBasedScreenDescription(const nlohmann::json& envData);
};

} // namespace burwell

#endif // BURWELL_ENVIRONMENTAL_PERCEPTION_H