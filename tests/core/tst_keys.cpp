#include "core/crypto/dotnet_random.h"
#include "core/crypto/hash.h"
#include "core/crypto/key_digests.h"
#include "core/crypto/keys.h"
#include "support/real_install.h"
#include "support/test_bytes.h"

#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <fstream>
#include <memory>
#include <random>

using namespace archon;
using namespace archon::test;

namespace {

std::array<std::byte, 64> draw(std::int32_t seed)
{
    std::array<std::byte, 64> out {};
    DotNetRandom random(seed);
    random.nextBytes(out);
    return out;
}

std::vector<std::byte> readWholeFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return toVector(content);
}

}

class TestKeys : public QObject {
    Q_OBJECT
private slots:
    void dotNetRandomIsDeterministic();
    void findAesKeyInSyntheticExecutable();
    void unwrapRejectsGarbage();
    void loadKeysWithoutFilesFails();
    void derivesKeysFromRealGame();
    void wrongMagicLeavesNoCache();
};

void TestKeys::dotNetRandomIsDeterministic()
{
    QCOMPARE(draw(12345), draw(12345));
    QVERIFY(draw(1) != draw(2));
    QCOMPARE(draw(INT32_MIN), draw(INT32_MAX));
    QVERIFY(draw(12345) != (std::array<std::byte, 64> {}));
}

void TestKeys::findAesKeyInSyntheticExecutable()
{
    std::vector<std::byte> executable(4096);
    std::array<std::byte, 32> secret {};
    for (std::size_t i = 0; i < secret.size(); ++i)
        secret[i] = static_cast<std::byte>(i * 7 + 1);
    const Digest digest = sha1(secret);
    const auto missing = findAesKey(executable, digest);
    QVERIFY(!missing);
    QCOMPARE(missing.error().message, std::string("AES key not found in executable"));
    std::ranges::copy(secret, executable.begin() + 1024);
    const auto found = findAesKey(executable, digest);
    QVERIFY(found);
    QCOMPARE(*found, secret);
    QVERIFY(!findAesKey(executable));
    QVERIFY(!findAesKey(std::span<const std::byte>(executable).first(16), digest));
}

void TestKeys::unwrapRejectsGarbage()
{
    std::array<std::byte, 32> key {};
    std::vector<std::byte> garbage(1000);
    std::mt19937 generator(1);
    for (std::byte &value : garbage)
        value = static_cast<std::byte>(generator() & 0xFF);
    QVERIFY(!unwrapKeys(key, garbage));
    const auto empty = unwrapKeys(key, {});
    QVERIFY(!empty);
}

void TestKeys::loadKeysWithoutFilesFails()
{
    QTemporaryDir directory;
    const std::filesystem::path root = directory.path().toStdString();
    QVERIFY(!loadKeys(root / "missing.exe", root / "missing.dat", root / "keys.bin"));
    QVERIFY(!std::filesystem::exists(root / "keys.bin"));
}

void TestKeys::derivesKeysFromRealGame()
{
    ARCHON_REQUIRE_REAL();
    QTemporaryDir directory;
    const std::filesystem::path cache = std::filesystem::path(directory.path().toStdString()) / "keys.bin";
    const auto loaded = loadKeys(*realGameDir() / "GTA5.exe", *realMagicDat(), cache);
    QVERIFY2(loaded, loaded ? "" : loaded.error().message.c_str());
    const auto keys = std::make_unique<Keys>(*loaded);
    QCOMPARE(sha1(keys->aes), aesKeyDigest);
    QVERIFY(toHex(sha1(std::as_bytes(std::span(keys->ngKeys[0])))).starts_with("eb0915120397"));
    QVERIFY(toHex(sha1(std::as_bytes(std::span(keys->ngTables[0][0])))).starts_with("ceab9716bdec"));
    QCOMPARE(sha1(std::as_bytes(std::span(keys->hashLookup))), hashLookupDigest);
    QVERIFY(std::filesystem::exists(cache));
    QCOMPARE(std::filesystem::status(cache).permissions(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    const auto cached = loadKeys(cache.parent_path() / "missing.exe", cache.parent_path() / "missing.dat", cache);
    QVERIFY(cached);
    QCOMPARE(cached->ngKeys, keys->ngKeys);
    QCOMPARE(cached->aes, keys->aes);
}

void TestKeys::wrongMagicLeavesNoCache()
{
    ARCHON_REQUIRE_REAL();
    QTemporaryDir directory;
    const std::filesystem::path root = directory.path().toStdString();
    auto magic = readWholeFile(*realMagicDat());
    QVERIFY(!magic.empty());
    magic[0] ^= std::byte { 0xFF };
    std::ofstream(root / "magic.dat", std::ios::binary).write(reinterpret_cast<const char *>(magic.data()), static_cast<std::streamsize>(magic.size()));
    const auto loaded = loadKeys(*realGameDir() / "GTA5.exe", root / "magic.dat", root / "keys.bin");
    QVERIFY(!loaded);
    QCOMPARE(loaded.error().message, std::string("key material does not match expected digests"));
    QVERIFY(!std::filesystem::exists(root / "keys.bin"));
}

QTEST_APPLESS_MAIN(TestKeys)
#include "tst_keys.moc"
