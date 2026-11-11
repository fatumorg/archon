#include "core/crypto/aes.h"
#include "core/crypto/hash.h"
#include "core/crypto/inflate.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestCryptoPrimitives : public QObject {
    Q_OBJECT
private slots:
    void jenkinsKnownValues();
    void sha1KnownValue();
    void sha256KnownValue();
    void aesKnownVector();
    void inflateRoundTrip();
    void inflateRejectsBomb();
    void inflateRejectsGarbage();
    void inflateRejectsUnreachableSize();
};

void TestCryptoPrimitives::jenkinsKnownValues()
{
    QCOMPARE(jenkinsHash(asBytes("a")), 0xCA2E9442u);
    QCOMPARE(jenkinsHash(asBytes("The quick brown fox jumps over the lazy dog")), 0x519E91F5u);
    QCOMPARE(jenkinsHash({}), 0u);
}

void TestCryptoPrimitives::sha1KnownValue()
{
    QCOMPARE(toHex(sha1(asBytes("abc"))), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
}

void TestCryptoPrimitives::sha256KnownValue()
{
    QCOMPARE(toHex(sha256(asBytes("abc"))), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void TestCryptoPrimitives::aesKnownVector()
{
    const auto keyBytes = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const std::span<const std::byte, 32> key(keyBytes.data(), 32);
    const auto plain = aesEcbDecrypt(fromHex("8ea2b7ca516745bfeafc49904b496089"), key);
    QVERIFY(plain);
    QCOMPARE(toHex(*plain), std::string("00112233445566778899aabbccddeeff"));
    const auto withTail = aesEcbDecrypt(fromHex("8ea2b7ca516745bfeafc49904b496089a1b2c3"), key);
    QVERIFY(withTail);
    QCOMPARE(toHex(*withTail), std::string("00112233445566778899aabbccddeeffa1b2c3"));
    const auto onlyTail = aesEcbDecrypt(fromHex("a1b2c3"), key);
    QVERIFY(onlyTail);
    QCOMPARE(toHex(*onlyTail), std::string("a1b2c3"));
}

void TestCryptoPrimitives::inflateRoundTrip()
{
    const std::vector<std::byte> original(10000, std::byte { 'x' });
    const auto packed = deflateRaw(original);
    QVERIFY(packed.size() < 200);
    const auto unpacked = inflateRaw(packed, 10000);
    QVERIFY(unpacked);
    QCOMPARE(*unpacked, original);
}

void TestCryptoPrimitives::inflateRejectsBomb()
{
    const auto packed = deflateRaw(std::vector<std::byte>(10000, std::byte { 'x' }));
    QVERIFY(!inflateRaw(packed, 9999));
    QVERIFY(!inflateRaw(packed, 10001));
    const auto tooLarge = inflateRaw(packed, maxAllocation + 1);
    QVERIFY(!tooLarge);
    QCOMPARE(tooLarge.error().message, std::string("declared size exceeds the allocation limit"));
}

void TestCryptoPrimitives::inflateRejectsGarbage()
{
    QVERIFY(!inflateRaw(fromHex("ffffffffffffffff"), 16));
    QVERIFY(!inflateRaw({}, 16));
}

void TestCryptoPrimitives::inflateRejectsUnreachableSize()
{
    const auto packed = deflateRaw(std::vector<std::byte>(10000, std::byte { 'x' }));
    const auto unreachable = inflateRaw(packed, 100'000'000);
    QVERIFY(!unreachable);
    QCOMPARE(unreachable.error().message, std::string("declared size cannot be reached by this data"));
    const auto empty = inflateRaw({}, maxAllocation);
    QVERIFY(!empty);
    QCOMPARE(empty.error().message, std::string("declared size cannot be reached by this data"));
}

QTEST_APPLESS_MAIN(TestCryptoPrimitives)
#include "tst_crypto_primitives.moc"
