#include "core/game/game_install.h"
#include "core/io/ascii.h"
#include "core/meta/metadata_document.h"
#include "core/rpf/rpf_reader.h"
#include "core/rsc/rsc7.h"
#include "core/ytd/texture_dictionary.h"
#include "core/ytd/texture_format.h"
#include "support/real_install.h"
#include "support/real_keys.h"

#include <QTest>

#include <map>

using namespace archon;
using namespace archon::test;

namespace {

struct Totals {
    std::uint64_t archives = 0;
    std::uint64_t entries = 0;
    std::uint64_t bytes = 0;
    std::uint64_t dictionaries = 0;
    std::uint64_t textures = 0;
    std::uint64_t metadataExact = 0;
    std::uint64_t metadataReadOnly = 0;
    std::map<std::uint32_t, std::uint64_t> unknownFormats;
    std::vector<std::string> unsupportedVersions;
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
        if (!data) {
            totals.failures.push_back(archivePath + " : " + entryPath + " : " + data.error().message);
            continue;
        }
        totals.bytes += data->size();
        const std::string lower = asciiLower(entry.name);
        if (lower.ends_with(".ymt") || lower.ends_with(".ymap") || lower.ends_with(".ytyp") || lower.ends_with(".ymf") || lower.ends_with(".pso") || lower.ends_with(".rbf")) {
            const auto stored = reader.storedFile(index);
            if (!stored) {
                totals.failures.push_back(archivePath + " : " + entryPath + " : " + stored.error().message);
                continue;
            }
            if (isBinaryMetadata(*stored)) {
                const auto document = openMetadata(*stored, NameTable());
                if (!document)
                    totals.failures.push_back(archivePath + " : " + entryPath + " : " + document.error().message);
                else if (document->readOnlyReason.empty())
                    ++totals.metadataExact;
                else
                    ++totals.metadataReadOnly;
            }
        }
        if (entry.kind == RpfEntryKind::Resource && asciiLower(entry.name).ends_with(".ytd")) {
            const auto textures = readTextureDictionary(*data, rsc7SizeFromFlags(entry.systemFlags), rsc7VersionFromFlags(entry.systemFlags, entry.graphicsFlags));
            if (!textures) {
                const std::uint32_t version = rsc7VersionFromFlags(entry.systemFlags, entry.graphicsFlags);
                if (version != legacyDictionaryVersion && version != enhancedDictionaryVersion)
                    totals.unsupportedVersions.push_back(archivePath + " : " + entryPath + " (version " + std::to_string(version) + ")");
                else
                    totals.failures.push_back(archivePath + " : " + entryPath + " : " + textures.error().message);
                continue;
            }
            ++totals.dictionaries;
            totals.textures += textures->size();
            for (const Texture &texture : *textures) {
                if (!textureFormat(texture.format))
                    ++totals.unknownFormats[texture.format];
            }
        }
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
    qInfo("texture dictionaries %llu, textures %llu", static_cast<unsigned long long>(totals.dictionaries), static_cast<unsigned long long>(totals.textures));
    qInfo("binary metadata saved byte-exact %llu, read-only %llu", static_cast<unsigned long long>(totals.metadataExact), static_cast<unsigned long long>(totals.metadataReadOnly));
    for (const std::string &unsupported : totals.unsupportedVersions)
        qInfo("texture dictionary of an unsupported version: %s", unsupported.c_str());
    for (const auto &[format, count] : totals.unknownFormats)
        qInfo("unknown texture format %u: %llu", format, static_cast<unsigned long long>(count));
    for (std::size_t i = 0; i < totals.failures.size() && i < 200; ++i)
        qWarning("%s", totals.failures[i].c_str());
    QCOMPARE(totals.failures.size(), std::size_t(0));
    QVERIFY(archives.size() >= 90);
    QVERIFY(totals.entries >= 100000);
}

QTEST_APPLESS_MAIN(TestRealSweep)
#include "tst_real_sweep.moc"
