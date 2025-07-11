#include "http_client.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include <thread>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#undef ERROR  // Undefine the Windows ERROR macro to avoid conflicts
#endif

namespace burwell {

#ifdef _WIN32
struct HttpClient::HttpClientImpl {
    HINTERNET hInternet;
    
    HttpClientImpl() : hInternet(nullptr) {
        hInternet = InternetOpenA("Burwell/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!hInternet) {
            BURWELL_HANDLE_ERROR(ErrorType::LLM_CONNECTION_ERROR, ErrorSeverity::HIGH,
                                  "Failed to initialize WinINet", "", "HttpClient::HttpClientImpl");
        }
    }
    
    ~HttpClientImpl() {
        if (hInternet) {
            InternetCloseHandle(hInternet);
        }
    }
};
#else
struct HttpClient::HttpClientImpl {
    // Placeholder for non-Windows platforms
    HttpClientImpl() {}
    ~HttpClientImpl() {}
};
#endif

HttpClient::HttpClient() 
    : m_impl(std::make_unique<HttpClientImpl>())
    , m_timeoutMs(30000)
    , m_userAgent("Burwell/1.0")
    , m_maxRetries(3)
    , m_retryDelay(1000)
    , m_verifySSL(true)
    , m_connectionPooling(true)
    , m_maxConnections(10) {
    
    SLOG_DEBUG().message("HttpClient initialized");
}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    request.timeoutMs = m_timeoutMs;
    
    return retryRequest(request);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    request.timeoutMs = m_timeoutMs;
    
    return retryRequest(request);
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    request.timeoutMs = m_timeoutMs;
    
    return retryRequest(request);
}

HttpResponse HttpClient::delete_(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "DELETE";
    request.headers = headers;
    request.timeoutMs = m_timeoutMs;
    
    return retryRequest(request);
}

HttpResponse HttpClient::request(const HttpRequest& req) {
    return retryRequest(req);
}

HttpResponse HttpClient::performRequest(const HttpRequest& request) {
    HttpResponse response;
    
    try {
        logRequest(request);
        
        if (m_requestHook) {
            HttpRequest modifiableRequest = request;
            m_requestHook(modifiableRequest);
        }
        
#ifdef _WIN32
        if (!m_impl->hInternet) {
            response.errorMessage = "WinINet not initialized";
            return response;
        }
        
        // Parse URL
        std::string protocol, hostname, path;
        int port = 80;
        
        size_t protocolEnd = request.url.find("://");
        if (protocolEnd != std::string::npos) {
            protocol = request.url.substr(0, protocolEnd);
            size_t hostStart = protocolEnd + 3;
            size_t pathStart = request.url.find('/', hostStart);
            
            if (pathStart != std::string::npos) {
                hostname = request.url.substr(hostStart, pathStart - hostStart);
                path = request.url.substr(pathStart);
            } else {
                hostname = request.url.substr(hostStart);
                path = "/";
            }
            
            if (protocol == "https") {
                port = 443;
            }
            
            // Check for port in hostname
            size_t colonPos = hostname.find(':');
            if (colonPos != std::string::npos) {
                port = std::stoi(hostname.substr(colonPos + 1));
                hostname = hostname.substr(0, colonPos);
            }
        }
        
        // Connect to server
        HINTERNET hConnect = InternetConnectA(m_impl->hInternet, hostname.c_str(), port, 
                                              nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        
        if (!hConnect) {
            response.errorMessage = "Failed to connect to server";
            return response;
        }
        
        // Create request
        DWORD requestFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (protocol == "https") {
            requestFlags |= INTERNET_FLAG_SECURE;
        }
        
        HINTERNET hRequest = HttpOpenRequestA(hConnect, request.method.c_str(), path.c_str(),
                                              nullptr, nullptr, nullptr, requestFlags, 0);
        
        if (!hRequest) {
            InternetCloseHandle(hConnect);
            response.errorMessage = "Failed to create HTTP request";
            return response;
        }
        
        // Set timeout
        DWORD timeout = request.timeoutMs;
        InternetSetOptionA(hRequest, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hRequest, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hRequest, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        
        // Build headers string
        std::string headersStr;
        for (const auto& header : request.headers) {
            headersStr += header.first + ": " + header.second + "\r\n";
        }
        
        // Send request
        BOOL result = HttpSendRequestA(hRequest, 
                                       headersStr.empty() ? nullptr : headersStr.c_str(),
                                       headersStr.length(),
                                       request.body.empty() ? nullptr : (LPVOID)request.body.c_str(),
                                       request.body.length());
        
        if (!result) {
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            response.errorMessage = "Failed to send HTTP request";
            return response;
        }
        
        // Get status code
        DWORD statusCode;
        DWORD statusCodeSize = sizeof(statusCode);
        HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusCodeSize, nullptr);
        response.statusCode = statusCode;
        
        // Read response headers
        DWORD headerSize = 0;
        HttpQueryInfoA(hRequest, HTTP_QUERY_RAW_HEADERS_CRLF, nullptr, &headerSize, nullptr);
        if (headerSize > 0) {
            std::vector<char> headerBuffer(headerSize);
            if (HttpQueryInfoA(hRequest, HTTP_QUERY_RAW_HEADERS_CRLF, 
                               headerBuffer.data(), &headerSize, nullptr)) {
                std::string headerStr(headerBuffer.data(), headerSize);
                // Parse headers (simplified)
                std::istringstream iss(headerStr);
                std::string line;
                while (std::getline(iss, line) && !line.empty()) {
                    size_t colonPos = line.find(':');
                    if (colonPos != std::string::npos) {
                        std::string key = line.substr(0, colonPos);
                        std::string value = line.substr(colonPos + 2);
                        response.headers[key] = value;
                    }
                }
            }
        }
        
        // Read response body
        std::string responseBody;
        DWORD bytesRead;
        char buffer[4096];
        
        while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer, bytesRead);
        }
        
        response.body = responseBody;
        response.success = (statusCode >= 200 && statusCode < 300);
        
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        
#else
        // Non-Windows simulation
        response.statusCode = 200;
        response.body = R"({"simulated": true, "message": "HTTP client simulated on non-Windows platform"})";
        response.success = true;
        SLOG_DEBUG().message("HTTP request simulated (non-Windows platform)");
#endif
        
        logResponse(response);
        
        if (m_responseHook) {
            m_responseHook(response);
        }
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().handleException(e, "HttpClient::performRequest");
    }
    
    return response;
}

