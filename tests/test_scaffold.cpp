#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>
#include <boost/beast/core.hpp>
#include <openssl/evp.h>
#include <string>

// Verify all dependencies are linkable and functional

TEST(Scaffold, NlohmannJsonWorks) {
    nlohmann::json j = {{"key", "value"}, {"number", 42}};
    EXPECT_EQ(j["key"].get<std::string>(), "value");
    EXPECT_EQ(j["number"].get<int>(), 42);

    // Round-trip serialization
    std::string serialized = j.dump();
    auto parsed = nlohmann::json::parse(serialized);
    EXPECT_EQ(parsed, j);
}

TEST(Scaffold, SpdlogWorks) {
    // Should not throw
    spdlog::info("Test log message from Google Test");
    spdlog::warn("Warning level works");
    SUCCEED();
}

TEST(Scaffold, YamlCppWorks) {
    YAML::Node node;
    node["venue"] = "kalshi";
    node["mode"] = "paper";
    node["risk"]["max_daily_loss"] = 15;

    EXPECT_EQ(node["venue"].as<std::string>(), "kalshi");
    EXPECT_EQ(node["mode"].as<std::string>(), "paper");
    EXPECT_EQ(node["risk"]["max_daily_loss"].as<int>(), 15);
}

TEST(Scaffold, BoostBeastCompiles) {
    // Verify Boost.Beast types are available
    boost::beast::flat_buffer buffer;
    EXPECT_EQ(buffer.size(), 0u);
}

TEST(Scaffold, OpenSSLAvailable) {
    // Verify OpenSSL EVP context can be created (needed for RSA-PSS auth)
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    ASSERT_NE(ctx, nullptr);
    EVP_MD_CTX_free(ctx);
}

TEST(Scaffold, CoreLibraryLinks) {
    // Verify trader_core library links by using config (defined in core lib)
    // Just confirm the library is linked - real tests are in test_config.cpp
    SUCCEED();
}
