#include "core/rsc/rsc7.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestRsc7 : public QObject {
    Q_OBJECT
private slots:
    void rsc7HeaderRoundTrip();
    void rsc7SizeFromFlagsKnown();
    void rsc7VersionFromFlagsKnown();
};

void TestRsc7::rsc7HeaderRoundTrip()
{
    const auto bytes = buildRsc7Header({ 165, 0x10, 0x20 });
    QCOMPARE(toHex(bytes), std::string("52534337a50000001000000020000000"));
    const auto parsed = parseRsc7Header(bytes);
    QVERIFY(parsed);
    QCOMPARE(parsed->version, 165u);
    QCOMPARE(parsed->systemFlags, 0x10u);
    QCOMPARE(parsed->graphicsFlags, 0x20u);
    auto wrong = bytes;
    wrong[3] = std::byte { '8' };
    const auto rejected = parseRsc7Header(wrong);
    QVERIFY(!rejected);
    QCOMPARE(rejected.error().format, std::string("rsc7"));
    QVERIFY(!parseRsc7Header(std::span(bytes).first(15)));
}

void TestRsc7::rsc7SizeFromFlagsKnown()
{
    QCOMPARE(rsc7SizeFromFlags(0), std::uint64_t(0));
    QCOMPARE(rsc7SizeFromFlags(0x08000000), std::uint64_t(0x200));
    QCOMPARE(rsc7SizeFromFlags(0x08000001), std::uint64_t(0x400));
    QCOMPARE(rsc7SizeFromFlags(0x00020000), std::uint64_t(0x2000));
}

void TestRsc7::rsc7VersionFromFlagsKnown()
{
    QCOMPARE(rsc7VersionFromFlags(0xA0000000, 0x50000000), 165u);
    QCOMPARE(rsc7VersionFromFlags(0, 0), 0u);
}

QTEST_APPLESS_MAIN(TestRsc7)
#include "tst_rsc7.moc"
