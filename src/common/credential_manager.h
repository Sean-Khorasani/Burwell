#ifndef BURWELL_CREDENTIAL_MANAGER_H
#define BURWELL_CREDENTIAL_MANAGER_H

#include <string>
#include <optional>
#include <unordered_map>
#include <mutex>

namespace burwell {

/**
 * @class CredentialManager
 * @brief Secure storage and retrieval of sensitive credentials
 * 
 * This class provides a secure way to store and retrieve credentials like API keys.
 * On Windows, it uses the Windows Credential Manager (DPAPI).
 * Credentials are never stored in plain text in configuration files.
 */
class CredentialManager {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the credential manager instance
     */
    static CredentialManager& getInstance();

    /**
     * @brief Store a credential securely
     * @param key The identifier for the credential
     * @param value The credential value to store
     * @return true if stored successfully, false otherwise
     */
    bool storeCredential(const std::string& key, const std::string& value);

    /**
     * @brief Retrieve a credential
     * @param key The identifier for the credential
     * @return The credential value if found, empty optional otherwise
     */
    std::optional<std::string> getCredential(const std::string& key);

    /**
     * @brief Delete a credential
     * @param key The identifier for the credential
     * @return true if deleted successfully, false otherwise
     */
    bool deleteCredential(const std::string& key);

    /**
     * @brief Check if a credential exists
     * @param key The identifier for the credential
     * @return true if the credential exists, false otherwise
     */
    bool hasCredential(const std::string& key) const;

    /**
     * @brief Get credential from environment variable as fallback
     * @param envVar The environment variable name
     * @return The credential value if found, empty optional otherwise
     */
    std::optional<std::string> getFromEnvironment(const std::string& envVar) const;

    /**
     * @brief Get credential with fallback to environment variable
     * @param key The identifier for the credential
     * @param envVar The environment variable to check if key not found
     * @return The credential value if found, empty optional otherwise
     */
    std::optional<std::string> getCredentialWithFallback(const std::string& key, const std::string& envVar);

    /**
     * @brief Clear all cached credentials (in-memory cache only)
     */
    void clearCache();

private:
    CredentialManager() = default;
    ~CredentialManager() = default;
    
    // Prevent copying
    CredentialManager(const CredentialManager&) = delete;
    CredentialManager& operator=(const CredentialManager&) = delete;

    // In-memory cache for frequently accessed credentials
    mutable std::mutex m_cacheMutex;
    std::unordered_map<std::string, std::string> m_cache;

    // Platform-specific credential storage
#ifdef _WIN32
    bool storeWindowsCredential(const std::string& key, const std::string& value);
    std::optional<std::string> getWindowsCredential(const std::string& key);
    bool deleteWindowsCredential(const std::string& key);
#else
    // For non-Windows platforms, we'll use encrypted file storage
    bool storeFileCredential(const std::string& key, const std::string& value);
    std::optional<std::string> getFileCredential(const std::string& key);
    bool deleteFileCredential(const std::string& key);
#endif
};

} // namespace burwell

#endif // BURWELL_CREDENTIAL_MANAGER_H