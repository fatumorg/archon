#include "core/crypto/hash.h"
#include "core/io/ascii.h"
#include "core/rpf/rpf_reader.h"
#include "core/rpf/rpf_toc.h"
#include "core/rpf/rpf_writer.h"
#include "core/safety/transaction.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTest>

#include <map>
#include <memory>
#include <set>

using namespace archon;
using namespace archon::test;

namespace {

using Edit = ArchiveEdit;

std::vector<std::byte> text(std::size_t count, unsigned seed)
{
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i)
        bytes[i] = static_cast<std::byte>('a' + (i * 7 + i / 13 + seed) % 23);
    return bytes;
}

RpfReader openReader(const std::filesystem::path &file, const Keys *keys = nullptr)
{
    return RpfReader::open(openFileSource(file), file.filename().string(), keys).value();
}

std::vector<std::byte> contentOf(const RpfReader &reader, std::string_view path)
{
    const auto index = reader.archive().find(path);
    if (!index)
        return toVector("<missing>");
    const auto data = reader.fileData(*index);
    return data ? *data : toVector("<unreadable: " + data.error().message + ">");
}

std::set<std::string> treeOf(const RpfReader &reader)
{
    std::set<std::string> paths;
    const auto &entries = reader.archive().entries();
    for (std::uint32_t index = 1; index < entries.size(); ++index)
        paths.insert(reader.archive().path(index) + (entries[index].kind == RpfEntryKind::Directory ? "/" : ""));
    return paths;
}

bool childrenAreSorted(const RpfArchive &archive)
{
    const auto &entries = archive.entries();
    for (const RpfEntry &entry : entries) {
        if (entry.kind != RpfEntryKind::Directory)
            continue;
        for (std::uint32_t child = entry.firstChild + 1; child < entry.firstChild + entry.childCount; ++child) {
            if (!(asciiLower(entries[child - 1].name) < asciiLower(entries[child].name)))
                return false;
        }
    }
    return true;
}

