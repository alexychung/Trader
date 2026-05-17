#include "core/alert_manager.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <regex>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace trader {

static bool parse_url(const std::string& url, std::string& host, std::string& port,
                       std::string& path, bool& is_https) {
    std::regex re(R"((https?)://([^/:]+)(?::(\d+))?(/.*)?)");
    std::smatch match;
    if (!std::regex_match(url, match, re)) return false;
    is_https = (match[1].str() == "https");
    host = match[2].str();
    port = match[3].matched ? match[3].str() : (is_https ? "443" : "80");
    path = match[4].matched ? match[4].str() : "/";
    return true;
}

AlertManager::AlertManager(Config config) : config_(std::move(config)) {
    if (enabled()) {
        worker_ = std::thread(&AlertManager::worker_loop, this);
        spdlog::info("AlertManager: enabled (format: {})",
                     config_.format == Format::Discord ? "Discord"
                     : config_.format == Format::Slack ? "Slack" : "Generic");
    }
}

AlertManager::~AlertManager() {
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }
}

std::string AlertManager::build_payload(const std::string& title, const std::string& message) {
    json j;
    std::string body = "**" + title + "**\n" + message;
    switch (config_.format) {
        case Format::Discord: j["content"] = body; break;
        case Format::Slack:   j["text"] = body; break;
        case Format::Generic: j["title"] = title; j["message"] = message; break;
    }
    return j.dump();
}

void AlertManager::send(const std::string& title, const std::string& message) {
    if (!enabled()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace(title, message);
    }
    cv_.notify_one();
}

void AlertManager::on_kill_switch(const std::string& reason, double balance, double daily_pnl) {
    if (!enabled() || !config_.send_kill_switch) return;
    std::ostringstream oss;
    oss << "Reason: " << reason << "\n"
        << "Balance: $" << balance << "\n"
        << "Daily PnL: $" << daily_pnl << "\n"
        << "All open orders cancelled. Manual reset required.";
    send("KILL SWITCH FIRED", oss.str());
}

void AlertManager::on_balance_change(double old_balance, double new_balance) {
    if (!enabled() || !config_.send_balance_changes) return;
    if (old_balance <= 0) return;
    double pct = std::abs(new_balance - old_balance) / old_balance;
    if (pct < config_.balance_change_pct) return;
    auto now = std::chrono::system_clock::now();
    if (now - last_balance_alert_ < config_.throttle) return;
    last_balance_alert_ = now;
    last_alerted_balance_ = new_balance;
    std::ostringstream oss;
    oss << "Old: $" << old_balance << "\n"
        << "New: $" << new_balance << "\n"
        << "Change: " << (new_balance > old_balance ? "+" : "") << (pct * 100.0) << "%";
    send(new_balance > old_balance ? "Balance up" : "Balance down", oss.str());
}

void AlertManager::daily_summary(double balance, double daily_pnl, int trades, double brier) {
    if (!enabled() || !config_.send_daily_summary) return;
    std::ostringstream oss;
    oss << "Balance: $" << balance << "\n"
        << "Daily PnL: $" << daily_pnl << "\n"
        << "Trades: " << trades << "\n"
        << "Brier (real): " << brier;
    send("Daily summary", oss.str());
}

void AlertManager::crash(const std::string& what) {
    if (!enabled()) return;
    send("CRASH", what);
}

void AlertManager::worker_loop() {
    while (true) {
        std::pair<std::string, std::string> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            item = std::move(queue_.front());
            queue_.pop();
        }
        std::string payload = build_payload(item.first, item.second);
        const bool ok = post_handler_ ? post_handler_(payload)
                                       : post_payload(payload);
        if (!ok) {
            spdlog::warn("AlertManager: webhook POST failed (title='{}')", item.first);
        }
    }
}

bool AlertManager::post_payload(const std::string& payload) {
    std::string host, port, path;
    bool is_https = true;
    if (!parse_url(config_.webhook_url, host, port, path, is_https)) {
        spdlog::warn("AlertManager: invalid webhook URL '{}'", config_.webhook_url);
        return false;
    }

    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, port);

        if (is_https) {
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) return false;
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req(http::verb::post, path, 11);
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "Trader/0.2.0");
            req.set(http::field::content_type, "application/json");
            req.body() = payload;
            req.prepare_payload();
            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);  // ignore truncation
            return res.result_int() >= 200 && res.result_int() < 300;
        } else {
            beast::tcp_stream stream(ioc);
            stream.connect(results);
            http::request<http::string_body> req(http::verb::post, path, 11);
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "Trader/0.2.0");
            req.set(http::field::content_type, "application/json");
            req.body() = payload;
            req.prepare_payload();
            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);
            stream.socket().shutdown(tcp::socket::shutdown_both);
            return res.result_int() >= 200 && res.result_int() < 300;
        }
    } catch (const std::exception& e) {
        spdlog::warn("AlertManager: post exception: {}", e.what());
        return false;
    }
}

} // namespace trader
