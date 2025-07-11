#include "environmental_perception.h"
#include "../ocal/application_automation.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/os_utils.h"
#include "../common/input_validator.h"
#include "../common/string_utils.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#undef ERROR  // Fix conflict with LogLevel::ERROR_LEVEL
#endif

using namespace burwell;

// Screenshot implementations
bool Screenshot::saveToFile(const std::string& path) const {
    if (!isValid()) return false;
    
    // Validate file path
    auto pathValidation = burwell::InputValidator::validateFilePath(path);
    if (!pathValidation.isValid) {
        SLOG_ERROR().message("Invalid screenshot save path")
            .context("error", pathValidation.errorMessage);
        return false;
    }
    
    // Validate file extension
    std::string extension = std::filesystem::path(path).extension().string();
    std::vector<std::string> validExtensions = {".bmp", ".png", ".jpg", ".jpeg"};
    if (std::find(validExtensions.begin(), validExtensions.end(), extension) == validExtensions.end()) {
        SLOG_ERROR().message("Invalid screenshot file extension")
            .context("extension", extension);
        return false;
    }
    
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        
        // Simple BMP format for now (can be enhanced for PNG/JPEG)
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;
        
        // BMP header implementation would go here
        // For now, just save raw data
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        return file.good();
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to save screenshot")
            .context("error", e.what());
        return false;
    }
}

bool Screenshot::loadFromFile(const std::string& path) {
    // Validate file path
    auto pathValidation = burwell::InputValidator::validateFilePath(path);
    if (!pathValidation.isValid) {
        SLOG_ERROR().message("Invalid screenshot load path")
            .context("error", pathValidation.errorMessage);
        return false;
    }
    
    // Check file exists
    if (!std::filesystem::exists(path)) {
        SLOG_ERROR().message("Screenshot file does not exist")
            .context("path", path);
        return false;
    }
    
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        
        // Basic file loading (would need proper image format support)
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Validate file size (max 100MB for safety)
        const size_t MAX_FILE_SIZE = 100 * 1024 * 1024;
        if (size > MAX_FILE_SIZE) {
            SLOG_ERROR().message("Screenshot file too large")
                .context("size", size)
                .context("max_size", MAX_FILE_SIZE);
            return false;
        }
        
        data.resize(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        
        filePath = path;
        return true;
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to load screenshot")
            .context("error", e.what());
        return false;
    }
}

#ifdef _WIN32
struct EnvironmentalPerception::EnvironmentalPerceptionImpl {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    bool gdiplusInitialized;
    
    EnvironmentalPerceptionImpl() : gdiplusInitialized(false) {
        // Initialize GDI+ for screen capture
        Gdiplus::Status status = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        if (status == Gdiplus::Ok) {
            gdiplusInitialized = true;
            SLOG_DEBUG().message("GDI+ initialized successfully");
        } else {
            SLOG_ERROR().message("Failed to initialize GDI+");
        }
    }
    
    ~EnvironmentalPerceptionImpl() {
        if (gdiplusInitialized) {
            Gdiplus::GdiplusShutdown(gdiplusToken);
        }
    }
};
#else
struct EnvironmentalPerception::EnvironmentalPerceptionImpl {
    // Placeholder for non-Windows platforms
    EnvironmentalPerceptionImpl() {}
    ~EnvironmentalPerceptionImpl() {}
};
#endif

EnvironmentalPerception::EnvironmentalPerception()
    : m_impl(std::make_unique<EnvironmentalPerceptionImpl>())
    , m_ocrLanguage("en")
    , m_ocrConfidenceThreshold(0.8f)
    , m_uiElementCaching(true)
    , m_screenshotFormat("BMP")
    , m_screenshotQuality(90)
    , m_performanceMode(false)
    , m_maxCacheSize(100) {
    
    initializeScreenCapture();
    initializeOCR();
    
    SLOG_INFO().message("EnvironmentalPerception initialized");
}

EnvironmentalPerception::~EnvironmentalPerception() = default;

Screenshot EnvironmentalPerception::captureScreen() {
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Capturing full screen");
        return captureScreenWindows();
    }, "EnvironmentalPerception::captureScreen");
    
    return Screenshot();
}

