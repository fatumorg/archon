#include "core/io/zip_archive.h"
#include "support/test_bytes.h"

#include <QTest>

#include <zip.h>

using namespace archon;
using namespace archon::test;

namespace {

std::vector<std::byte> makeZip(const std::vector<std::pair<std::string, std::string>> &members)
{
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t *buffer = zip_source_buffer_create(nullptr, 0, 0, &error);
    zip_t *archive = zip_open_from_source(buffer, ZIP_TRUNCATE, &error);
    zip_source_keep(buffer);
    for (const auto &[name, content] : members) {
        zip_source_t *data = zip_source_buffer(archive, content.data(), content.size(), 0);
        zip_file_add(archive, name.c_str(), data, ZIP_FL_ENC_UTF_8);
    }
    zip_close(archive);
    zip_source_open(buffer);
    zip_source_seek(buffer, 0, SEEK_END);
    const auto size = static_cast<std::size_t>(zip_source_tell(buffer));
    zip_source_seek(buffer, 0, SEEK_SET);
    std::vector<std::byte> bytes(size);
    zip_source_read(buffer, bytes.data(), size);
    zip_source_close(buffer);
    zip_source_free(buffer);
    return bytes;
}

}

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