HttpResponse HttpClient::retryRequest(const HttpRequest& request) {
    HttpResponse response;
    
    for (int attempt = 0; attempt <= m_maxRetries; ++attempt) {
        response = performRequest(request);
        
        if (response.success) {
            break;
        }
        
        if (attempt < m_maxRetries) {
            SLOG_WARNING().message("HTTP request failed, retrying").context("attempt", 
                       std::to_string(attempt + 1) + "/" + std::to_string(m_maxRetries + 1) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(m_retryDelay));
        }
    }
    
    if (!response.success) {
        BURWELL_HANDLE_ERROR(ErrorType::LLM_CONNECTION_ERROR, ErrorSeverity::HIGH,
                              "HTTP request failed after retries", response.errorMessage, 
                              "HttpClient::retryRequest");
    }
    
    return response;
}

void HttpClient::logRequest(const HttpRequest& request) {
    SLOG_DEBUG().message("HTTP request").context("method", request.method).context("url", request.url);
    if (!request.body.empty() && request.body.length() < 500) {
        SLOG_DEBUG().message("Request body").context("body", request.body);
    }
}

void HttpClient::logResponse(const HttpResponse& response) {
    SLOG_DEBUG().message("HTTP Response").context("status_code", response.statusCode);
    if (!response.body.empty() && response.body.length() < 500) {
        SLOG_DEBUG().message("Response body").context("body", response.body);
    }
}

// Configuration methods
void HttpClient::setTimeout(int timeoutMs) { m_timeoutMs = timeoutMs; }
void HttpClient::setUserAgent(const std::string& userAgent) { m_userAgent = userAgent; }
void HttpClient::setMaxRetries(int maxRetries) { m_maxRetries = maxRetries; }
void HttpClient::setRetryDelay(int delayMs) { m_retryDelay = delayMs; }
void HttpClient::setVerifySSL(bool verify) { m_verifySSL = verify; }
void HttpClient::setCertificatePath(const std::string& path) { m_certificatePath = path; }
void HttpClient::setProxy(const std::string& proxyUrl) { m_proxyUrl = proxyUrl; }
void HttpClient::setProxyAuth(const std::string& username, const std::string& password) {
    m_proxyUsername = username;
    m_proxyPassword = password;
}
void HttpClient::enableConnectionPooling(bool enable) { m_connectionPooling = enable; }
void HttpClient::setMaxConnections(int maxConnections) { m_maxConnections = maxConnections; }
void HttpClient::setRequestHook(RequestHook hook) { m_requestHook = hook; }
void HttpClient::setResponseHook(ResponseHook hook) { m_responseHook = hook; }

} // namespace burwell