std::vector<std::byte> commonArchive()
{
    RpfBuilder inner;
    inner.addBinary("deep/c.txt", toVector("deep content"), false);
    RpfBuilder common;
    common.addBinary("data/a.txt", toVector("hello"), false);
    common.addBinary("data/b.bin", std::vector<std::byte>(5000, std::byte { 'z' }), true);
    common.addResource("x.ytd", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    common.addBinary("inner.rpf", inner.build(), false);
    return common.build();
}

std::uint32_t wordAt(const std::vector<std::byte> &bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}

void setWord(std::vector<std::byte> &bytes, std::size_t offset, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

void encryptRange(std::vector<std::byte> &bytes, std::size_t offset, std::size_t size, const Keys &keys)
{
    const auto encrypted = aesEcbEncrypt(std::span(bytes).subspan(offset, size - size % 16), keys.aes);
    std::ranges::copy(encrypted, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<std::byte> asAesArchive(std::vector<std::byte> bytes, const Keys &keys, std::string_view encryptedEntry)
{
    const std::uint32_t count = wordAt(bytes, 4);
    const std::uint32_t namesLength = wordAt(bytes, 8);
    const std::size_t names = 16 + count * 16;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t entry = 16 + index * 16;
        const std::uint32_t low = wordAt(bytes, entry);
        const std::uint32_t high = wordAt(bytes, entry + 4);
        if (high == 0x7FFFFF00 || (high & 0x80000000) != 0)
            continue;
        if (std::string(reinterpret_cast<const char *>(&bytes[names + (low & 0xFFFF)])) != encryptedEntry)
            continue;
        const std::uint32_t stored = (low >> 16) | ((high & 0xFF) << 16);
        setWord(bytes, entry + 12, 1);
        encryptRange(bytes, std::size_t(high >> 8) * 512, stored != 0 ? stored : wordAt(bytes, entry + 8), keys);
    }
    setWord(bytes, 12, 0x0FFFFFF9);
    encryptRange(bytes, 16, count * 16, keys);
    encryptRange(bytes, names, namesLength, keys);
    return bytes;
}

std::vector<std::byte> resourceFile(const std::vector<std::byte> &pages, std::uint32_t systemFlags, std::uint32_t graphicsFlags)
{
    std::vector<std::byte> file;
    for (const std::uint32_t word : { 0x37435352u, 13u, systemFlags, graphicsFlags }) {
        for (int i = 0; i < 4; ++i)
            file.push_back(static_cast<std::byte>(word >> (8 * i)));
    }
    const auto body = deflateRaw(pages);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

QByteArray fileDigest(const std::filesystem::path &file)
{
    QFile opened(file);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return opened.open(QIODevice::ReadOnly) && hash.addData(&opened) ? hash.result() : QByteArray();
}

void hashEntries(const RpfReader &reader, const std::string &prefix, std::map<std::string, std::array<std::byte, 32>> &hashes)
{
    const auto &entries = reader.archive().entries();
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
        const RpfEntry &entry = entries[index];
        if (entry.kind == RpfEntryKind::Directory)
            continue;
        const std::string path = prefix + reader.archive().path(index);
        if (entry.kind == RpfEntryKind::Binary && entry.storedSize == 0 && asciiLower(entry.name).ends_with(".rpf")) {
            if (const auto nested = reader.openNested(index)) {
                hashEntries(*nested, path + "/", hashes);
                continue;
            }
        }
        const auto data = reader.storedFile(index);
        hashes[path] = data ? sha256(*data) : std::array<std::byte, 32> {};
    }
}

}

class TestRpfWriter : public QObject {
    Q_OBJECT
private slots:
    void init();
    void entryNameRule();
    void serializedTocParsesBack();
    void addsReplacesRemovesRenamesAndMakesFolders();
    void resourceSourceBecomesResourceEntry();
    void rawExtensionsStayUncompressedAndTextShrinks();
    void replacedRawEntryStaysRaw();
    void tocGrowthRelocatesFirstEntries();
    void editsNestedArchiveInTheMiddleOfItsParent();
    void editsArchiveTwoLevelsDeep();
    void rollbackRestoresByteIdentical();
    void failingEditLeavesNothingBehind();
    void hostileNamesAreRefused();
    void clashesAndMissingTargetsAreErrors();
    void aesArchiveIsConverted();
    void aesArchiveWithScriptsIsRefused();
    void editsCopiesOfRealArchives();
    void rebuildDropsDeadSpace();
    void rebuildRollbackRestoresOriginal();
    void rebuildDecryptsAesArchive();
    void rebuildRefusesAesScripts();
    void rebuildsCopyOfCommon();

private:
    std::filesystem::path source(const std::string &name, std::span<const std::byte> content)
    {
        const std::filesystem::path file = pathOf(*directory_) / "sources" / name;
        writeFile(file, content);
        return file;
    }
    Edit put(std::string path, const std::filesystem::path &from) const
    {
        return { Edit::Kind::Put, std::move(path), from, {} };
    }
    Result<void> apply(const std::vector<Edit> &edits, const std::vector<std::string> &nested = {}, const Keys *keys = nullptr)
    {
        auto transaction = Transaction::begin(store_, root_, "edit");
        if (!transaction)
            return std::unexpected(transaction.error());
        const auto edited = editArchive(*transaction, file_, nested, edits, keys);
        if (!edited)
            return edited;
        return transaction->commit();
    }
    Result<void> rebuild(const Keys *keys = nullptr)
    {
        auto transaction = Transaction::begin(store_, root_, "rebuild");
        if (!transaction)
            return std::unexpected(transaction.error());
        const auto rebuilt = rebuildArchive(*transaction, file_, keys);
        if (!rebuilt)
            return rebuilt;
        return transaction->commit();
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    std::filesystem::path file_;
};

void TestRpfWriter::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    file_ = root_ / "common.rpf";
    writeFile(file_, commonArchive());
}

void TestRpfWriter::entryNameRule()
{
    QVERIFY(isValidEntryName("a.txt"));
    QVERIFY(isValidEntryName("with space.ytd"));
    QVERIFY(!isValidEntryName(""));
    QVERIFY(!isValidEntryName("."));
    QVERIFY(!isValidEntryName(".."));
    QVERIFY(!isValidEntryName("a/b"));
    QVERIFY(!isValidEntryName("a\\b"));
    QVERIFY(!isValidEntryName(std::string_view("a\0b", 3)));
    QVERIFY(isValidEntryName(std::string(255, 'n')));
    QVERIFY(!isValidEntryName(std::string(256, 'n')));
}

void TestRpfWriter::serializedTocParsesBack()
{
    const RpfReader reader = openReader(file_);
    const auto &entries = reader.archive().entries();
    const auto toc = serializeToc(entries);
    QVERIFY(toc);
    const auto parsed = RpfArchive::parse(*toc, "common.rpf", std::filesystem::file_size(file_), nullptr);
    QVERIFY2(parsed, parsed ? "" : parsed.error().message.c_str());
    QCOMPARE(parsed->encryption(), RpfEncryption::Open);
    QCOMPARE(parsed->entries().size(), entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const RpfEntry &before = entries[index];
        const RpfEntry &after = parsed->entries()[index];
        QCOMPARE(after.name, before.name);
        QCOMPARE(after.kind, before.kind);
        QCOMPARE(after.offset, before.offset);
        QCOMPARE(after.storedSize, before.storedSize);
        QCOMPARE(after.uncompressedSize, before.uncompressedSize);
        QCOMPARE(after.systemFlags, before.systemFlags);
        QCOMPARE(after.graphicsFlags, before.graphicsFlags);
        QCOMPARE(after.firstChild, before.firstChild);
        QCOMPARE(after.childCount, before.childCount);
    }
}

void TestRpfWriter::addsReplacesRemovesRenamesAndMakesFolders()
{
    const auto fresh = text(3000, 1);
    const auto replacement = text(700, 2);
    const auto applied = apply({
        put("data/new.txt", source("new.txt", fresh)),
        put("DATA/A.TXT", source("a.txt", replacement)),
        { Edit::Kind::Remove, "x.ytd", {}, {} },
        { Edit::Kind::MakeDirectory, "empty", {}, {} },
        { Edit::Kind::Rename, "data/b.bin", {}, "c.bin" },
        put("fresh/deep/z.txt", source("z.txt", toVector("zed"))),
    });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());

    const RpfReader reader = openReader(file_);
    const std::set<std::string> expected { "data/", "data/a.txt", "data/c.bin", "data/new.txt", "empty/", "fresh/", "fresh/deep/", "fresh/deep/z.txt", "inner.rpf" };
    QCOMPARE(treeOf(reader), expected);
    QCOMPARE(contentOf(reader, "data/new.txt"), fresh);
    QCOMPARE(contentOf(reader, "data/a.txt"), replacement);
    QCOMPARE(contentOf(reader, "data/c.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    QCOMPARE(toText(contentOf(reader, "fresh/deep/z.txt")), std::string("zed"));
    QCOMPARE(reader.archive().encryption(), RpfEncryption::Open);
    QCOMPARE(std::filesystem::file_size(file_) % 512, std::uintmax_t(0));
    QVERIFY(childrenAreSorted(reader.archive()));
    const auto inner = reader.openNested(*reader.archive().find("inner.rpf"));
    QVERIFY(inner);
    QCOMPARE(toText(contentOf(*inner, "deep/c.txt")), std::string("deep content"));
}

void TestRpfWriter::resourceSourceBecomesResourceEntry()
{
    const std::vector<std::byte> pages(0x400, std::byte { 5 });
    const auto file = resourceFile(pages, 0x08000000, 0x08000000);
    const auto applied = apply({ put("model.ydr", source("model.ydr", file)) });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());
    const RpfReader reader = openReader(file_);
    const auto index = reader.archive().find("model.ydr");
    QVERIFY(index);
    const RpfEntry &entry = reader.archive().entries()[*index];
    QCOMPARE(entry.kind, RpfEntryKind::Resource);
    QCOMPARE(entry.systemFlags, std::uint32_t(0x08000000));
    QCOMPARE(entry.graphicsFlags, std::uint32_t(0x08000000));
    QCOMPARE(entry.storedSize, std::uint32_t(file.size()));
    QCOMPARE(contentOf(reader, "model.ydr"), pages);
}

void TestRpfWriter::rawExtensionsStayUncompressedAndTextShrinks()
{
    const auto content = text(40000, 3);
    const auto applied = apply({
        put("sound.awc", source("sound.awc", content)),
        put("movie.bik", source("movie.bik", content)),
        put("notes.txt", source("notes.txt", content)),
        put("tiny.txt", source("tiny.txt", toVector("x"))),
        put("nothing.txt", source("nothing.txt", {})),
    });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());
    const RpfReader reader = openReader(file_);
    const auto &entries = reader.archive().entries();
    QCOMPARE(entries[*reader.archive().find("sound.awc")].storedSize, std::uint32_t(0));
    QCOMPARE(entries[*reader.archive().find("movie.bik")].storedSize, std::uint32_t(0));
    const std::uint32_t stored = entries[*reader.archive().find("notes.txt")].storedSize;
    QVERIFY(stored != 0 && stored < 40000);
    QCOMPARE(entries[*reader.archive().find("tiny.txt")].storedSize, std::uint32_t(0));
    QCOMPARE(contentOf(reader, "sound.awc"), content);
    QCOMPARE(contentOf(reader, "notes.txt"), content);
    QCOMPARE(toText(contentOf(reader, "tiny.txt")), std::string("x"));
    QVERIFY(contentOf(reader, "nothing.txt").empty());
}

void TestRpfWriter::replacedRawEntryStaysRaw()
{
    const auto content = text(40000, 4);
    const auto applied = apply({ put("data/a.txt", source("a.txt", content)) });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());
    const RpfReader reader = openReader(file_);
    QCOMPARE(reader.archive().entries()[*reader.archive().find("data/a.txt")].storedSize, std::uint32_t(0));
    QCOMPARE(contentOf(reader, "data/a.txt"), content);
}

