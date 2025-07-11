#include "string_utils.h"
#include "structured_logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace burwell {
namespace utils {

std::string StringUtils::replaceAll(const std::string& str, const std::string& from, const std::string& to) {
    // Input validation
    if (from.empty()) {
        SLOG_ERROR().message("Empty 'from' parameter in replaceAll");
        return str; // Return original string on invalid input
    }

    std::string result = str;
    size_t startPos = 0;
    
    try {
        while ((startPos = result.find(from, startPos)) != std::string::npos) {
            result.replace(startPos, from.length(), to);
            startPos += to.length(); // Move past the replacement to avoid infinite loop
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in replaceAll").context("error", e.what());
        return str; // Return original string on error
    }

    return result;
}

std::string StringUtils::trim(const std::string& str) {
    if (str.empty()) {
        return str;
    }

    try {
        // Find first non-whitespace character
        size_t first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos) {
            return ""; // String contains only whitespace
        }

        // Find last non-whitespace character
        size_t last = str.find_last_not_of(" \t\n\r\f\v");
        
        return str.substr(first, (last - first + 1));
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in trim").context("error", e.what());
        return str; // Return original string on error
    }
}

std::string StringUtils::trimLeft(const std::string& str) {
    if (str.empty()) {
        return str;
    }

    try {
        size_t first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos) {
            return ""; // String contains only whitespace
        }
        
        return str.substr(first);
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in trimLeft").context("error", e.what());
        return str; // Return original string on error
    }
}

std::string StringUtils::trimRight(const std::string& str) {
    if (str.empty()) {
        return str;
    }

    try {
        size_t last = str.find_last_not_of(" \t\n\r\f\v");
        if (last == std::string::npos) {
            return ""; // String contains only whitespace
        }
        
        return str.substr(0, last + 1);
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in trimRight").context("error", e.what());
        return str; // Return original string on error
    }
}

std::vector<std::string> StringUtils::split(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> result;
    
    // Input validation
    if (delimiter.empty()) {
        SLOG_ERROR().message("Empty delimiter in split");
        if (!str.empty()) {
            result.push_back(str); // Return original string as single element
        }
        return result;
    }

    if (str.empty()) {
        return result; // Return empty vector for empty string
    }

    try {
        size_t start = 0;
        size_t end = 0;
        
        while ((end = str.find(delimiter, start)) != std::string::npos) {
            result.push_back(str.substr(start, end - start));
            start = end + delimiter.length();
        }
        
        // Add the last part (or the only part if no delimiter found)
        result.push_back(str.substr(start));
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in split").context("error", e.what());
        result.clear();
        if (!str.empty()) {
            result.push_back(str); // Return original string as fallback
        }
    }

    return result;
}

bool StringUtils::startsWith(const std::string& str, const std::string& prefix) {
    // Input validation
    if (prefix.empty()) {
        SLOG_ERROR().message("Empty prefix in startsWith");
        return false;
    }

    if (str.length() < prefix.length()) {
        return false;
    }

    try {
        return str.compare(0, prefix.length(), prefix) == 0;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in startsWith").context("error", e.what());
        return false;
    }
}

bool StringUtils::endsWith(const std::string& str, const std::string& suffix) {
    // Input validation
    if (suffix.empty()) {
        SLOG_ERROR().message("Empty suffix in endsWith");
        return false;
    }

    if (str.length() < suffix.length()) {
        return false;
    }

    try {
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in endsWith").context("error", e.what());
        return false;
    }
}

std::string StringUtils::toLowerCase(const std::string& str) {
    std::string result = str;
    
    try {
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in toLowerCase").context("error", e.what());
        return str; // Return original string on error
    }
    
    return result;
}

std::string StringUtils::toUpperCase(const std::string& str) {
    std::string result = str;
    
    try {
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::toupper(c); });
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in toUpperCase").context("error", e.what());
        return str; // Return original string on error
    }
    
    return result;
}

std::string StringUtils::join(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) {
        return "";
    }

    try {
        std::stringstream result;
        
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0) {
                result << delimiter;
            }
            result << strings[i];
        }
        
        return result.str();
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in join").context("error", e.what());
        return ""; // Return empty string on error
    }
}

bool StringUtils::isWhitespaceOnly(const std::string& str) {
    if (str.empty()) {
        return true;
    }

    try {
        return std::all_of(str.begin(), str.end(), 
                          [](unsigned char c) { return isWhitespace(c); });
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in isWhitespaceOnly").context("error", e.what());
        return false;
    }
}

std::string StringUtils::escapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.length() * 2); // Reserve space for potential escaping
    
    try {
        for (char c : str) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:
                    if (c >= 0 && c < 32) {
                        // Escape other control characters
                        std::stringstream ss;
                        ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
                        result += ss.str();
                    } else {
                        result += c;
                    }
                    break;
            }
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Exception in escapeJsonString").context("error", e.what());
        return str; // Return original string on error
    }
    
    return result;
}

// Private helper functions
bool StringUtils::isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

} // namespace utils
} // namespace burwell