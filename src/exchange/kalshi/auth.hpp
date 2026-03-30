#pragma once

#include <string>
#include <chrono>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rsa.h>

namespace trader::kalshi {

// RSA-PSS signer for Kalshi API authentication.
// Signs: timestamp_ms + method + path
// Uses SHA256 with PSS padding, DIGEST_LENGTH salt.
class KalshiAuth {
public:
    KalshiAuth() = default;

    // Load RSA private key from PEM file
    bool load_key(const std::string& pem_path);

    // Load RSA private key from PEM string (for testing)
    bool load_key_from_string(const std::string& pem_data);

    void set_api_key_id(const std::string& key_id) { api_key_id_ = key_id; }
    const std::string& api_key_id() const { return api_key_id_; }

    // Generate timestamp in milliseconds
    static int64_t timestamp_ms();

    // Sign message with RSA-PSS (SHA256, salt=DIGEST_LENGTH)
    // Returns base64-encoded signature, or empty string on failure.
    std::string sign(const std::string& message) const;

    // Build the three auth headers for a request
    struct AuthHeaders {
        std::string key;        // KALSHI-ACCESS-KEY
        std::string timestamp;  // KALSHI-ACCESS-TIMESTAMP (ms)
        std::string signature;  // KALSHI-ACCESS-SIGNATURE (base64)
    };

    AuthHeaders make_headers(const std::string& method, const std::string& path) const;
    AuthHeaders make_headers(const std::string& method, const std::string& path, int64_t ts_ms) const;

    bool is_loaded() const { return pkey_ != nullptr; }

private:
    EVP_PKEY* pkey_ = nullptr;
    std::string api_key_id_;
};

} // namespace trader::kalshi