void TestRpfWriter::tocGrowthRelocatesFirstEntries()
{
    std::vector<Edit> edits;
    for (int i = 0; i < 200; ++i) {
        const std::string name = "generated_file_number_" + std::to_string(i) + ".txt";
        edits.push_back(put("bulk/" + name, source(name, text(100 + i, i))));
    }
    const auto applied = apply(edits);
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());
    const RpfReader reader = openReader(file_);
    QCOMPARE(toText(contentOf(reader, "data/a.txt")), std::string("hello"));
    QCOMPARE(contentOf(reader, "data/b.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    QCOMPARE(contentOf(reader, "x.ytd"), std::vector<std::byte>(0x400, std::byte { 7 }));
    for (int i = 0; i < 200; ++i)
        QCOMPARE(contentOf(reader, "bulk/generated_file_number_" + std::to_string(i) + ".txt"), text(100 + i, i));
    const auto inner = reader.openNested(*reader.archive().find("inner.rpf"));
    QVERIFY(inner);
    QCOMPARE(toText(contentOf(*inner, "deep/c.txt")), std::string("deep content"));
    QVERIFY(childrenAreSorted(reader.archive()));
}

void TestRpfWriter::editsNestedArchiveInTheMiddleOfItsParent()
{
    RpfBuilder inner;
    inner.addBinary("deep/c.txt", toVector("deep content"), false);
    RpfBuilder parent;
    parent.addBinary("a.bin", text(3000, 5), false);
    parent.addBinary("inner.rpf", inner.build(), false);
    parent.addBinary("z.bin", text(2000, 6), true);
    writeFile(file_, parent.build());

    const auto added = text(9000, 7);
    const auto applied = apply({ put("deep/added.txt", source("added.txt", added)) }, { "inner.rpf" });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());

    const RpfReader reader = openReader(file_);
    QCOMPARE(reader.archive().encryption(), RpfEncryption::Open);
    QCOMPARE(contentOf(reader, "a.bin"), text(3000, 5));
    QCOMPARE(contentOf(reader, "z.bin"), text(2000, 6));
    const auto nested = reader.openNested(*reader.archive().find("inner.rpf"));
    QVERIFY2(nested, nested ? "" : nested.error().message.c_str());
    QCOMPARE(nested->archive().encryption(), RpfEncryption::Open);
    QCOMPARE(toText(contentOf(*nested, "deep/c.txt")), std::string("deep content"));
    QCOMPARE(contentOf(*nested, "deep/added.txt"), added);
    QCOMPARE(std::filesystem::file_size(file_) % 512, std::uintmax_t(0));
}

