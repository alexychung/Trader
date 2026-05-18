#include "core/http.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <spdlog/spdlog.h>
#include <regex>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace trader {

namespace {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;  // path + query, e.g. "/fred/series/observations?..."
    bool valid = false;
};

ParsedUrl parse_absolute_url(const std::string& url) {
    static const std::regex re(R"((https?)://([^/:?#]+)(?::(\d+))?([/?#].*)?)");
    std::smatch m;
    ParsedUrl out;
    if (!std::regex_match(url, m, re)) return out;
    out.scheme = m[1].str();
    out.host = m[2].str();
    out.port = m[3].matched ? m[3].str() : (out.scheme == "https" ? "443" : "80");
    out.target = m[4].matched ? m[4].str() : "/";
    if (out.target.empty()) out.target = "/";
    out.valid = true;
    return out;
}

HttpResult do_request(http::verb verb, const std::string& url,
                      const std::string* json_body,
                      const std::vector<std::pair<std::string, std::string>>* extra_headers) {
    auto parsed = parse_absolute_url(url);
    if (!parsed.valid) return {0, "invalid url: " + url};
    if (parsed.scheme != "https") {
        // This helper is HTTPS-only by design. All backtest data sources are https.
        return {0, "only https supported: " + url};
    }

    // Per-operation timeout. Without this, a single slow endpoint (observed:
    // Open-Meteo returning >30s for one station) blocks the entire sync
    // refresh serialized across 18 stations. 10s is plenty for responsive
    // APIs and forces us to fail fast on the pathological ones.
    constexpr auto kOpTimeout = std::chrono::seconds(10);

    try {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), parsed.host.c_str())) {
            return {0, "SNI setup failed"};
        }

        // Bound each phase with the same timeout. beast::tcp_stream::connect
        // (and subsequent sync reads/writes) honor the deadline set by
        // expires_after and will throw on elapsed-time error.
        beast::get_lowest_layer(stream).expires_after(kOpTimeout);
        auto const results = resolver.resolve(parsed.host, parsed.port);
        beast::get_lowest_layer(stream).connect(results);

        beast::get_lowest_layer(stream).expires_after(kOpTimeout);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req(verb, parsed.target, 11);
        req.set(http::field::host, parsed.host);
        req.set(http::field::user_agent, "Trader/0.1.0 (backtest)");
        req.set(http::field::accept, "application/json");
        if (json_body) {
            req.set(http::field::content_type, "application/json");
            req.body() = *json_body;
            req.prepare_payload();
        }
        // Apply caller-provided header overrides last so they win over defaults.
        // An empty value erases the header (Beast erase is by name, case-insensitive).
        if (extra_headers) {
            for (const auto& [name, value] : *extra_headers) {
                req.erase(name);
                if (!value.empty()) req.set(name, value);
            }
        }

        beast::get_lowest_layer(stream).expires_after(kOpTimeout);
        http::write(stream, req);

        beast::get_lowest_layer(stream).expires_after(kOpTimeout);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec);

        return {static_cast<int>(res.result_int()), res.body()};
    } catch (const std::exception& e) {
        spdlog::debug("HTTP {} {} failed: {}",
                      verb == http::verb::get ? "GET" : "POST", url, e.what());
        return {0, e.what()};
    }
}

} // namespace

HttpResult https_get(const std::string& url) {
    return do_request(http::verb::get, url, nullptr, nullptr);
}

HttpResult https_post_json(const std::string& url, const std::string& json_body) {
    return do_request(http::verb::post, url, &json_body, nullptr);
}

HttpResult https_get_with_headers(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    return do_request(http::verb::get, url, nullptr, &extra_headers);
}

} // namespace trader
