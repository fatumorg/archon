#include "core/game/game_install.h"
#include "core/io/ascii.h"
#include "core/rpf/rpf_reader.h"
#include "support/real_install.h"
#include "support/real_keys.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

namespace {

struct Totals {
    std::uint64_t archives = 0;
    std::uint64_t entries = 0;
    std::uint64_t bytes = 0;
    std::vector<std::string> failures;
};

void walk(const RpfReader &reader, const std::string &archivePath, Totals &totals)
{
    ++totals.archives;
    const auto &entries = reader.archive().entries();
    totals.entries += entries.size();
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
        const RpfEntry &entry = entries[index];
        if (entry.kind == RpfEntryKind::Directory)
            continue;
        const std::string entryPath = reader.archive().path(index);
        if (entry.kind == RpfEntryKind::Binary && entry.storedSize == 0 && asciiLower(entry.name).ends_with(".rpf")) {
            const auto nested = reader.openNested(index);
            if (nested)
                walk(*nested, archivePath + "/" + entryPath, totals);
            else
                totals.failures.push_back(archivePath + " : " + entryPath + " : " + nested.error().message);
            continue;
        }
        const auto data = reader.fileData(index);
        if (data)
            totals.bytes += data->size();
        else
            totals.failures.push_back(archivePath + " : " + entryPath + " : " + data.error().message);
    }
}

}

class TestRealSweep : public QObject {
    Q_OBJECT
private slots:
    void readsEveryEntryOfTheInstall();
};

void TestRealSweep::readsEveryEntryOfTheInstall()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto install = openGameInstall(*realGameDir());
    QVERIFY(install);
    const auto archives = listArchives(*install);
    Totals totals;
    for (const std::filesystem::path &relative : archives) {
        const auto reader = RpfReader::open(openFileSource(install->root / relative), relative.filename().string(), keys);
        if (reader)
            walk(*reader, relative.generic_string(), totals);
        else
            totals.failures.push_back(relative.generic_string() + " : : " + reader.error().message);
    }
    qInfo("archives %llu (top level %zu), entries %llu, bytes %llu, failures %zu",
        static_cast<unsigned long long>(totals.archives), archives.size(),
        static_cast<unsigned long long>(totals.entries), static_cast<unsigned long long>(totals.bytes), totals.failures.size());
    for (std::size_t i = 0; i < totals.failures.size() && i < 200; ++i)
        qWarning("%s", totals.failures[i].c_str());
    QCOMPARE(totals.failures.size(), std::size_t(0));
    QVERIFY(archives.size() >= 90);
    QVERIFY(totals.entries >= 100000);
}

QTEST_APPLESS_MAIN(TestRealSweep)
#include "tst_real_sweep.moc"
