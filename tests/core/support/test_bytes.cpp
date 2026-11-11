#include "support/test_bytes.h"

#include <openssl/evp.h>
#include <zlib.h>

#include <stdexcept>

namespace archon::test {

std::span<const std::byte> asBytes(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::vector<std::byte> toVector(std::string_view text)
{
    const auto bytes = asBytes(text);
    return { bytes.begin(), bytes.end() };
}

std::vector<std::byte> fromHex(std::string_view hex)
{
    std::vector<std::byte> result;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    return result;
}

std::string toHex(std::span<const std::byte> data)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const std::byte value : data) {
        result.push_back(digits[std::to_integer<unsigned>(value) >> 4]);
        result.push_back(digits[std::to_integer<unsigned>(value) & 0xF]);
    }
    return result;
}

std::string toText(std::span<const std::byte> data)
{
    return { reinterpret_cast<const char *>(data.data()), data.size() };
}

std::vector<std::byte> deflateRaw(std::span<const std::byte> data)
{
    z_stream stream {};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("deflateInit2 failed");
    std::vector<std::byte> result(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = static_cast<uInt>(result.size());
    const int status = deflate(&stream, Z_FINISH);
    result.resize(stream.total_out);
    deflateEnd(&stream);
    if (status != Z_STREAM_END)
        throw std::runtime_error("deflate failed");
    return result;
}

std::vector<std::byte> aesEcbEncrypt(std::span<const std::byte> data, std::span<const std::byte, 32> key)
{
    std::vector<std::byte> result(data.begin(), data.end());
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(context, EVP_aes_256_ecb(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), nullptr);
    EVP_CIPHER_CTX_set_padding(context, 0);
    int written = 0;
    EVP_EncryptUpdate(context, reinterpret_cast<unsigned char *>(result.data()), &written, reinterpret_cast<const unsigned char *>(data.data()), static_cast<int>(data.size() - data.size() % 16));
    EVP_CIPHER_CTX_free(context);
    return result;
}

}
