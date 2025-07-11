#include "credential_manager.h"
#include "structured_logger.h"
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <fstream>
#include <filesystem>
#include "file_utils.h"
#include "string_utils.h"
#endif

namespace burwell {

CredentialManager& CredentialManager::getInstance() {
    static CredentialManager instance;
    return instance;
}

std::optional<std::string> CredentialManager::getFromEnvironment(const std::string& envVar) const {
    if (envVar.empty()) {
        return std::nullopt;
    }

    const char* value = std::getenv(envVar.c_str());
    if (value != nullptr && strlen(value) > 0) {
        SLOG_DEBUG().message("Retrieved credential from environment variable").context("env_var", envVar);
        return std::string(value);
    }
    
    return std::nullopt;
}

std::optional<std::string> CredentialManager::getCredentialWithFallback(const std::string& key, const std::string& envVar) {
    // First try to get from secure storage
    auto credential = getCredential(key);
    if (credential.has_value()) {
        return credential;
    }

    // Fall back to environment variable
    return getFromEnvironment(envVar);
}

bool CredentialManager::storeCredential(const std::string& key, const std::string& value) {
    if (key.empty() || value.empty()) {
        SLOG_ERROR().message("Cannot store credential with empty key or value");
        return false;
    }

    // Store in cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[key] = value;
    }

#ifdef _WIN32
    return storeWindowsCredential(key, value);
#else
    return storeFileCredential(key, value);
#endif
}

std::optional<std::string> CredentialManager::getCredential(const std::string& key) {
    if (key.empty()) {
        return std::nullopt;
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return it->second;
        }
    }

    // Try to get from secure storage
#ifdef _WIN32
    auto credential = getWindowsCredential(key);
#else
    auto credential = getFileCredential(key);
#endif

    // Cache the result if found
    if (credential.has_value()) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[key] = credential.value();
    }

    return credential;
}

bool CredentialManager::deleteCredential(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    // Remove from cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.erase(key);
    }

#ifdef _WIN32
    return deleteWindowsCredential(key);
#else
    return deleteFileCredential(key);
#endif
}

bool CredentialManager::hasCredential(const std::string& key) const {
    if (key.empty()) {
        return false;
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(key) != m_cache.end()) {
            return true;
        }
    }

    // Check secure storage
#ifdef _WIN32
    return const_cast<CredentialManager*>(this)->getWindowsCredential(key).has_value();
#else
    return const_cast<CredentialManager*>(this)->getFileCredential(key).has_value();
#endif
}

void CredentialManager::clearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
    SLOG_DEBUG().message("Credential cache cleared");
}

#ifdef _WIN32

bool CredentialManager::storeWindowsCredential(const std::string& key, const std::string& value) {
    std::string targetName = "Burwell_" + key;
    std::wstring wTargetName(targetName.begin(), targetName.end());
    std::wstring wComment = L"Burwell secure credential";
    
    CREDENTIALW cred;
    memset(&cred, 0, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(wTargetName.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(value.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.Comment = const_cast<wchar_t*>(wComment.c_str());

    BOOL result = CredWriteW(&cred, 0);
    
    if (result) {
        SLOG_INFO().message("Credential stored securely").context("key", key);
        return true;
    } else {
        DWORD error = GetLastError();
        SLOG_ERROR().message("Failed to store credential").context("key", key).context("error", std::to_string(error));
        return false;
    }
}

std::optional<std::string> CredentialManager::getWindowsCredential(const std::string& key) {
    std::string targetName = "Burwell_" + key;
    std::wstring wTargetName(targetName.begin(), targetName.end());
    
    PCREDENTIALW pcred;
    BOOL result = CredReadW(wTargetName.c_str(), CRED_TYPE_GENERIC, 0, &pcred);
    
    if (result) {
        std::string value(reinterpret_cast<char*>(pcred->CredentialBlob), pcred->CredentialBlobSize);
        CredFree(pcred);
        SLOG_DEBUG().message("Retrieved credential").context("key", key);
        return value;
    } else {
        DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            SLOG_WARNING().message("Failed to retrieve credential").context("key", key).context("error", std::to_string(error));
        }
        return std::nullopt;
    }
}

bool CredentialManager::deleteWindowsCredential(const std::string& key) {
    std::string targetName = "Burwell_" + key;
    std::wstring wTargetName(targetName.begin(), targetName.end());
    
    BOOL result = CredDeleteW(wTargetName.c_str(), CRED_TYPE_GENERIC, 0);
    
    if (result) {
        SLOG_INFO().message("Credential deleted").context("key", key);
        return true;
    } else {
        DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            SLOG_WARNING().message("Failed to delete credential").context("key", key).context("error", std::to_string(error));
        }
        return false;
    }
}

#else

// For non-Windows platforms, implement a simple encrypted file storage
// This is a placeholder implementation - in production, use proper encryption

bool CredentialManager::storeFileCredential(const std::string& key, const std::string& value) {
    // Avoid unused parameter warning
    (void)key;
    (void)value;
    
    // TODO: Implement proper encryption for non-Windows platforms
    SLOG_WARNING().message("File-based credential storage not yet implemented for non-Windows platforms");
    return false;
}

std::optional<std::string> CredentialManager::getFileCredential(const std::string& key) {
    // Avoid unused parameter warning
    (void)key;
    
    // TODO: Implement proper encryption for non-Windows platforms
    SLOG_WARNING().message("File-based credential retrieval not yet implemented for non-Windows platforms");
    return std::nullopt;
}

bool CredentialManager::deleteFileCredential(const std::string& key) {
    // Avoid unused parameter warning
    (void)key;
    
    // TODO: Implement proper encryption for non-Windows platforms
    SLOG_WARNING().message("File-based credential deletion not yet implemented for non-Windows platforms");
    return false;
}

#endif

} // namespace burwell