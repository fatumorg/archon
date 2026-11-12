#include "core/game/game_browser.h"
#include "core/game/game_edit.h"
#include "core/rpf/rpf_reader.h"
#include "core/safety/transaction.h"
#include "support/browse_fixture.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>

using namespace archon;
using namespace archon::test;

namespace {

bool listed(GameBrowser &browser, std::string_view folder, std::string_view name)
{
    const auto nodes = browser.list(folder);
    return nodes && std::ranges::any_of(*nodes, [name](const BrowserNode &node) { return node.name == name; });
}

std::string readEntry(const std::filesystem::path &archive, std::span<const std::string> nested, std::string_view path)
{
    auto reader = RpfReader::open(openFileSource(archive), archive.filename().string(), nullptr);
    if (!reader)
        return "<" + reader.error().message + ">";
    for (const std::string &level : nested) {
        const auto index = reader->archive().find(level);
        if (!index)
            return "<missing " + level + ">";
        auto next = reader->openNested(*index);
        if (!next)
            return "<" + next.error().message + ">";
        reader = std::move(next);
    }
    const auto index = reader->archive().find(path);
    if (!index)
        return "<missing>";
    const auto data = reader->fileData(*index);
    return data ? toText(*data) : "<" + data.error().message + ">";
}

}

class TestGameEdit : public QObject {
    Q_OBJECT
private slots:
    void init();
    void locateSplitsDiskArchiveAndInner();
    void importsIntoArchiveAndNested();
    void importsFolderRecursively();
    void importsOntoDisk();
    void replacesLooseFileAndRollsBack();
    void replacesArchiveEntry();
    void removesSeveralEntries();
    void diskFolderRemoveIsRefused();
    void makesFolderOnDiskAndInArchive();
    void renamesEntry();
    void diskRenameIsRefused();
    void rebuildsContainingArchive();
    void copyToModsCopiesOnceAndMapsPath();
    void modsFolderIsMatchedWithoutCase();
    void failedImportRollsBack();
    void everyOperationIsOneHistoryEntry();

private:
    std::filesystem::path source(const std::string &relative, std::string_view content)
    {
        const std::filesystem::path file = sources_ / relative;
        writeFile(file, content);
        return file;
    }
    GameEditor editor() const
    {
        return GameEditor(root_, nullptr, store_);
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    std::filesystem::path sources_;
};

void TestGameEdit::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    sources_ = pathOf(*directory_) / "sources";
    writeBrowseFixture(root_);
}

void TestGameEdit::locateSplitsDiskArchiveAndInner()
{
    GameBrowser browser(root_, nullptr);
    const auto deep = browser.locate("COMMON.rpf/inner.rpf/deep/c.txt", false);
    QVERIFY2(deep, deep ? "" : deep.error().message.c_str());
    QCOMPARE(deep->archiveFile, root_ / "common.rpf");
    QCOMPARE(deep->nested, std::vector<std::string> { "inner.rpf" });
    QCOMPARE(deep->inner, std::string("deep/c.txt"));
    QCOMPARE(deep->name, std::string("c.txt"));
    QCOMPARE(deep->canonical, std::string("common.rpf/inner.rpf/deep/c.txt"));
    QVERIFY(!deep->directory);

    const auto archiveRoot = browser.locate("common.rpf", true);
    QVERIFY(archiveRoot);
    QCOMPARE(archiveRoot->archiveFile, root_ / "common.rpf");
    QVERIFY(archiveRoot->nested.empty());
    QCOMPARE(archiveRoot->inner, std::string());
    QVERIFY(archiveRoot->directory);

    const auto archiveFile = browser.locate("common.rpf", false);
    QVERIFY(archiveFile);
    QVERIFY(archiveFile->archiveFile.empty());
    QCOMPARE(archiveFile->disk, root_ / "common.rpf");
    QVERIFY(!archiveFile->directory);

    const auto nestedFile = browser.locate("common.rpf/inner.rpf", false);
    QVERIFY(nestedFile);
    QCOMPARE(nestedFile->archiveFile, root_ / "common.rpf");
    QVERIFY(nestedFile->nested.empty());
    QCOMPARE(nestedFile->inner, std::string("inner.rpf"));

    const auto folder = browser.locate("update", true);
    QVERIFY(folder);
    QVERIFY(folder->archiveFile.empty());
    QVERIFY(folder->directory);

    QVERIFY(isInArchive(browser, "common.rpf"));
    QVERIFY(isInArchive(browser, "common.rpf/data"));
    QVERIFY(!isInArchive(browser, "update"));
    QVERIFY(!isInArchive(browser, "GTA5.exe"));
    QVERIFY(isInMods("mods/common.rpf"));
    QVERIFY(isInMods("MODS"));
    QVERIFY(!isInMods("modsx/a"));
    QVERIFY(!isInMods("update/mods"));
}

