#include "core/crypto/keys.h"

#include "core/crypto/aes.h"
#include "core/crypto/dotnet_random.h"
#include "core/crypto/hash.h"
#include "core/crypto/inflate.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <memory>
#include <vector>

namespace archon {

namespace {

constexpr std::size_t ngKeySize = 272;
constexpr std::size_t ngKeyCount = 101;
constexpr std::size_t ngTableSize = 1024;
constexpr std::size_t ngTableCount = 17 * 16;
constexpr std::size_t lookupSize = 256;
constexpr std::size_t materialSize = ngKeyCount * ngKeySize + ngTableCount * ngTableSize + lookupSize;
constexpr std::size_t inflatedMagicLimit = 400000;
constexpr std::size_t magicFileLimit = 16u << 20;

std::unexpected<Error> keyError(std::string message)
{
    return std::unexpected(Error { "keys", 0, std::move(message) });
}

void readWords(std::span<const std::byte> source, std::span<std::uint32_t> target)
{
    for (std::size_t i = 0; i < target.size(); ++i) {
        target[i] = std::to_integer<std::uint32_t>(source[4 * i])
            | std::to_integer<std::uint32_t>(source[4 * i + 1]) << 8
            | std::to_integer<std::uint32_t>(source[4 * i + 2]) << 16
            | std::to_integer<std::uint32_t>(source[4 * i + 3]) << 24;
    }
}

Result<void> parseMaterial(std::span<const std::byte, 32> aesKey, std::span<const std::byte> material, Keys &keys)
{
    const auto mismatch = keyError("key material does not match expected digests");
    if (material.size() < materialSize || sha1(aesKey) != aesKeyDigest)
        return mismatch;
    std::ranges::copy(aesKey, keys.aes.begin());
    std::size_t offset = 0;
    for (std::size_t i = 0; i < ngKeyCount; ++i, offset += ngKeySize) {
        const auto raw = material.subspan(offset, ngKeySize);
        if (sha1(raw) != ngKeyDigests[i])
            return mismatch;
        readWords(raw, keys.ngKeys[i]);
    }
    for (std::size_t i = 0; i < ngTableCount; ++i, offset += ngTableSize) {
        const auto raw = material.subspan(offset, ngTableSize);
        if (sha1(raw) != ngTableDigests[i])
            return mismatch;
        readWords(raw, keys.ngTables[i / 16][i % 16]);
    }
    const auto lookup = material.subspan(offset, lookupSize);
    if (sha1(lookup) != hashLookupDigest)
        return mismatch;
    std::ranges::transform(lookup, keys.hashLookup.begin(), [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    return {};
}

Result<std::vector<std::byte>> unwrapMaterial(std::span<const std::byte, 32> aesKey, std::span<const std::byte> magic)
{
    if (magic.empty() || magic.size() > magicFileLimit)
        return keyError("key blob is empty or too large");
    std::vector<std::byte> masked(magic.begin(), magic.end());
    std::vector<std::byte> stream(magic.size());
    DotNetRandom random(static_cast<std::int32_t>(jenkinsHash(aesKey)));
    for (int pass = 0; pass < 4; ++pass) {
        random.nextBytes(stream);
        for (std::size_t i = 0; i < masked.size(); ++i)
            masked[i] = static_cast<std::byte>(std::to_integer<unsigned>(masked[i]) - std::to_integer<unsigned>(stream[i]));
    }
    const auto decrypted = aesEcbDecrypt(masked, aesKey);
    if (!decrypted)
        return std::unexpected(decrypted.error());
    return inflateRawUpTo(*decrypted, inflatedMagicLimit);
}

Result<std::vector<std::byte>> readFile(const std::filesystem::path &path, std::size_t limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return keyError("cannot open " + path.string());
    if (file.size() < 0 || static_cast<std::uint64_t>(file.size()) > limit)
        return keyError("file too large: " + path.string());
    const QByteArray content = file.readAll();
    if (content.size() != file.size())
        return keyError("cannot read " + path.string());
    const auto bytes = std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

Result<void> writeCache(const std::filesystem::path &cacheFile, std::span<const std::byte, 32> aesKey, std::span<const std::byte> material)
{
    std::error_code ignored;
    std::filesystem::create_directories(cacheFile.parent_path(), ignored);
    QSaveFile file(QFile::decodeName(cacheFile.c_str()));
    if (!file.open(QIODevice::WriteOnly))
        return keyError("cannot write key cache " + cacheFile.string());
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(reinterpret_cast<const char *>(aesKey.data()), static_cast<qint64>(aesKey.size()));
    file.write(reinterpret_cast<const char *>(material.data()), static_cast<qint64>(materialSize));
    if (!file.commit())
        return keyError("cannot write key cache " + cacheFile.string());
    return {};
}

}

Result<std::array<std::byte, 32>> findAesKey(std::span<const std::byte> executable)
{
    return findAesKey(executable, aesKeyDigest);
}

Result<std::array<std::byte, 32>> findAesKey(std::span<const std::byte> executable, const Digest &digest)
{
    for (std::size_t offset = 0; executable.size() >= 32 && offset <= executable.size() - 32; offset += 8) {
        const auto window = executable.subspan(offset, 32);
        if (sha1(window) == digest) {
            std::array<std::byte, 32> key {};
            std::ranges::copy(window, key.begin());
            return key;
        }
    }
    return keyError("AES key not found in executable");
}

Result<Keys> unwrapKeys(std::span<const std::byte, 32> aesKey, std::span<const std::byte> magic)
{
    const auto material = unwrapMaterial(aesKey, magic);
    if (!material)
        return std::unexpected(material.error());
    auto keys = std::make_unique<Keys>();
    if (const auto parsed = parseMaterial(aesKey, *material, *keys); !parsed)
        return std::unexpected(parsed.error());
    return *keys;
}

Result<Keys> loadKeys(const std::filesystem::path &executable, const std::filesystem::path &magic, const std::filesystem::path &cacheFile)
{
    auto keys = std::make_unique<Keys>();
    if (const auto cached = readFile(cacheFile, 32 + materialSize); cached && cached->size() == 32 + materialSize) {
        const std::span<const std::byte> content(*cached);
        if (parseMaterial(content.first<32>(), content.subspan(32), *keys))
            return *keys;
    }
    const auto executableBytes = readFile(executable, maxAllocation);
    if (!executableBytes)
        return std::unexpected(executableBytes.error());
    const auto aesKey = findAesKey(*executableBytes);
    if (!aesKey)
        return std::unexpected(aesKey.error());
    const auto magicBytes = readFile(magic, magicFileLimit);
    if (!magicBytes)
        return std::unexpected(magicBytes.error());
    const auto material = unwrapMaterial(*aesKey, *magicBytes);
    if (!material)
        return std::unexpected(material.error());
    if (const auto parsed = parseMaterial(*aesKey, *material, *keys); !parsed)
        return std::unexpected(parsed.error());
    if (const auto written = writeCache(cacheFile, *aesKey, *material); !written)
        return std::unexpected(written.error());
    return *keys;
}

}