Screenshot EnvironmentalPerception::captureScreenWindows() {
    Screenshot screenshot;
    
#ifdef _WIN32
    if (!m_impl->gdiplusInitialized) {
        return screenshot;
    }
    
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Create device contexts
    HDC screenDC = GetDC(nullptr);
    HDC memoryDC = CreateCompatibleDC(screenDC);
    
    // Create bitmap
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, screenWidth, screenHeight);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memoryDC, bitmap);
    
    // Copy screen to bitmap
    BitBlt(memoryDC, 0, 0, screenWidth, screenHeight, screenDC, 0, 0, SRCCOPY);
    
    // Get bitmap data
    BITMAPINFO bmpInfo = {};
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = screenWidth;
    bmpInfo.bmiHeader.biHeight = -screenHeight; // Negative for top-down
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 24;
    bmpInfo.bmiHeader.biCompression = BI_RGB;
    
    int dataSize = screenWidth * screenHeight * 3; // 24 bits = 3 bytes per pixel
    screenshot.data.resize(dataSize);
    
    GetDIBits(memoryDC, bitmap, 0, screenHeight, screenshot.data.data(), &bmpInfo, DIB_RGB_COLORS);
    
    screenshot.width = screenWidth;
    screenshot.height = screenHeight;
    screenshot.bitsPerPixel = 24;
    screenshot.format = "RGB";
    
    // Cleanup
    SelectObject(memoryDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDC);
    ReleaseDC(nullptr, screenDC);
    
    SLOG_DEBUG().message("Screen captured")
        .context("width", screenWidth)
        .context("height", screenHeight);
#else
    // Non-Windows simulation
    screenshot.width = 1920;
    screenshot.height = 1080;
    screenshot.bitsPerPixel = 24;
    screenshot.format = "RGB";
    screenshot.data.resize(screenshot.width * screenshot.height * 3, 128); // Gray image
    SLOG_DEBUG().message("Screen capture simulated (non-Windows platform)");
#endif
    
    return screenshot;
}

std::vector<WindowInfo> EnvironmentalPerception::enumerateWindows() {
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Enumerating all windows");
        return enumerateWindowsWindows();
    }, "EnvironmentalPerception::enumerateWindows");
    
    return {};
}

std::vector<WindowInfo> EnvironmentalPerception::enumerateWindowsWindows() {
    std::vector<WindowInfo> windows;
    
#ifdef _WIN32
    struct EnumData {
        std::vector<WindowInfo>* windows;
    };
    
    EnumData data;
    data.windows = &windows;
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* data = reinterpret_cast<EnumData*>(lParam);
        
        WindowInfo info;
        info.handle = hwnd;
        
        // Get window title
        char title[256];
        GetWindowTextA(hwnd, title, sizeof(title));
        info.title = title;
        
        // Get class name
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        info.className = className;
        
        // Get process ID
        GetWindowThreadProcessId(hwnd, &info.processId);
        
        // Get window rect
        RECT rect;
        GetWindowRect(hwnd, &rect);
        info.bounds.x = rect.left;
        info.bounds.y = rect.top;
        info.bounds.width = rect.right - rect.left;
        info.bounds.height = rect.bottom - rect.top;
        
        // Get window state
        info.isVisible = IsWindowVisible(hwnd);
        info.isMinimized = IsIconic(hwnd);
        info.isMaximized = IsZoomed(hwnd);
        
        data->windows->push_back(info);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    
    SLOG_DEBUG().message("Found windows")
        .context("window_count", windows.size());
#else
    // Non-Windows simulation
    for (int i = 0; i < 3; ++i) {
        WindowInfo info;
        info.title = "Simulated Window " + std::to_string(i);
        info.className = "SimulatedClass";
        info.processId = 1000 + i;
        info.bounds = ScreenRegion(100 * i, 100 * i, 800, 600);
        info.isVisible = true;
        windows.push_back(info);
    }
    SLOG_DEBUG().message("Window enumeration simulated (non-Windows platform)");
#endif
    
    return windows;
}

OCRResult EnvironmentalPerception::performOCR(const Screenshot& screenshot) {
    OCRResult result;
    
    BURWELL_TRY_CATCH({
        if (!screenshot.isValid()) {
            return result;
        }
        
        SLOG_DEBUG().message("Performing OCR on screenshot");
        return performOCRWindows(screenshot);
    }, "EnvironmentalPerception::performOCR");
    
    return result;
}

OCRResult EnvironmentalPerception::performOCRWindows(const Screenshot& screenshot) {
    (void)screenshot; // TODO: Implement OCR using screenshot data
    OCRResult result;
    
#ifdef _WIN32
    // For now, implement basic text detection simulation
    // In a full implementation, you would integrate with Windows OCR API or Tesseract
    result.text = "Simulated OCR text detection";
    result.confidence = 0.85f;
    result.words = {"Simulated", "OCR", "text", "detection"};
    
    // Create mock word bounds
    for (size_t i = 0; i < result.words.size(); ++i) {
        ScreenRegion bounds(i * 100, 10, 80, 20);
        result.wordBounds.push_back(bounds);
    }
    
    SLOG_DEBUG().message("OCR completed")
        .context("word_count", result.words.size());
#else
    result.text = "OCR simulated on non-Windows platform";
    result.confidence = 0.9f;
    SLOG_DEBUG().message("OCR simulated (non-Windows platform)");
#endif
    
    return result;
}