void TestGameEdit::importsIntoArchiveAndNested()
{
    const std::vector<std::filesystem::path> files { source("one.txt", "first"), source("two.txt", "second") };
    const auto imported = editor().import("common.rpf/data", files);
    QVERIFY2(imported, imported ? "" : imported.error().message.c_str());
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "data/one.txt"), std::string("first"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "data/two.txt"), std::string("second"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "data/a.txt"), std::string("hello"));

    const std::vector<std::filesystem::path> deep { source("three.txt", "third") };
    const auto nested = editor().import("common.rpf/inner.rpf/deep", deep);
    QVERIFY2(nested, nested ? "" : nested.error().message.c_str());
    const std::vector<std::string> chain { "inner.rpf" };
    QCOMPARE(readEntry(root_ / "common.rpf", chain, "deep/three.txt"), std::string("third"));
    QCOMPARE(readEntry(root_ / "common.rpf", chain, "deep/c.txt"), std::string("deep content"));
}

void TestGameEdit::importsFolderRecursively()
{
    source("pack/a.txt", "a");
    source("pack/sub/b.txt", "b");
    std::filesystem::create_symlink(sources_ / "pack" / "a.txt", sources_ / "pack" / "link.txt");
    const std::vector<std::filesystem::path> folders { sources_ / "pack" };
    const auto imported = editor().import("common.rpf", folders);
    QVERIFY2(imported, imported ? "" : imported.error().message.c_str());
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "pack/a.txt"), std::string("a"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "pack/sub/b.txt"), std::string("b"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "pack/link.txt"), std::string("<missing>"));
    QCOMPARE(listTransactions(store_).front().description, std::string("Add 2 files to common.rpf"));
}

void TestGameEdit::importsOntoDisk()
{
    source("pack/x.dll", "dll");
    const std::vector<std::filesystem::path> items { source("readme.txt", "read"), sources_ / "pack" };
    const auto imported = editor().import("update", items);
    QVERIFY2(imported, imported ? "" : imported.error().message.c_str());
    QCOMPARE(toText(readFile(root_ / "update" / "readme.txt")), std::string("read"));
    QCOMPARE(toText(readFile(root_ / "update" / "pack" / "x.dll")), std::string("dll"));
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(rollbackTransaction(store_, history.front().id));
    QVERIFY(!std::filesystem::exists(root_ / "update" / "readme.txt"));
    QVERIFY(!std::filesystem::exists(root_ / "update" / "pack"));
}

void TestGameEdit::replacesLooseFileAndRollsBack()
{
    const auto original = readFile(root_ / "GTA5.exe");
    const auto replaced = editor().replace("gta5.exe", source("new.exe", "replacement"));
    QVERIFY2(replaced, replaced ? "" : replaced.error().message.c_str());
    QCOMPARE(toText(readFile(root_ / "GTA5.exe")), std::string("replacement"));
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QCOMPARE(history.front().description, std::string("Replace GTA5.exe"));
    QVERIFY(rollbackTransaction(store_, history.front().id));
    QCOMPARE(readFile(root_ / "GTA5.exe"), original);
}

