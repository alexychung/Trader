#pragma once

#include <string>
#include <utility>
#include <vector>

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

// GET with arbitrary header overrides. The internal defaults (Host, Accept:
// application/json, User-Agent: "Trader/0.1.0 (backtest)") are still applied,
// but any header named here replaces or supplements them. Used by the NBA
// feed to send a browser-like User-Agent + Referer to cdn.nba.com, which
// otherwise treats the default UA inconsistently.
//
// Header names are case-insensitive per RFC. If you set "User-Agent",
// "user-agent", or "USER-AGENT" the override takes precedence over the
// default. Setting a header to an empty string deletes it.
HttpResult https_get_with_headers(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& extra_headers);

} // namespace trader