// Legacy interface implementations
std::string EnvironmentalPerception::takeScreenshot(const std::string& outputPath, const std::string& region) {
    // Validate output path
    auto pathValidation = burwell::InputValidator::validateFilePath(outputPath);
    if (!pathValidation.isValid) {
        SLOG_ERROR().message("Invalid screenshot output path")
            .context("error", pathValidation.errorMessage);
        return "";
    }
    
    Screenshot screenshot;
    
    if (region.empty()) {
        screenshot = captureScreen();
    } else {
        // Validate region string format before parsing
        if (!burwell::InputValidator::isNotEmpty(region)) {
            SLOG_ERROR().message("Empty region string provided");
            return "";
        }
        ScreenRegion screenRegion = parseRegionString(region);
        if (!validateScreenRegion(screenRegion)) {
            SLOG_ERROR().message("Invalid screen region")
                .context("region", region);
            return "";
        }
        screenshot = captureRegion(screenRegion);
    }
    
    if (screenshot.isValid() && screenshot.saveToFile(outputPath)) {
        return outputPath;
    }
    
    return "";
}

std::vector<std::map<std::string, std::string>> EnvironmentalPerception::listWindows() {
    std::vector<std::map<std::string, std::string>> result;
    auto windows = enumerateWindows();
    
    for (const auto& window : windows) {
        std::map<std::string, std::string> windowMap;
        windowMap["title"] = window.title;
        windowMap["className"] = window.className;
        windowMap["processId"] = std::to_string(window.processId);
        windowMap["visible"] = window.isVisible ? "true" : "false";
        result.push_back(windowMap);
    }
    
    return result;
}

OCRResult EnvironmentalPerception::performOCR(const std::string& imagePath) {
    // Validate image path
    auto pathValidation = burwell::InputValidator::validateFilePath(imagePath);
    if (!pathValidation.isValid) {
        OCRResult result;
        result.success = false;
        result.errorMessage = "Invalid image path: " + pathValidation.errorMessage;
        SLOG_ERROR().message("Invalid OCR image path")
            .context("error", pathValidation.errorMessage);
        return result;
    }
    
    Screenshot screenshot;
    if (!screenshot.loadFromFile(imagePath)) {
        OCRResult result;
        result.success = false;
        result.errorMessage = "Failed to load image";
        return result;
    }
    
    return performOCR(screenshot);
}

std::map<std::string, std::string> EnvironmentalPerception::detectUIElement(const std::map<std::string, std::string>& properties) {
    std::map<std::string, std::string> result;
    
    // Simulate element detection based on properties
    if (properties.find("type") != properties.end()) {
        result["found"] = "true";
        result["x"] = "100";
        result["y"] = "100";
        result["width"] = "80";
        result["height"] = "30";
    } else {
        result["found"] = "false";
    }
    
    return result;
}

// Utility methods
ScreenRegion EnvironmentalPerception::parseRegionString(const std::string& regionStr) {
    // Validate region string is not empty
    if (!burwell::InputValidator::isNotEmpty(regionStr)) {
        SLOG_ERROR().message("Empty region string provided");
        return ScreenRegion();
    }
    
    // Parse format: "x,y,width,height"
    std::regex regionRegex(R"((\d+),(\d+),(\d+),(\d+))");
    std::smatch match;
    
    if (std::regex_match(regionStr, match, regionRegex)) {
        int x = std::stoi(match[1]);
        int y = std::stoi(match[2]);
        int width = std::stoi(match[3]);
        int height = std::stoi(match[4]);
        
        // Validate region values
        const int MAX_COORDINATE = 10000;
        const int MAX_DIMENSION = 10000;
        
        if (x < 0 || x > MAX_COORDINATE || y < 0 || y > MAX_COORDINATE) {
            SLOG_ERROR().message("Invalid region coordinates")
                .context("x", x)
                .context("y", y);
            return ScreenRegion();
        }
        
        if (width <= 0 || width > MAX_DIMENSION || height <= 0 || height > MAX_DIMENSION) {
            SLOG_ERROR().message("Invalid region dimensions")
                .context("width", width)
                .context("height", height);
            return ScreenRegion();
        }
        
        return ScreenRegion(x, y, width, height);
    }
    
    SLOG_ERROR().message("Invalid region string format")
        .context("expected", "x,y,width,height")
        .context("received", regionStr);
    return ScreenRegion();
}

bool EnvironmentalPerception::initializeScreenCapture() {
    SLOG_DEBUG().message("Initializing screen capture");
    return true;
}

bool EnvironmentalPerception::initializeOCR() {
    SLOG_DEBUG().message("Initializing OCR");
    return true;
}