void TestGameEdit::replacesArchiveEntry()
{
    const auto replaced = editor().replace("common.rpf/data/a.txt", source("a.txt", "changed"));
    QVERIFY2(replaced, replaced ? "" : replaced.error().message.c_str());
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "data/a.txt"), std::string("changed"));
    QVERIFY(!editor().replace("common.rpf/data", source("b.txt", "x")));
}

void TestGameEdit::removesSeveralEntries()
{
    const std::vector<std::string> paths { "common.rpf/data/a.txt", "common.rpf/data/b.bin" };
    const auto removed = editor().remove(paths);
    QVERIFY2(removed, removed ? "" : removed.error().message.c_str());
    GameBrowser browser(root_, nullptr);
    const auto nodes = browser.list("common.rpf/data");
    QVERIFY(nodes);
    QVERIFY(nodes->empty());
    QCOMPARE(listTransactions(store_).front().description, std::string("Delete 2 items from common.rpf/data"));

    const std::vector<std::string> loose { "GTA5.exe" };
    QVERIFY(editor().remove(loose));
    QVERIFY(!std::filesystem::exists(root_ / "GTA5.exe"));

    const std::vector<std::string> mixed { "common.rpf/x.ytd", "update/update.rpf" };
    QVERIFY(!editor().remove(mixed));
}

void TestGameEdit::diskFolderRemoveIsRefused()
{
    const std::vector<std::string> paths { "update" };
    QVERIFY(!editor().remove(paths));
    QVERIFY(std::filesystem::exists(root_ / "update" / "update.rpf"));
    QVERIFY(listTransactions(store_).empty());
}

void TestGameEdit::makesFolderOnDiskAndInArchive()
{
    QVERIFY(editor().makeFolder("", "mods"));
    QVERIFY(std::filesystem::is_directory(root_ / "mods"));
    QVERIFY(!editor().makeFolder("", "MODS"));
    QVERIFY(!editor().makeFolder("", ".."));
    QVERIFY(editor().makeFolder("common.rpf/data", "fresh"));
    GameBrowser browser(root_, nullptr);
    QVERIFY(listed(browser, "common.rpf/data", "fresh"));
    QVERIFY(!editor().makeFolder("common.rpf", "DATA"));
}

void TestGameEdit::renamesEntry()
{
    QVERIFY(editor().rename("common.rpf/data/a.txt", "renamed.txt"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "data/renamed.txt"), std::string("hello"));
    QVERIFY(editor().rename("common.rpf/data", "folder"));
    QCOMPARE(readEntry(root_ / "common.rpf", {}, "folder/renamed.txt"), std::string("hello"));
    QVERIFY(!editor().rename("common.rpf/folder/renamed.txt", "a/b"));
}

void TestGameEdit::diskRenameIsRefused()
{
    QVERIFY(!editor().rename("GTA5.exe", "other.exe"));
    QVERIFY(!editor().rename("common.rpf", "other.rpf"));
    QVERIFY(std::filesystem::exists(root_ / "GTA5.exe"));
    QVERIFY(listTransactions(store_).empty());
}

void TestGameEdit::rebuildsContainingArchive()
{
    const std::vector<std::filesystem::path> files { source("big.txt", std::string(200000, 'q')) };
    QVERIFY(editor().import("common.rpf/inner.rpf", files));
    const std::vector<std::string> paths { "common.rpf/inner.rpf/big.txt" };
    QVERIFY(editor().remove(paths));
    const auto edited = std::filesystem::file_size(root_ / "common.rpf");
    const auto rebuilt = editor().rebuild("common.rpf/inner.rpf");
    QVERIFY2(rebuilt, rebuilt ? "" : rebuilt.error().message.c_str());
    QVERIFY(std::filesystem::file_size(root_ / "common.rpf") < edited);
    QCOMPARE(readEntry(root_ / "common.rpf", std::vector<std::string> { "inner.rpf" }, "deep/c.txt"), std::string("deep content"));
    QCOMPARE(listTransactions(store_).front().description, std::string("Rebuild common.rpf"));
    QVERIFY(!editor().rebuild("update"));
}

