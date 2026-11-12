#include "core/io/zip_archive.h"
#include "support/test_bytes.h"
#include "support/zip_builder.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestZipArchive : public QObject {
    Q_OBJECT
private slots:
    void listsAndReadsMembers();
    void missingMemberIsError();
    void memberOverLimitIsRefused();
    void garbageIsNotAZip();
};

void TestZipArchive::listsAndReadsMembers()
{
    const std::string big(100000, 'q');
    const auto zip = ZipArchive::open(makeZip({ { "dinput8.dll", "loader" }, { "folder/inner.txt", big } }));
    QVERIFY2(zip, zip ? "" : zip.error().message.c_str());
    QCOMPARE(zip->names(), (std::vector<std::string> { "dinput8.dll", "folder/inner.txt" }));
    const auto loader = zip->read("dinput8.dll", 1024);
    QVERIFY(loader);
    QCOMPARE(toText(*loader), std::string("loader"));
    const auto inner = zip->read("folder/inner.txt", 200000);
    QVERIFY(inner);
    QCOMPARE(toText(*inner), big);
}

void TestZipArchive::missingMemberIsError()
{
    const auto zip = ZipArchive::open(makeZip({ { "a.txt", "a" } }));
    QVERIFY(zip);
    QVERIFY(!zip->read("A.TXT", 100));
    QVERIFY(!zip->read("b.txt", 100));
}

void TestZipArchive::memberOverLimitIsRefused()
{
    const auto zip = ZipArchive::open(makeZip({ { "a.txt", std::string(5000, 'x') } }));
    QVERIFY(zip);
    const auto read = zip->read("a.txt", 4999);
    QVERIFY(!read);
    QVERIFY(read.error().message.find("too large") != std::string::npos);
}

void TestZipArchive::garbageIsNotAZip()
{
    QVERIFY(!ZipArchive::open(toVector("this is not a zip file at all")));
    QVERIFY(!ZipArchive::open({}));
}

QTEST_APPLESS_MAIN(TestZipArchive)
#include "tst_zip_archive.moc"