ScreenRegion EnvironmentalPerception::getDesktopBounds() {
#ifdef _WIN32
    return ScreenRegion(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
#else
    return ScreenRegion(0, 0, 1920, 1080); // Simulated
#endif
}

// Simple implementations for remaining methods to make it compilable
Screenshot EnvironmentalPerception::captureWindow(void* windowHandle) { 
    (void)windowHandle; // TODO: Implement window-specific screenshot capture
    return Screenshot(); 
}
Screenshot EnvironmentalPerception::captureRegion(const ScreenRegion& region) { 
    (void)region; // TODO: Implement region-specific screenshot capture
    return Screenshot(); 
}
bool EnvironmentalPerception::saveScreenshot(const Screenshot& screenshot, const std::string& filePath) { 
    (void)screenshot; // TODO: Implement screenshot saving functionality
    (void)filePath; // TODO: Implement file path handling for screenshot
    return false; 
}
std::vector<WindowInfo> EnvironmentalPerception::getVisibleWindows() { return {}; }
std::vector<WindowInfo> EnvironmentalPerception::getWindowsByProcess(const std::string& processName) { 
    (void)processName; // TODO: Implement process-based window filtering
    return {}; 
}
WindowInfo EnvironmentalPerception::getActiveWindow() { return WindowInfo(); }
WindowInfo EnvironmentalPerception::findWindow(const std::string& title) { 
    (void)title; // TODO: Implement window search by title
    return WindowInfo(); 
}
OCRResult EnvironmentalPerception::performOCROnRegion(const ScreenRegion& region) { 
    (void)region; // TODO: Implement OCR for specific screen region
    return OCRResult(); 
}
std::vector<std::string> EnvironmentalPerception::extractTextLines(const Screenshot& screenshot) { 
    (void)screenshot; // TODO: Implement text line extraction from screenshot
    return {}; 
}
std::vector<UIElement> EnvironmentalPerception::detectUIElements(const Screenshot& screenshot) { 
    (void)screenshot; // TODO: Implement UI element detection from screenshot
    return {}; 
}
std::vector<UIElement> EnvironmentalPerception::findElementsByText(const std::string& text) { 
    (void)text; // TODO: Implement UI element search by text content
    return {}; 
}
std::vector<UIElement> EnvironmentalPerception::findElementsByType(const std::string& type) { 
    (void)type; // TODO: Implement UI element search by type
    return {}; 
}
UIElement EnvironmentalPerception::findElementAt(int x, int y) { 
    (void)x; // TODO: Implement UI element detection at coordinates
    (void)y; // TODO: Implement UI element detection at coordinates
    return UIElement(); 
}
bool EnvironmentalPerception::compareScreenshots(const Screenshot& s1, const Screenshot& s2, float threshold) { 
    (void)s1; // TODO: Implement screenshot comparison functionality
    (void)s2; // TODO: Implement screenshot comparison functionality
    (void)threshold; // TODO: Implement threshold-based comparison
    return false; 
}
std::vector<ScreenRegion> EnvironmentalPerception::findImageTemplates(const Screenshot& s, const Screenshot& templateImage, float threshold) { 
    (void)s; // TODO: Implement template matching in screenshot
    (void)templateImage; // TODO: Implement template image handling
    (void)threshold; // TODO: Implement threshold-based template matching
    return {}; 
}
std::vector<ScreenRegion> EnvironmentalPerception::detectChanges(const Screenshot& before, const Screenshot& after) { 
    (void)before; // TODO: Implement screenshot change detection
    (void)after; // TODO: Implement screenshot change detection
    return {}; 
}

nlohmann::json EnvironmentalPerception::gatherEnvironmentInfo() {
    nlohmann::json envInfo;
    
    try {
        SLOG_DEBUG().message("Gathering comprehensive environment information");
        
        // Basic system information
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        envInfo["timestamp"] = timestamp;
        
        // Desktop environment details
        ScreenRegion desktop = getDesktopBounds();
        envInfo["screen_resolution"] = {
            {"width", desktop.width},
            {"height", desktop.height}
        };
        
        // Get active window with detailed information
        WindowInfo activeWindow = getActiveWindow();
        envInfo["activeWindow"] = activeWindow.title;
        
        nlohmann::json activeWindowDetails;
        if (!activeWindow.title.empty()) {
            activeWindowDetails["title"] = activeWindow.title;
            activeWindowDetails["className"] = activeWindow.className;
            activeWindowDetails["processId"] = activeWindow.processId;
            activeWindowDetails["bounds"] = {
                {"x", activeWindow.bounds.x},
                {"y", activeWindow.bounds.y},
                {"width", activeWindow.bounds.width},
                {"height", activeWindow.bounds.height}
            };
            activeWindowDetails["isMaximized"] = activeWindow.isMaximized;
            activeWindowDetails["isMinimized"] = activeWindow.isMinimized;
            
            // Add window control positions (close, minimize, maximize buttons)
            if (activeWindow.isVisible && !activeWindow.isMinimized) {
                nlohmann::json windowControls;
                int titleBarHeight = 30; // Standard Windows title bar height
                
                windowControls["close_button"] = {
                    {"x", activeWindow.bounds.x + activeWindow.bounds.width - 45},
                    {"y", activeWindow.bounds.y + 8},
                    {"width", 35}, {"height", titleBarHeight - 16}
                };
                windowControls["maximize_button"] = {
                    {"x", activeWindow.bounds.x + activeWindow.bounds.width - 90},
                    {"y", activeWindow.bounds.y + 8},
                    {"width", 35}, {"height", titleBarHeight - 16}
                };
                windowControls["minimize_button"] = {
                    {"x", activeWindow.bounds.x + activeWindow.bounds.width - 135},
                    {"y", activeWindow.bounds.y + 8},
                    {"width", 35}, {"height", titleBarHeight - 16}
                };
                
                activeWindowDetails["window_controls"] = windowControls;
            }
        }
        envInfo["active_window_details"] = activeWindowDetails;
        
        // Get all visible windows with detailed information
        auto visibleWindows = getVisibleWindows();
        nlohmann::json windowList = nlohmann::json::array();
        nlohmann::json detailedWindows = nlohmann::json::array();
        
        for (const auto& window : visibleWindows) {
            windowList.push_back(window.title);
            
            // Add detailed window information
            nlohmann::json windowDetails;
            windowDetails["title"] = window.title;
            windowDetails["className"] = window.className;
            windowDetails["processId"] = window.processId;
            windowDetails["bounds"] = {
                {"x", window.bounds.x},
                {"y", window.bounds.y},
                {"width", window.bounds.width},
                {"height", window.bounds.height}
            };
            windowDetails["isVisible"] = window.isVisible;
            windowDetails["isMaximized"] = window.isMaximized;
            windowDetails["isMinimized"] = window.isMinimized;
            windowDetails["zOrder"] = window.zOrder;
            
            detailedWindows.push_back(windowDetails);
        }
        envInfo["openWindows"] = windowList;
        envInfo["detailed_windows"] = detailedWindows;
        
        // Take screenshot and perform OCR for text content
        Screenshot screenshot = captureScreen();
        if (screenshot.isValid()) {
            SLOG_DEBUG().message("Screenshot captured, performing OCR");
            
            OCRResult ocrResult = performOCR(screenshot);
            if (ocrResult.success && !ocrResult.text.empty()) {
                envInfo["screen_text_content"] = ocrResult.text;
                envInfo["ocr_confidence"] = ocrResult.confidence;
                
                // Add word-level text information with positions
                nlohmann::json textElements = nlohmann::json::array();
                for (size_t i = 0; i < ocrResult.words.size() && i < ocrResult.wordBounds.size(); ++i) {
                    nlohmann::json textElement;
                    textElement["text"] = ocrResult.words[i];
                    textElement["bounds"] = {
                        {"x", ocrResult.wordBounds[i].x},
                        {"y", ocrResult.wordBounds[i].y},
                        {"width", ocrResult.wordBounds[i].width},
                        {"height", ocrResult.wordBounds[i].height}
                    };
                    textElements.push_back(textElement);
                }
                envInfo["text_elements"] = textElements;
            }
        }
        
        // Generate comprehensive text description for text-only LLMs
        std::string textDescription = generateTextBasedScreenDescription(envInfo);
        envInfo["text_description"] = textDescription;
        
        // Get current directory using OS utilities
        envInfo["currentDirectory"] = burwell::os::SystemInfo::getCurrentWorkingDirectory();
        
        SLOG_DEBUG().message("Environment information gathering completed successfully");
        
    } catch (const std::exception& e) {
        SLOG_WARNING().message("Failed to gather environment info")
            .context("error", e.what());
        envInfo["error"] = e.what();
    }
    
    return envInfo;
}

void EnvironmentalPerception::setOCRLanguage(const std::string& language) {
    // Validate language code
    if (!burwell::InputValidator::isNotEmpty(language)) {
        SLOG_ERROR().message("Empty OCR language provided");
        return;
    }
    
    // Basic language code validation (ISO 639-1)
    if (language.length() != 2 && language.length() != 3) {
        SLOG_WARNING().message("Unusual OCR language code length")
            .context("language", language)
            .context("expected_length", "2 or 3");
    }
    
    m_ocrLanguage = language;
    SLOG_DEBUG().message("OCR language set")
        .context("language", language);
}
void EnvironmentalPerception::setOCRConfidenceThreshold(float threshold) {
    // Validate threshold is in valid range
    if (threshold < 0.0f || threshold > 1.0f) {
        SLOG_ERROR().message("Invalid OCR confidence threshold")
            .context("threshold", threshold)
            .context("valid_range", "0.0 to 1.0");
        return;
    }
    
    m_ocrConfidenceThreshold = threshold;
    SLOG_DEBUG().message("OCR confidence threshold set")
        .context("threshold", threshold);
}
void EnvironmentalPerception::enableUIElementCaching(bool enable) { m_uiElementCaching = enable; }
void EnvironmentalPerception::setScreenshotFormat(const std::string& format) {
    // Validate format
    if (!burwell::InputValidator::isNotEmpty(format)) {
        SLOG_ERROR().message("Empty screenshot format provided");
        return;
    }
    
    // Convert to uppercase for comparison
    std::string upperFormat = utils::StringUtils::toUpperCase(format);
    
    // Validate against supported formats
    std::vector<std::string> supportedFormats = {"BMP", "PNG", "JPG", "JPEG"};
    if (std::find(supportedFormats.begin(), supportedFormats.end(), upperFormat) == supportedFormats.end()) {
        SLOG_ERROR().message("Unsupported screenshot format")
            .context("format", format)
            .context("supported", "BMP, PNG, JPG, JPEG");
        return;
    }
    
    m_screenshotFormat = upperFormat;
    SLOG_DEBUG().message("Screenshot format set")
        .context("format", upperFormat);
}
void EnvironmentalPerception::setScreenshotQuality(int quality) {
    // Validate quality is in valid range
    if (quality < 0 || quality > 100) {
        SLOG_ERROR().message("Invalid screenshot quality")
            .context("quality", quality)
            .context("valid_range", "0 to 100");
        return;
    }
    
    m_screenshotQuality = quality;
    SLOG_DEBUG().message("Screenshot quality set")
        .context("quality", quality);
}
void EnvironmentalPerception::clearCache() { }
void EnvironmentalPerception::enablePerformanceMode(bool enable) { m_performanceMode = enable; }
void EnvironmentalPerception::setMaxCacheSize(size_t maxSizeMB) {
    // Validate cache size is reasonable
    const size_t MAX_CACHE_SIZE_MB = 10240; // 10GB max
    if (maxSizeMB > MAX_CACHE_SIZE_MB) {
        SLOG_WARNING().message("Very large cache size requested")
            .context("requested_mb", maxSizeMB)
            .context("max_recommended_mb", MAX_CACHE_SIZE_MB);
    }
    
    m_maxCacheSize = maxSizeMB;
    SLOG_DEBUG().message("Max cache size set")
        .context("size_mb", maxSizeMB);
}
bool EnvironmentalPerception::startWindowMonitoring(WindowEventCallback callback) { 
    (void)callback; // TODO: Implement window event monitoring with callback
    return false; 
}
void EnvironmentalPerception::stopWindowMonitoring() { }
std::vector<UIElement> EnvironmentalPerception::getAccessibleElements(void* windowHandle) { 
    (void)windowHandle; // TODO: Implement accessibility element retrieval for window
    return {}; 
}
std::string EnvironmentalPerception::getElementAccessibleName(const UIElement& element) { 
    (void)element; // TODO: Implement accessibility name retrieval for element
    return ""; 
}
bool EnvironmentalPerception::isElementAccessible(const UIElement& element) { 
    (void)element; // TODO: Implement accessibility check for element
    return false; 
}
std::vector<ScreenRegion> EnvironmentalPerception::getMonitorBounds() { return {getDesktopBounds()}; }
int EnvironmentalPerception::getCurrentDPI() { return 96; }
float EnvironmentalPerception::getDisplayScaling() { return 1.0f; }
void EnvironmentalPerception::cleanupCache() { }
std::string EnvironmentalPerception::generateCacheKey(const std::string& type, const std::string& data) { return type + "_" + data; }
std::string EnvironmentalPerception::formatRegionString(const ScreenRegion& region) { 
    (void)region; // TODO: Implement region formatting to string
    return ""; 
}
bool EnvironmentalPerception::validateScreenRegion(const ScreenRegion& region) {
    // Get desktop bounds for validation
    ScreenRegion desktop = getDesktopBounds();
    
    // Check if region is within desktop bounds
    if (region.x < 0 || region.y < 0) {
        SLOG_DEBUG().message("Screen region has negative coordinates")
            .context("x", region.x)
            .context("y", region.y);
        return false;
    }
    
    if (region.width <= 0 || region.height <= 0) {
        SLOG_DEBUG().message("Screen region has invalid dimensions")
            .context("width", region.width)
            .context("height", region.height);
        return false;
    }
    
    if (region.x + region.width > desktop.width || region.y + region.height > desktop.height) {
        SLOG_DEBUG().message("Screen region extends beyond desktop bounds")
            .context("region_right", region.x + region.width)
            .context("region_bottom", region.y + region.height)
            .context("desktop_width", desktop.width)
            .context("desktop_height", desktop.height);
        return false;
    }
    
    return true;
}
Screenshot EnvironmentalPerception::convertToFormat(const Screenshot& screenshot, const std::string& format) { 
    (void)format; // TODO: Implement screenshot format conversion
    return screenshot; 
}

std::string EnvironmentalPerception::generateTextBasedScreenDescription(const nlohmann::json& envData) {
    std::ostringstream desc;
    
    try {
        desc << "SCREEN_STATE_ANALYSIS:\n";
        desc << "======================\n\n";
        
        // Desktop environment
        if (envData.contains("screen_resolution")) {
            auto resolution = envData["screen_resolution"];
            desc << "DESKTOP_ENVIRONMENT:\n";
            desc << "- Screen Resolution: " << resolution["width"] << "x" << resolution["height"] << "\n";
        }
        
        // Active window details
        if (envData.contains("active_window_details") && !envData["active_window_details"].is_null()) {
            auto activeWindow = envData["active_window_details"];
            desc << "- Active Window: \"" << activeWindow.value("title", "Unknown") << "\"\n";
            
            if (activeWindow.contains("bounds")) {
                auto bounds = activeWindow["bounds"];
                desc << "- Window Position: (" << bounds["x"] << ", " << bounds["y"] 
                     << ") Size: (" << bounds["width"] << "x" << bounds["height"] << ")\n";
                desc << "- Window State: ";
                if (activeWindow.value("isMaximized", false)) desc << "Maximized";
                else if (activeWindow.value("isMinimized", false)) desc << "Minimized";
                else desc << "Normal";
                desc << "\n";
                
                // Window controls
                if (activeWindow.contains("window_controls")) {
                    auto controls = activeWindow["window_controls"];
                    desc << "- Window Controls:\n";
                    if (controls.contains("close_button")) {
                        auto close = controls["close_button"];
                        desc << "  * Close Button: (" << close["x"] << ", " << close["y"] << ") - Red X button\n";
                    }
                    if (controls.contains("minimize_button")) {
                        auto minimize = controls["minimize_button"];
                        desc << "  * Minimize Button: (" << minimize["x"] << ", " << minimize["y"] << ") - Minimize to taskbar\n";
                    }
                    if (controls.contains("maximize_button")) {
                        auto maximize = controls["maximize_button"];
                        desc << "  * Maximize Button: (" << maximize["x"] << ", " << maximize["y"] << ") - Toggle maximized state\n";
                    }
                }
            }
        }
        desc << "\n";
        
        // All visible windows
        if (envData.contains("detailed_windows") && envData["detailed_windows"].is_array()) {
            desc << "VISIBLE_WINDOWS:\n";
            auto windows = envData["detailed_windows"];
            int windowCount = 0;
            for (const auto& window : windows) {
                if (window.value("isVisible", false) && !window.value("isMinimized", false)) {
                    windowCount++;
                    desc << windowCount << ". \"" << window.value("title", "Unknown") << "\"\n";
                    if (window.contains("bounds")) {
                        auto bounds = window["bounds"];
                        desc << "   Position: (" << bounds["x"] << ", " << bounds["y"] 
                             << ") Size: (" << bounds["width"] << "x" << bounds["height"] << ")\n";
                    }
                    desc << "   Process: " << window.value("className", "Unknown") << "\n";
                }
            }
            if (windowCount == 0) {
                desc << "- No visible windows detected\n";
            }
        }
        desc << "\n";
        
        // Screen text content from OCR
        if (envData.contains("screen_text_content") && !envData["screen_text_content"].get<std::string>().empty()) {
            desc << "SCREEN_TEXT_CONTENT:\n";
            desc << "- OCR Confidence: " << envData.value("ocr_confidence", 0.0f) << "\n";
            desc << "- Extracted Text: \"" << envData["screen_text_content"].get<std::string>() << "\"\n";
            
            // Individual text elements with positions
            if (envData.contains("text_elements") && envData["text_elements"].is_array()) {
                desc << "- Text Element Positions:\n";
                int elementCount = 0;
                for (const auto& element : envData["text_elements"]) {
                    elementCount++;
                    auto bounds = element["bounds"];
                    desc << "  " << elementCount << ". \"" << element["text"].get<std::string>() 
                         << "\" at (" << bounds["x"] << ", " << bounds["y"] << ")\n";
                    if (elementCount >= 10) { // Limit to first 10 elements to avoid spam
                        desc << "  ... (and " << (envData["text_elements"].size() - 10) << " more text elements)\n";
                        break;
                    }
                }
            }
        } else {
            desc << "SCREEN_TEXT_CONTENT:\n";
            desc << "- No text content detected on screen\n";
        }
        desc << "\n";
        
        // System information
        if (envData.contains("currentDirectory") && !envData["currentDirectory"].get<std::string>().empty()) {
            desc << "SYSTEM_CONTEXT:\n";
            desc << "- Current Directory: " << envData["currentDirectory"].get<std::string>() << "\n";
        }
        
        // Basic tab analysis without configuration
        if (envData.contains("text_elements") && envData["text_elements"].is_array()) {
            // Use basic text patterns instead of configuration
            std::vector<nlohmann::json> tabCloseButtons;
            
            // Basic tab patterns (hardcoded for simplicity)
            std::map<std::string, std::vector<std::string>> basicPatterns = {
                {"settings", {"settings", "preferences", "options"}},
                {"extensions", {"extensions", "addons", "plugins"}},
                {"downloads", {"downloads"}},
                {"history", {"history"}}
            };
            std::map<std::string, std::vector<nlohmann::json>> tabsByType;
            
            for (const auto& element : envData["text_elements"]) {
                std::string text = utils::StringUtils::toLowerCase(element["text"].get<std::string>());
                
                // Check against basic patterns
                for (const auto& patternType : basicPatterns) {
                    const std::string& patternName = patternType.first;
                    const std::vector<std::string>& patterns = patternType.second;
                    
                    // Check if text matches any pattern for this type
                    for (const std::string& pattern : patterns) {
                        std::string lowerPattern = utils::StringUtils::toLowerCase(pattern);
                        
                        if (text.find(lowerPattern) != std::string::npos) {
                            tabsByType[patternName].push_back(element);
                            break; // Found match for this pattern type
                        }
                    }
                }
                
                // Look for tab close buttons (configurable patterns)
                std::vector<std::string> closeButtonPatterns = {"×", "x", "✕", "close"};
                for (const std::string& closePattern : closeButtonPatterns) {
                    if (text == closePattern) {
                        auto bounds = element["bounds"];
                        int y = bounds["y"].get<int>();
                        // Tab close buttons are typically in the upper area (y < 100 for most screens)
                        if (y < 100) {
                            tabCloseButtons.push_back(element);
                            break;
                        }
                    }
                }
            }
            
            // Generate analysis for each tab type found
            if (!tabsByType.empty()) {
                desc << "\nBROWSER_TAB_ANALYSIS:\n";
                for (const auto& tabType : tabsByType) {
                    const std::string& typeName = tabType.first;
                    const std::vector<nlohmann::json>& tabs = tabType.second;
                    
                    if (!tabs.empty()) {
                        desc << "- Found " << tabs.size() << " " << typeName << " tab(s):\n";
                        for (size_t i = 0; i < tabs.size(); i++) {
                            auto bounds = tabs[i]["bounds"];
                            desc << "  " << (i+1) << ". " << typeName << " tab at (" << bounds["x"] << ", " << bounds["y"] << ")\n";
                            
                            // Find closest close button to this tab
                            int tabX = bounds["x"].get<int>();
                            int tabY = bounds["y"].get<int>();
                            int minDistance = 999999;
                            nlohmann::json closestCloseButton;
                            
                            for (const auto& closeBtn : tabCloseButtons) {
                                auto closeBounds = closeBtn["bounds"];
                                int closeX = closeBounds["x"].get<int>();
                                int closeY = closeBounds["y"].get<int>();
                                
                                // Calculate distance (prioritize horizontal proximity)
                                int distance = abs(closeX - tabX) + abs(closeY - tabY) * 2;
                                if (distance < minDistance) {
                                    minDistance = distance;
                                    closestCloseButton = closeBtn;
                                }
                            }
                            
                            if (!closestCloseButton.empty()) {
                                auto closeBounds = closestCloseButton["bounds"];
                                desc << "     → Close button at (" << closeBounds["x"] << ", " << closeBounds["y"] << ")\n";
                            }
                        }
                    }
                }
            }
            
            if (!tabCloseButtons.empty()) {
                desc << "\nTAB_CLOSE_BUTTONS:\n";
                desc << "- Found " << tabCloseButtons.size() << " tab close button(s) at top of screen\n";
            }
        }
        
        // Add interaction guidance
        desc << "\nINTERACTION_GUIDANCE:\n";
        desc << "- Use MOUSE_CLICK[x=X, y=Y] for precise coordinate clicking\n";
        desc << "- Use MOUSE_MOVE[x=X, y=Y] for cursor positioning\n";
        desc << "- Use KEY_TYPE[text=\"text\"] for text input\n";
        desc << "- Use KEY_COMBO[keys=\"ctrl+c\"] for keyboard shortcuts\n";
        desc << "- Window controls are typically in the top-right corner\n";
        desc << "- Check OCR text content for clickable buttons and links\n";
        desc << "- For browser tabs: Click close button coordinates provided in BROWSER_TAB_ANALYSIS\n";
        
        SLOG_DEBUG().message("Generated text-based screen description")
            .context("character_count", desc.str().length());
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to generate text description")
            .context("error", e.what());
        desc << "ERROR: Failed to generate screen description - " << e.what() << "\n";
    }
    
    return desc.str();
}