void TestGameEdit::copyToModsCopiesOnceAndMapsPath()
{
    const auto original = readFile(root_ / "update" / "update.rpf");
    const auto mapped = editor().copyToMods("update/update.rpf/common/data");
    QVERIFY2(mapped, mapped ? "" : mapped.error().message.c_str());
    QCOMPARE(*mapped, std::string("mods/update/update.rpf/common/data"));
    QCOMPARE(readFile(root_ / "mods" / "update" / "update.rpf"), original);
    QCOMPARE(readFile(root_ / "update" / "update.rpf"), original);
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));
    QCOMPARE(listTransactions(store_).front().description, std::string("Copy update/update.rpf to mods"));

    const auto again = editor().copyToMods("update/update.rpf/common/data");
    QVERIFY(again);
    QCOMPARE(*again, std::string("mods/update/update.rpf/common/data"));
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));

    const auto inMods = editor().copyToMods("mods/update/update.rpf");
    QVERIFY(inMods);
    QCOMPARE(*inMods, std::string("mods/update/update.rpf"));
    QVERIFY(!editor().copyToMods("update"));
}

void TestGameEdit::modsFolderIsMatchedWithoutCase()
{
    std::filesystem::create_directories(root_ / "Mods");
    const auto mapped = editor().copyToMods("common.rpf/data/a.txt");
    QVERIFY2(mapped, mapped ? "" : mapped.error().message.c_str());
    QCOMPARE(*mapped, std::string("Mods/common.rpf/data/a.txt"));
    QVERIFY(std::filesystem::exists(root_ / "Mods" / "common.rpf"));
    QVERIFY(!std::filesystem::exists(root_ / "mods"));
}

void TestGameEdit::failedImportRollsBack()
{
    const auto original = readFile(root_ / "common.rpf");
    const std::vector<std::filesystem::path> files { source("ok.txt", "fine"), sources_ / "missing.txt" };
    QVERIFY(!editor().import("common.rpf/data", files));
    QCOMPARE(readFile(root_ / "common.rpf"), original);
    QVERIFY(listTransactions(store_).empty());

    const std::vector<std::filesystem::path> disk { source("ok2.txt", "fine"), sources_ / "missing.txt" };
    QVERIFY(!editor().import("update", disk));
    QVERIFY(!std::filesystem::exists(root_ / "update" / "ok2.txt"));
    QVERIFY(listTransactions(store_).empty());
}

void TestGameEdit::everyOperationIsOneHistoryEntry()
{
    const std::vector<std::filesystem::path> files { source("one.txt", "1"), source("two.txt", "2"), source("three.txt", "3") };
    QVERIFY(editor().import("common.rpf/data", files));
    QVERIFY(editor().replace("common.rpf/data/one.txt", source("uno.txt", "uno")));
    const std::vector<std::string> paths { "common.rpf/data/two.txt" };
    QVERIFY(editor().remove(paths));
    QVERIFY(editor().makeFolder("common.rpf", "made"));
    QVERIFY(editor().rename("common.rpf/data/three.txt", "tres.txt"));
    QVERIFY(editor().rebuild("common.rpf"));
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(6));
    QVERIFY(std::ranges::all_of(history, &TransactionRecord::committed));
    QCOMPARE(history.back().description, std::string("Add 3 files to common.rpf/data"));
    QCOMPARE(history.front().description, std::string("Rebuild common.rpf"));
}

QTEST_APPLESS_MAIN(TestGameEdit)
#include "tst_game_edit.moc"