void TestRpfWriter::editsArchiveTwoLevelsDeep()
{
    RpfBuilder inner;
    inner.addBinary("deep/c.txt", toVector("deep content"), false);
    RpfBuilder middle;
    middle.addBinary("packs/inner.rpf", inner.build(), false);
    middle.addBinary("packs/zz.bin", text(1500, 8), false);
    RpfBuilder parent;
    parent.addBinary("a.bin", text(3000, 5), false);
    parent.addBinary("middle.rpf", middle.build(), false);
    parent.addBinary("z.bin", text(2000, 6), true);
    writeFile(file_, parent.build());

    const auto added = text(70000, 9);
    const auto applied = apply({ put("added.txt", source("added.txt", added)), { Edit::Kind::Remove, "deep", {}, {} } }, { "middle.rpf", "packs/inner.rpf" });
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());

    const RpfReader reader = openReader(file_);
    QCOMPARE(contentOf(reader, "a.bin"), text(3000, 5));
    QCOMPARE(contentOf(reader, "z.bin"), text(2000, 6));
    const auto middleReader = reader.openNested(*reader.archive().find("middle.rpf"));
    QVERIFY2(middleReader, middleReader ? "" : middleReader.error().message.c_str());
    QCOMPARE(contentOf(*middleReader, "packs/zz.bin"), text(1500, 8));
    const auto innerReader = middleReader->openNested(*middleReader->archive().find("packs/inner.rpf"));
    QVERIFY2(innerReader, innerReader ? "" : innerReader.error().message.c_str());
    QCOMPARE(treeOf(*innerReader), std::set<std::string> { "added.txt" });
    QCOMPARE(contentOf(*innerReader, "added.txt"), added);
}

