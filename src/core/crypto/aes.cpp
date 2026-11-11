#include "core/crypto/aes.h"

#include <openssl/evp.h>

#include <climits>
#include <memory>

namespace archon {

Result<std::vector<std::byte>> aesEcbDecrypt(std::span<const std::byte> data, std::span<const std::byte, 32> key)
{
    const auto failure = [](const char *message) { return std::unexpected(Error { "aes", 0, message }); };
    const std::size_t whole = data.size() - data.size() % 16;
    if (whole > INT_MAX)
        return failure("data too large to decrypt");
    std::vector<std::byte> result(data.begin(), data.end());
    if (whole == 0)
        return result;
    const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!context)
        return failure("cipher context allocation failed");
    if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_ecb(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), nullptr) != 1)
        return failure("cipher initialisation failed");
    EVP_CIPHER_CTX_set_padding(context.get(), 0);
    int written = 0;
    if (EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(result.data()), &written, reinterpret_cast<const unsigned char *>(data.data()), static_cast<int>(whole)) != 1
        || static_cast<std::size_t>(written) != whole)
        return failure("decryption failed");
    return result;
}

}
