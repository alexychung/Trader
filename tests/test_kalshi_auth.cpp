#include <gtest/gtest.h>
#include "exchange/kalshi/auth.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <fstream>
#include <filesystem>
#include <cstring>

using namespace trader::kalshi;

// Generate a test RSA key pair in PEM format
static std::string generate_test_rsa_key() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(bio, &bptr);
    std::string pem(bptr->data, bptr->length);

    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return pem;
}

class KalshiAuthTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_key_pem_ = generate_test_rsa_key();
    }

    std::string test_key_pem_;
};

TEST_F(KalshiAuthTest, LoadKeyFromString) {
    KalshiAuth auth;
    EXPECT_FALSE(auth.is_loaded());
    EXPECT_TRUE(auth.load_key_from_string(test_key_pem_));
    EXPECT_TRUE(auth.is_loaded());
}

TEST_F(KalshiAuthTest, LoadKeyFromFile) {
    // Write key to temp file
    std::ofstream out("test_key_temp.pem");
    out << test_key_pem_;
    out.close();

    KalshiAuth auth;
    EXPECT_TRUE(auth.load_key("test_key_temp.pem"));
    EXPECT_TRUE(auth.is_loaded());

    std::filesystem::remove("test_key_temp.pem");
}

TEST_F(KalshiAuthTest, LoadKeyFailsOnInvalidData) {
    KalshiAuth auth;
    EXPECT_FALSE(auth.load_key_from_string("not a real key"));
    EXPECT_FALSE(auth.is_loaded());
}

TEST_F(KalshiAuthTest, LoadKeyFailsOnMissingFile) {
    KalshiAuth auth;
    EXPECT_FALSE(auth.load_key("nonexistent_file.pem"));
    EXPECT_FALSE(auth.is_loaded());
}

TEST_F(KalshiAuthTest, SignProducesNonEmptyBase64) {
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));

    std::string signature = auth.sign("1711234567890GET/trade-api/v2/markets");
    EXPECT_FALSE(signature.empty());

    // Base64 should only contain valid characters
    for (char c : signature) {
        EXPECT_TRUE(std::isalnum(c) || c == '+' || c == '/' || c == '=')
            << "Invalid base64 char: " << c;
    }
}

TEST_F(KalshiAuthTest, SignReturnsEmptyWhenNoKey) {
    KalshiAuth auth;
    // No key loaded
    std::string signature = auth.sign("test message");
    EXPECT_TRUE(signature.empty());
}

TEST_F(KalshiAuthTest, DifferentMessagesDifferentSignatures) {
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));

    std::string sig1 = auth.sign("message1");
    std::string sig2 = auth.sign("message2");

    EXPECT_NE(sig1, sig2);
}

TEST_F(KalshiAuthTest, SameMessageProducesDifferentSignaturesDueToPSSSalt) {
    // RSA-PSS uses random salt, so same input -> different output each time
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));

    std::string sig1 = auth.sign("same message");
    std::string sig2 = auth.sign("same message");

    // PSS signatures should differ (probabilistic scheme)
    // Note: there's a tiny theoretical chance they match, but practically impossible
    EXPECT_NE(sig1, sig2);
}

TEST_F(KalshiAuthTest, TimestampIsMilliseconds) {
    int64_t ts = KalshiAuth::timestamp_ms();
    // Should be ~13 digits (milliseconds since epoch in 2026)
    EXPECT_GT(ts, 1700000000000LL);  // After 2023
    EXPECT_LT(ts, 2000000000000LL);  // Before 2033
}

TEST_F(KalshiAuthTest, MakeHeadersProducesAllThreeFields) {
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));
    auth.set_api_key_id("test-key-abc123");

    auto headers = auth.make_headers("GET", "/trade-api/v2/markets");

    EXPECT_EQ(headers.key, "test-key-abc123");
    EXPECT_FALSE(headers.timestamp.empty());
    EXPECT_FALSE(headers.signature.empty());

    // Timestamp should be a valid millisecond timestamp
    int64_t ts = std::stoll(headers.timestamp);
    EXPECT_GT(ts, 1700000000000LL);
}

TEST_F(KalshiAuthTest, MakeHeadersWithExplicitTimestamp) {
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));
    auth.set_api_key_id("my-key");

    int64_t fixed_ts = 1711234567890LL;
    auto headers = auth.make_headers("GET", "/trade-api/v2/markets", fixed_ts);

    EXPECT_EQ(headers.key, "my-key");
    EXPECT_EQ(headers.timestamp, "1711234567890");
    EXPECT_FALSE(headers.signature.empty());

    // Same timestamp + method + path should produce a signature
    // (different each time due to PSS salt, but always non-empty)
    auto headers2 = auth.make_headers("GET", "/trade-api/v2/markets", fixed_ts);
    EXPECT_EQ(headers2.timestamp, "1711234567890");
    EXPECT_FALSE(headers2.signature.empty());
}
