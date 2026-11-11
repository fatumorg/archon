#include "core/io/byte_reader.h"

#include <QTest>

#include <array>
#include <cstdint>
#include <string_view>

using archon::ByteReader;

namespace {

std::span<const std::byte> asBytes(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

constexpr std::string_view eightBytes("\x01\x02\x03\x04\x05\x06\x07\x08", 8);

}

class TestByteReader : public QObject {
    Q_OBJECT
private slots:
    void readsLittleEndian();
    void readsSixtyFourBits();
    void seekPastEndFails();
    void bytesPastEndFails();
    void cStringStopsAtNul();
};

void TestByteReader::readsLittleEndian()
{
    ByteReader reader(asBytes(eightBytes), "test");
    QCOMPARE(reader.size(), std::size_t(8));
    QCOMPARE(reader.u16().value(), std::uint16_t(0x0201));
    QCOMPARE(reader.u24().value(), std::uint32_t(0x050403));
    QCOMPARE(reader.u8().value(), std::uint8_t(0x06));
    const auto failed = reader.u32();
    QVERIFY(!failed);
    QCOMPARE(failed.error().offset, std::uint64_t(6));
    QCOMPARE(failed.error().format, std::string("test"));
    QVERIFY(!failed.error().message.empty());
    QCOMPARE(reader.position(), std::size_t(6));
}

void TestByteReader::readsSixtyFourBits()
{
    ByteReader reader(asBytes(eightBytes), "test");
    QCOMPARE(reader.u64().value(), std::uint64_t(0x0807060504030201));
    QCOMPARE(reader.position(), std::size_t(8));
    QVERIFY(!reader.u8());
}

void TestByteReader::seekPastEndFails()
{
    ByteReader reader(asBytes(eightBytes), "test");
    QVERIFY(!reader.seek(9));
    QCOMPARE(reader.position(), std::size_t(0));
    QVERIFY(reader.seek(8));
    QCOMPARE(reader.position(), std::size_t(8));
}

void TestByteReader::bytesPastEndFails()
{
    ByteReader reader(asBytes(eightBytes), "test");
    QVERIFY(!reader.bytes(9));
    QVERIFY(reader.seek(4));
    QVERIFY(!reader.bytes(SIZE_MAX));
    QCOMPARE(reader.position(), std::size_t(4));
    const auto rest = reader.bytes(4);
    QVERIFY(rest);
    QCOMPARE(rest->size(), std::size_t(4));
    QCOMPARE(std::to_integer<int>((*rest)[0]), 5);
    QCOMPARE(reader.position(), std::size_t(8));
}

void TestByteReader::cStringStopsAtNul()
{
    const ByteReader reader(asBytes(std::string_view("ab\0cd", 5)), "test");
    QCOMPARE(reader.cString(0).value(), std::string("ab"));
    QVERIFY(!reader.cString(3));
    QVERIFY(!reader.cString(9));
    QVERIFY(!reader.cString(0, 2));
    QCOMPARE(reader.cString(0, 3).value(), std::string("ab"));
}

QTEST_APPLESS_MAIN(TestByteReader)
#include "tst_byte_reader.moc"
