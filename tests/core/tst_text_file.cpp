#include "core/meta/text_file.h"
#include "support/test_bytes.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestTextFile : public QObject {
    Q_OBJECT
private slots:
    void decodesUtf8AndKeepsLayout();
    void latin1RoundTripsByteExact();
    void binaryIsRefused();
    void lfStaysLf();
};

namespace {

std::vector<std::byte> bytesOf(const std::string &text)
{
    return toVector(text);
}

}

void TestTextFile::decodesUtf8AndKeepsLayout()
{
    const std::string original = "\xEF\xBB\xBF<CHandlingDataMgr>\r\n\t<x>caf\xC3\xA9</x>\r\n</CHandlingDataMgr>\r\n";
    const auto file = decodeTextFile(bytesOf(original), "handling.meta");
    QVERIFY2(file, file ? "" : file.error().message.c_str());
    QVERIFY(file->bom);
    QVERIFY(file->crlf);
    QVERIFY(!file->latin1);
    QCOMPARE(file->text, QStringLiteral("<CHandlingDataMgr>\n\t<x>café</x>\n</CHandlingDataMgr>\n"));
    QCOMPARE(toText(encodeTextFile(*file, file->text)), original);
    const auto edited = encodeTextFile(*file, QStringLiteral("<a/>\n<b/>"));
    QCOMPARE(toText(edited), std::string("\xEF\xBB\xBF<a/>\r\n<b/>"));
}

void TestTextFile::latin1RoundTripsByteExact()
{
    const std::string original = "name=caf\xE9\nnext=1\n";
    const auto file = decodeTextFile(bytesOf(original), "x.dat");
    QVERIFY(file);
    QVERIFY(file->latin1);
    QCOMPARE(file->text, QStringLiteral("name=café\nnext=1\n"));
    QCOMPARE(toText(encodeTextFile(*file, file->text)), original);
}

void TestTextFile::binaryIsRefused()
{
    const auto refused = decodeTextFile(bytesOf(std::string("PSIN\0\0\x01", 7)), "x.meta");
    QVERIFY(!refused);
    QCOMPARE(refused.error().message, std::string("x.meta is not a text file"));
    QVERIFY(!decodeTextFile(std::vector<std::byte>((64u << 20) + 1, std::byte { 'a' }), "huge.xml"));
}

void TestTextFile::lfStaysLf()
{
    const auto file = decodeTextFile(bytesOf("a\nb\n"), "a.txt");
    QVERIFY(file);
    QVERIFY(!file->crlf);
    QVERIFY(!file->bom);
    QCOMPARE(toText(encodeTextFile(*file, QStringLiteral("x\ny"))), std::string("x\ny"));
}

QTEST_APPLESS_MAIN(TestTextFile)
#include "tst_text_file.moc"
