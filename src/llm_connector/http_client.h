#ifndef BURWELL_HTTP_CLIENT_H
#define BURWELL_HTTP_CLIENT_H

#include <string>
#include <map>
#include <functional>
#include <memory>

namespace burwell {

struct HttpResponse {
    int statusCode;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success;
    std::string errorMessage;
    
    HttpResponse() : statusCode(0), success(false) {}
};

struct HttpRequest {
    std::string url;
    std::string method;
    std::string body;
    std::map<std::string, std::string> headers;
    int timeoutMs;
    
    HttpRequest() : method("GET"), timeoutMs(30000) {}
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Core HTTP methods
    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    HttpResponse post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});
    HttpResponse put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});
    HttpResponse delete_(const std::string& url, const std::map<std::string, std::string>& headers = {});
    
    // Generic request method
    HttpResponse request(const HttpRequest& req);
    
    // Configuration
    void setTimeout(int timeoutMs);
    void setUserAgent(const std::string& userAgent);
    void setMaxRetries(int maxRetries);
    void setRetryDelay(int delayMs);
    
    // SSL/TLS configuration
    void setVerifySSL(bool verify);
    void setCertificatePath(const std::string& path);
    
    // Proxy configuration
    void setProxy(const std::string& proxyUrl);
    void setProxyAuth(const std::string& username, const std::string& password);
    
    // Connection pooling
    void enableConnectionPooling(bool enable);
    void setMaxConnections(int maxConnections);
    
    // Request/Response hooks
    using RequestHook = std::function<void(HttpRequest&)>;
    using ResponseHook = std::function<void(const HttpResponse&)>;
    
    void setRequestHook(RequestHook hook);
    void setResponseHook(ResponseHook hook);

private:
    struct HttpClientImpl;
    std::unique_ptr<HttpClientImpl> m_impl;
    
    // Configuration
    int m_timeoutMs;
    std::string m_userAgent;
    int m_maxRetries;
    int m_retryDelay;
    bool m_verifySSL;
    std::string m_certificatePath;
    std::string m_proxyUrl;
    std::string m_proxyUsername;
    std::string m_proxyPassword;
    bool m_connectionPooling;
    int m_maxConnections;
    
    // Hooks
    RequestHook m_requestHook;
    ResponseHook m_responseHook;
    
    // Internal methods
    HttpResponse performRequest(const HttpRequest& request);
    HttpResponse retryRequest(const HttpRequest& request);
    void logRequest(const HttpRequest& request);
    void logResponse(const HttpResponse& response);
};

} // namespace burwell

#endif // BURWELL_HTTP_CLIENT_H