void TestRpfWriter::rollbackRestoresByteIdentical()
{
    const auto original = readFile(file_);
    auto transaction = Transaction::begin(store_, root_, "edit");
    QVERIFY(transaction);
    std::vector<Edit> edits;
    for (int i = 0; i < 120; ++i) {
        const std::string name = "rolled_back_file_" + std::to_string(i) + ".txt";
        edits.push_back(put(name, source(name, text(300, i))));
    }
    edits.push_back({ Edit::Kind::Remove, "data", {}, {} });
    QVERIFY(editArchive(*transaction, file_, {}, edits, nullptr));
    QVERIFY(editArchive(*transaction, file_, std::vector<std::string> { "inner.rpf" }, std::vector<Edit> { put("more.txt", source("more.txt", text(5000, 1))) }, nullptr));
    QVERIFY(readFile(file_) != original);
    QVERIFY(transaction->rollback());
    QCOMPARE(sha256(readFile(file_)), sha256(original));
}

void TestRpfWriter::failingEditLeavesNothingBehind()
{
    const auto original = readFile(file_);
    const auto applied = apply({ put("ok.txt", source("ok.txt", text(900, 1))), put("gone.txt", pathOf(*directory_) / "sources" / "does-not-exist.txt") });
    QVERIFY(!applied);
    QCOMPARE(readFile(file_), original);
    QVERIFY(listTransactions(store_).empty());
}

void TestRpfWriter::hostileNamesAreRefused()
{
    const auto original = readFile(file_);
    const auto content = source("ok.txt", text(10, 1));
    QVERIFY(!apply({ put("..", content) }));
    QVERIFY(!apply({ put("data/../up.txt", content) }));
    QVERIFY(!apply({ put("a\\b", content) }));
    QVERIFY(!apply({ put(std::string(300, 'n'), content) }));
    QVERIFY(!apply({ { Edit::Kind::MakeDirectory, "data/..", {}, {} } }));
    QVERIFY(!apply({ { Edit::Kind::Rename, "data/a.txt", {}, "x/y" } }));
    QCOMPARE(readFile(file_), original);
}

