#include "exchange/kalshi/auth.hpp"
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace trader::kalshi {

static std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

bool KalshiAuth::load_key(const std::string& pem_path) {
    std::ifstream file(pem_path);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();
    return load_key_from_string(ss.str());
}

bool KalshiAuth::load_key_from_string(const std::string& pem_data) {
    if (pkey_) {
        EVP_PKEY_free(pkey_);
        pkey_ = nullptr;
    }

    BIO* bio = BIO_new_mem_buf(pem_data.data(), static_cast<int>(pem_data.size()));
    if (!bio) return false;

    pkey_ = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey_ != nullptr;
}

int64_t KalshiAuth::timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

std::string KalshiAuth::sign(const std::string& message) const {
    if (!pkey_) return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    EVP_PKEY_CTX* pkey_ctx = nullptr;

    if (EVP_DigestSignInit(ctx, &pkey_ctx, EVP_sha256(), nullptr, pkey_) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    // Set RSA-PSS padding with salt length = digest length
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, EVP_MD_CTX_get_size(ctx)) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    if (EVP_DigestSignUpdate(ctx, message.data(), message.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    size_t sig_len = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    std::vector<unsigned char> sig(sig_len);
    if (EVP_DigestSignFinal(ctx, sig.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);
    return base64_encode(sig.data(), sig_len);
}

KalshiAuth::AuthHeaders KalshiAuth::make_headers(const std::string& method, const std::string& path) const {
    return make_headers(method, path, timestamp_ms());
}

KalshiAuth::AuthHeaders KalshiAuth::make_headers(const std::string& method, const std::string& path, int64_t ts_ms) const {
    std::string ts_str = std::to_string(ts_ms);
    std::string message = ts_str + method + path;
    std::string signature = sign(message);

    return AuthHeaders{
        .key = api_key_id_,
        .timestamp = ts_str,
        .signature = signature
    };
}

} // namespace trader::kalshi
