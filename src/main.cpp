#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <boost/beast/core.hpp>
#include <openssl/ssl.h>
#include <iostream>

int main() {
    spdlog::info("Trader v0.1.0 starting");
    spdlog::info("All dependencies linked successfully");

    // Verify nlohmann/json
    nlohmann::json j = {{"status", "ok"}, {"version", "0.1.0"}};
    spdlog::info("JSON: {}", j.dump());

    // Verify yaml-cpp
    YAML::Node node;
    node["venue"] = "kalshi";
    spdlog::info("YAML venue: {}", node["venue"].as<std::string>());

    // Verify OpenSSL
    spdlog::info("OpenSSL version: {}", OpenSSL_version(OPENSSL_VERSION));

    // Verify Boost.Beast (just include check - already compiled above)
    spdlog::info("Boost.Beast available");

    spdlog::info("All systems operational");
    return 0;
}