void TestRpfWriter::clashesAndMissingTargetsAreErrors()
{
    const auto original = readFile(file_);
    const auto content = source("ok.txt", text(10, 1));
    QVERIFY(!apply({ put("data", content) }));
    QVERIFY(!apply({ put("data/a.txt/under.txt", content) }));
    QVERIFY(!apply({ { Edit::Kind::Remove, "missing.txt", {}, {} } }));
    QVERIFY(!apply({ { Edit::Kind::Remove, "", {}, {} } }));
    QVERIFY(!apply({ { Edit::Kind::Rename, "data/a.txt", {}, "B.BIN" } }));
    QVERIFY(!apply({ { Edit::Kind::MakeDirectory, "DATA", {}, {} } }));
    QVERIFY(!apply({ put("x.txt", content) }, { "missing.rpf" }));
    QVERIFY(!apply({ put("x.txt", content) }, { "data/a.txt" }));
    QCOMPARE(readFile(file_), original);
}

void TestRpfWriter::aesArchiveIsConverted()
{
    Keys keys {};
    for (std::size_t i = 0; i < keys.aes.size(); ++i)
        keys.aes[i] = static_cast<std::byte>(i * 3 + 1);
    writeFile(file_, asAesArchive(commonArchive(), keys, "b.bin"));
    {
        const RpfReader before = openReader(file_, &keys);
        QCOMPARE(before.archive().encryption(), RpfEncryption::Aes);
        QVERIFY(before.archive().entries()[*before.archive().find("data/b.bin")].encrypted);
        QCOMPARE(contentOf(before, "data/b.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    }
    const auto applied = apply({ put("new.txt", source("new.txt", toVector("fresh"))) }, {}, &keys);
    QVERIFY2(applied, applied ? "" : applied.error().message.c_str());
    const RpfReader reader = openReader(file_, &keys);
    QCOMPARE(reader.archive().encryption(), RpfEncryption::Open);
    QVERIFY(!reader.archive().entries()[*reader.archive().find("data/b.bin")].encrypted);
    QCOMPARE(contentOf(reader, "data/b.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    QCOMPARE(toText(contentOf(reader, "data/a.txt")), std::string("hello"));
    QCOMPARE(toText(contentOf(reader, "new.txt")), std::string("fresh"));
}

void TestRpfWriter::aesArchiveWithScriptsIsRefused()
{
    Keys keys {};
    RpfBuilder builder;
    builder.addBinary("a.txt", toVector("hello"), false);
    builder.addResource("main.ysc", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    const auto archive = asAesArchive(builder.build(), keys, "");
    writeFile(file_, archive);
    const auto applied = apply({ put("new.txt", source("new.txt", toVector("fresh"))) }, {}, &keys);
    QVERIFY(!applied);
    QVERIFY(applied.error().message.find("scripts in an AES archive") != std::string::npos);
    QCOMPARE(readFile(file_), archive);
}

void TestRpfWriter::editsCopiesOfRealArchives()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    std::filesystem::create_directories(scratchDirectory());
    QTemporaryDir scratch(QString::fromStdString((scratchDirectory() / "writer-XXXXXX").string()));
    QVERIFY(scratch.isValid());
    const std::filesystem::path root = pathOf(scratch) / "game";
    const std::filesystem::path store = pathOf(scratch) / "store";
    const auto added = text(50000, 11);
    const std::filesystem::path addedFile = source("archon_added.txt", added);

    for (const char *relative : { "common.rpf", "update/update.rpf" }) {
        const std::filesystem::path file = root / relative;
        std::filesystem::create_directories(file.parent_path());
        std::filesystem::copy_file(*realGameDir() / relative, file);
        const std::string name = file.filename().string();

        std::map<std::string, std::array<std::byte, 32>> before;
        std::vector<std::string> nested;
        std::string encryptedPath;
        QByteArray originalDigest;
        {
            const auto reader = RpfReader::open(openFileSource(file), name, keys);
            QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
            hashEntries(*reader, "", before);
            const auto &entries = reader->archive().entries();
            for (std::uint32_t index = 0; index < entries.size() && nested.empty(); ++index) {
                if (entries[index].kind == RpfEntryKind::Binary && asciiLower(entries[index].name).ends_with(".rpf"))
                    nested.push_back(reader->archive().path(index));
            }
            for (std::uint32_t index = 0; index < entries.size() && nested.empty() && encryptedPath.empty(); ++index) {
                if (entries[index].kind == RpfEntryKind::Binary && entries[index].encrypted)
                    encryptedPath = reader->archive().path(index);
            }
            originalDigest = fileDigest(file);
        }
        const std::string folder = nested.empty() ? "data/" : "";
        const std::string prefix = nested.empty() ? "" : nested[0] + "/";

        auto transaction = Transaction::begin(store, root, "real edit");
        QVERIFY(transaction);
        std::vector<Edit> edits { put(folder + "archon_added.txt", addedFile) };
        if (!encryptedPath.empty()) {
            edits.push_back({ Edit::Kind::Rename, encryptedPath, {}, "archon_renamed.dat" });
            const std::string renamed = encryptedPath.substr(0, encryptedPath.rfind('/') + 1) + "archon_renamed.dat";
            before[renamed] = before.at(encryptedPath);
            before.erase(encryptedPath);
            qInfo("renaming encrypted entry %s", encryptedPath.c_str());
        }
        const auto edited = editArchive(*transaction, file, nested, edits, keys);
        QVERIFY2(edited, edited ? "" : edited.error().message.c_str());
        {
            const auto reader = RpfReader::open(openFileSource(file), name, keys);
            QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
            QCOMPARE(reader->archive().encryption(), RpfEncryption::Open);
            std::map<std::string, std::array<std::byte, 32>> after;
            hashEntries(*reader, "", after);
            const std::string addedPath = prefix + folder + "archon_added.txt";
            QVERIFY(after.contains(addedPath));
            QCOMPARE(after.at(addedPath), sha256(added));
            after.erase(addedPath);
            QCOMPARE(after.size(), before.size());
            std::size_t different = 0;
            for (const auto &[path, digest] : before)
                different += after.contains(path) && after.at(path) == digest ? 0 : 1;
            QCOMPARE(different, std::size_t(0));
            qInfo("%s: %zu entries unchanged after edit", relative, before.size());
        }
        QVERIFY(transaction->rollback());
        QCOMPARE(fileDigest(file), originalDigest);
        std::filesystem::remove(file);
    }
}

void TestRpfWriter::rebuildDropsDeadSpace()
{
    const auto big = source("big.awc", text(1 << 20, 3));
    QVERIFY(apply({ put("big.awc", big) }));
    QVERIFY(apply({ put("big.awc", big) }, { "inner.rpf" }));
    QVERIFY(apply({ { Edit::Kind::Remove, "big.awc", {}, {} } }));
    QVERIFY(apply({ { Edit::Kind::Remove, "big.awc", {}, {} } }, { "inner.rpf" }));
    const auto editedSize = std::filesystem::file_size(file_);
    std::map<std::string, std::array<std::byte, 32>> before;
    std::set<std::string> treeBefore;
    {
        const RpfReader reader = openReader(file_);
        hashEntries(reader, "", before);
        treeBefore = treeOf(reader);
    }

    const auto rebuilt = rebuild();
    QVERIFY2(rebuilt, rebuilt ? "" : rebuilt.error().message.c_str());

    QVERIFY(std::filesystem::file_size(file_) < editedSize);
    QCOMPARE(std::filesystem::file_size(file_) % 512, std::uintmax_t(0));
    const RpfReader reader = openReader(file_);
    QCOMPARE(reader.archive().encryption(), RpfEncryption::Open);
    QVERIFY(childrenAreSorted(reader.archive()));
    QCOMPARE(treeOf(reader), treeBefore);
    std::map<std::string, std::array<std::byte, 32>> after;
    hashEntries(reader, "", after);
    QCOMPARE(after, before);
    QVERIFY(after.contains("inner.rpf/deep/c.txt"));
    QCOMPARE(contentOf(reader, "data/b.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    QVERIFY(!std::filesystem::exists(file_.string() + ".archon-tmp"));
}

void TestRpfWriter::rebuildRollbackRestoresOriginal()
{
    QVERIFY(apply({ put("gone.txt", source("gone.txt", text(90000, 2))) }));
    QVERIFY(apply({ { Edit::Kind::Remove, "gone.txt", {}, {} } }));
    const auto original = readFile(file_);
    auto transaction = Transaction::begin(store_, root_, "rebuild");
    QVERIFY(transaction);
    const auto rebuilt = rebuildArchive(*transaction, file_, nullptr);
    QVERIFY2(rebuilt, rebuilt ? "" : rebuilt.error().message.c_str());
    QVERIFY(readFile(file_) != original);
    QVERIFY(transaction->rollback());
    QCOMPARE(sha256(readFile(file_)), sha256(original));
}

void TestRpfWriter::rebuildDecryptsAesArchive()
{
    Keys keys {};
    for (std::size_t i = 0; i < keys.aes.size(); ++i)
        keys.aes[i] = static_cast<std::byte>(i * 5 + 2);
    writeFile(file_, asAesArchive(commonArchive(), keys, "b.bin"));
    const auto rebuilt = rebuild(&keys);
    QVERIFY2(rebuilt, rebuilt ? "" : rebuilt.error().message.c_str());
    const RpfReader reader = openReader(file_, &keys);
    QCOMPARE(reader.archive().encryption(), RpfEncryption::Open);
    QVERIFY(!reader.archive().entries()[*reader.archive().find("data/b.bin")].encrypted);
    QCOMPARE(contentOf(reader, "data/b.bin"), std::vector<std::byte>(5000, std::byte { 'z' }));
    QCOMPARE(toText(contentOf(reader, "data/a.txt")), std::string("hello"));
    const auto nested = reader.openNested(*reader.archive().find("inner.rpf"));
    QVERIFY2(nested, nested ? "" : nested.error().message.c_str());
    QCOMPARE(toText(contentOf(*nested, "deep/c.txt")), std::string("deep content"));
}

void TestRpfWriter::rebuildRefusesAesScripts()
{
    Keys keys {};
    RpfBuilder builder;
    builder.addBinary("a.txt", toVector("hello"), false);
    builder.addResource("main.ysc", std::vector<std::byte>(0x400, std::byte { 7 }), 0x08000000, 0x08000000);
    const auto archive = asAesArchive(builder.build(), keys, "");
    writeFile(file_, archive);
    const auto rebuilt = rebuild(&keys);
    QVERIFY(!rebuilt);
    QVERIFY(rebuilt.error().message.find("scripts in an AES archive") != std::string::npos);
    QCOMPARE(readFile(file_), archive);
    QVERIFY(listTransactions(store_).empty());
}

void TestRpfWriter::rebuildsCopyOfCommon()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    std::filesystem::create_directories(scratchDirectory());
    QTemporaryDir scratch(QString::fromStdString((scratchDirectory() / "rebuild-XXXXXX").string()));
    QVERIFY(scratch.isValid());
    const std::filesystem::path root = pathOf(scratch) / "game";
    const std::filesystem::path store = pathOf(scratch) / "store";
    const std::filesystem::path file = root / "common.rpf";
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(*realGameDir() / "common.rpf", file);

    std::map<std::string, std::array<std::byte, 32>> before;
    hashEntries(RpfReader::open(openFileSource(file), "common.rpf", keys).value(), "", before);
    const QByteArray originalDigest = fileDigest(file);

    auto transaction = Transaction::begin(store, root, "rebuild");
    QVERIFY(transaction);
    const auto rebuilt = rebuildArchive(*transaction, file, keys);
    QVERIFY2(rebuilt, rebuilt ? "" : rebuilt.error().message.c_str());
    {
        const auto reader = RpfReader::open(openFileSource(file), "common.rpf", keys);
        QVERIFY2(reader, reader ? "" : reader.error().message.c_str());
        QCOMPARE(reader->archive().encryption(), RpfEncryption::Open);
        std::map<std::string, std::array<std::byte, 32>> after;
        hashEntries(*reader, "", after);
        QCOMPARE(after.size(), before.size());
        QVERIFY(after == before);
        qInfo("common.rpf: %zu entries unchanged after rebuild", before.size());
    }
    QVERIFY(transaction->rollback());
    QCOMPARE(fileDigest(file), originalDigest);
}

QTEST_APPLESS_MAIN(TestRpfWriter)
#include "tst_rpf_writer.moc"
