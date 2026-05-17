#include <gtest/gtest.h>
#include "exchange/kalshi/auth.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <vector>

using namespace trader::kalshi;

// Generate a test RSA key pair. Returns (private_pem, public_pem).
static std::pair<std::string, std::string> generate_test_rsa_keypair() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    BIO* priv_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(priv_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM* priv_ptr;
    BIO_get_mem_ptr(priv_bio, &priv_ptr);
    std::string priv_pem(priv_ptr->data, priv_ptr->length);
    BIO_free(priv_bio);

    BIO* pub_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pub_bio, pkey);
    BUF_MEM* pub_ptr;
    BIO_get_mem_ptr(pub_bio, &pub_ptr);
    std::string pub_pem(pub_ptr->data, pub_ptr->length);
    BIO_free(pub_bio);

    EVP_PKEY_free(pkey);
    return {priv_pem, pub_pem};
}

static std::string generate_test_rsa_key() {
    return generate_test_rsa_keypair().first;
}

// Decode standard base64 (non-URL) into raw bytes.
static std::vector<unsigned char> base64_decode(const std::string& b64) {
    BIO* mem = BIO_new_mem_buf(b64.data(), static_cast<int>(b64.size()));
    BIO* b = BIO_new(BIO_f_base64());
    BIO_set_flags(b, BIO_FLAGS_BASE64_NO_NL);
    b = BIO_push(b, mem);
    std::vector<unsigned char> out(b64.size());
    int n = BIO_read(b, out.data(), static_cast<int>(out.size()));
    BIO_free_all(b);
    if (n <= 0) return {};
    out.resize(n);
    return out;
}

// Verify an RSA-PSS/SHA256 signature with salt = digest length.
static bool verify_pss(const std::string& public_pem,
                       const std::string& message,
                       const std::string& b64_sig) {
    BIO* bio = BIO_new_mem_buf(public_pem.data(), static_cast<int>(public_pem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    auto sig = base64_decode(b64_sig);
    if (sig.empty()) { EVP_PKEY_free(pkey); return false; }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    bool ok = EVP_DigestVerifyInit(ctx, &pctx, EVP_sha256(), nullptr, pkey) == 1
              && EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) == 1
              && EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, EVP_MD_CTX_get_size(ctx)) == 1
              && EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) == 1
              && EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1;

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
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

TEST_F(KalshiAuthTest, SignatureUsesPss32ByteSaltAndStandardBase64) {
    // Concrete verification that our signatures decode as standard base64 (not
    // URL-safe) and validate under RSA-PSS/SHA-256 with salt length = digest
    // length = 32 bytes. The verifier in verify_pss enforces exactly these
    // parameters — if the signer ever drifts (PKCS1 padding, different salt,
    // URL-safe b64), this test fails.
    auto [priv, pub] = generate_test_rsa_keypair();
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(priv));
    auth.set_api_key_id("k");

    const int64_t ts = 1744200000123LL;
    auto h = auth.make_headers("POST", "/trade-api/v2/portfolio/orders", ts);
    const std::string expected_message = std::to_string(ts) + "POST/trade-api/v2/portfolio/orders";

    // Standard base64 alphabet only — no '-' or '_'.
    for (char c : h.signature) {
        EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || c == '=')
            << "URL-safe base64 char '" << c << "' leaked into signature";
    }
    // And the signature actually verifies under PSS/SHA-256 with 32-byte salt.
    EXPECT_TRUE(verify_pss(pub, expected_message, h.signature));
}

TEST_F(KalshiAuthTest, TimestampHeaderIsMillisecondsString) {
    // KALSHI-ACCESS-TIMESTAMP must be milliseconds-since-epoch as a string.
    // Seconds (10-digit) would be silently accepted by some servers as "old"
    // and rejected for skew. Enforce 13 digits and numeric round-trip.
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(test_key_pem_));
    auth.set_api_key_id("k");

    auto h = auth.make_headers("GET", "/markets");
    EXPECT_GE(h.timestamp.size(), 13u);
    EXPECT_LE(h.timestamp.size(), 14u);  // allows future-proofing to 14 digits
    int64_t parsed = std::stoll(h.timestamp);
    EXPECT_GT(parsed, 1700000000000LL);
    EXPECT_LT(parsed, 2000000000000LL);
}

TEST_F(KalshiAuthTest, SignatureExcludesQueryString) {
    // Kalshi requires signing the path WITHOUT the query string.
    // Both /markets and /markets?limit=100 at the same timestamp must produce
    // signatures that verify against the SAME message (ts+method+path_only).
    auto [priv, pub] = generate_test_rsa_keypair();
    KalshiAuth auth;
    ASSERT_TRUE(auth.load_key_from_string(priv));
    auth.set_api_key_id("k");

    const int64_t ts = 1711234567890LL;
    auto h_no_q    = auth.make_headers("GET", "/trade-api/v2/markets", ts);
    auto h_with_q  = auth.make_headers("GET", "/trade-api/v2/markets?limit=100&cursor=abc", ts);

    const std::string expected_message = std::to_string(ts) + "GET/trade-api/v2/markets";

    EXPECT_TRUE(verify_pss(pub, expected_message, h_no_q.signature))
        << "plain path must verify against ts+method+path";
    EXPECT_TRUE(verify_pss(pub, expected_message, h_with_q.signature))
        << "query string must be stripped before signing";
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
