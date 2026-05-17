#pragma once

#include <string>

namespace trader {

// Simple public HTTPS client for unauthenticated APIs (FRED/ALFRED, BLS,
// Open-Meteo). Kalshi endpoints use KalshiRestClient, which handles RSA-PSS
// auth. Keeping this helper separate avoids disturbing the authenticated code
// path.

struct HttpResult {
    int status_code = 0;
    std::string body;
    bool ok() const { return status_code >= 200 && status_code < 300; }
};

// GET an absolute URL (https://host[:port]/path?query). Returns {0, error} on
// transport failure.
HttpResult https_get(const std::string& url);

// POST an absolute URL with a JSON body.
HttpResult https_post_json(const std::string& url, const std::string& json_body);

} // namespace trader
