#include "core/game/game_browser.h"
#include "support/browse_fixture.h"
#include "support/rpf_builder.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <algorithm>
#include <fstream>

using namespace archon;
using namespace archon::test;

namespace {

std::string contentOf(const std::filesystem::path &file)
{
    std::ifstream stream(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

const BrowserNode *named(const std::vector<BrowserNode> &nodes, std::string_view name)
{
    const auto found = std::ranges::find(nodes, name, &BrowserNode::name);
    return found == nodes.end() ? nullptr : &*found;
}

}

class TestGameBrowser : public QObject {
    Q_OBJECT
private slots:
    void init();
    void listsRoot();
    void listsArchiveAndNested();
    void resolvesCaseInsensitively();
    void missingPathIsError();
    void pathCannotLeaveRoot();
    void brokenArchiveIsError();
    void extractsFileFolderAndNestedArchive();
    void copiesDiskFileAndRefusesDiskFolder();
    void hostileNameIsListedButNotWritten();

private:
    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path output_;
};

void TestGameBrowser::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    output_ = pathOf(*directory_) / "out";
    writeBrowseFixture(root_);
}

void TestGameBrowser::listsRoot()
{
    GameBrowser browser(root_, nullptr);
    const auto nodes = browser.list("");
    QVERIFY2(nodes, nodes ? "" : nodes.error().message.c_str());
    QCOMPARE(nodes->size(), std::size_t { 3 });
    QCOMPARE(named(*nodes, "update")->kind, NodeKind::Folder);
    QCOMPARE(named(*nodes, "update")->size, std::uint64_t { 1 });
    QCOMPARE(named(*nodes, "common.rpf")->kind, NodeKind::Archive);
    QCOMPARE(named(*nodes, "GTA5.exe")->kind, NodeKind::File);
    QCOMPARE(named(*nodes, "GTA5.exe")->size, std::uint64_t { 21 });
    QVERIFY(!named(*nodes, "GTA5.exe")->inArchive);
}

void TestGameBrowser::listsArchiveAndNested()
{
    GameBrowser browser(root_, nullptr);
    const auto nodes = browser.list("common.rpf");
    QVERIFY2(nodes, nodes ? "" : nodes.error().message.c_str());
    QCOMPARE(nodes->size(), std::size_t { 3 });
    QCOMPARE(named(*nodes, "data")->kind, NodeKind::Folder);
    QCOMPARE(named(*nodes, "data")->size, std::uint64_t { 2 });
    QCOMPARE(named(*nodes, "inner.rpf")->kind, NodeKind::Archive);
    QVERIFY(named(*nodes, "x.ytd")->resource);
    QVERIFY(named(*nodes, "x.ytd")->inArchive);

    const auto data = browser.list("common.rpf/data");
    QVERIFY(data);
    QCOMPARE(named(*data, "b.bin")->size, std::uint64_t { 5000 });
    QVERIFY(named(*data, "b.bin")->compressed);
    QVERIFY(!named(*data, "a.txt")->compressed);

    const auto deep = browser.list("common.rpf/inner.rpf/deep");
    QVERIFY2(deep, deep ? "" : deep.error().message.c_str());
    QCOMPARE(deep->size(), std::size_t { 1 });
    QCOMPARE(deep->front().name, std::string("c.txt"));
}

void TestGameBrowser::resolvesCaseInsensitively()
{
    GameBrowser browser(root_, nullptr);
    QCOMPARE(browser.canonicalPath("COMMON.RPF/DATA").value(), std::string("common.rpf/data"));
    QCOMPARE(browser.canonicalPath("Update/UPDATE.rpf").value(), std::string("update/update.rpf"));
    QCOMPARE(browser.canonicalPath("common.rpf/INNER.RPF/Deep/C.TXT").value(), std::string("common.rpf/inner.rpf/deep/c.txt"));
    QCOMPARE(browser.canonicalPath("").value(), std::string());
    QVERIFY(browser.list("COMMON.RPF/DATA"));
}

void TestGameBrowser::missingPathIsError()
{
    GameBrowser browser(root_, nullptr);
    QVERIFY(!browser.list("nothing"));
    QVERIFY(!browser.list("common.rpf/nothing"));
    QVERIFY(!browser.list("GTA5.exe"));
    QVERIFY(!browser.list("common.rpf/data/a.txt"));
    QVERIFY(!browser.canonicalPath("common.rpf/data/missing.txt"));
}

void TestGameBrowser::pathCannotLeaveRoot()
{
    writeFile(pathOf(*directory_) / "secret.txt", "secret");
    GameBrowser browser(root_, nullptr);
    QVERIFY(!browser.list(".."));
    QVERIFY(!browser.list("update/../.."));
    QVERIFY(!browser.canonicalPath("../secret.txt"));
    QVERIFY(!browser.extract("../secret.txt", output_));
    QVERIFY(!std::filesystem::exists(output_ / "secret.txt"));
}

void TestGameBrowser::brokenArchiveIsError()
{
    writeFile(root_ / "bad.rpf", "0123456789");
    GameBrowser browser(root_, nullptr);
    QCOMPARE(named(browser.list("").value(), "bad.rpf")->kind, NodeKind::Archive);
    const auto nodes = browser.list("bad.rpf");
    QVERIFY(!nodes);
    QVERIFY(!nodes.error().message.empty());
}

void TestGameBrowser::extractsFileFolderAndNestedArchive()
{
    GameBrowser browser(root_, nullptr);
    const auto file = browser.extract("common.rpf/data/a.txt", output_);
    QVERIFY2(file, file ? "" : file.error().message.c_str());
    QCOMPARE(file->files, std::uint64_t { 1 });
    QCOMPARE(contentOf(output_ / "a.txt"), std::string("hello"));

    const auto folder = browser.extract("common.rpf/data", output_);
    QVERIFY(folder);
    QCOMPARE(folder->files, std::uint64_t { 2 });
    QVERIFY(folder->failures.empty());
    QCOMPARE(contentOf(output_ / "data" / "a.txt"), std::string("hello"));
    QCOMPARE(contentOf(output_ / "data" / "b.bin"), std::string(5000, 'z'));

    const auto nested = browser.extract("common.rpf/inner.rpf", output_);
    QVERIFY(nested);
    QCOMPARE(contentOf(output_ / "inner.rpf").substr(0, 4), std::string("7FPR"));

    QVERIFY(!browser.extract("", output_));
    QVERIFY(!browser.extract("common.rpf/data/a.txt", output_));
}

void TestGameBrowser::copiesDiskFileAndRefusesDiskFolder()
{
    GameBrowser browser(root_, nullptr);
    const auto copied = browser.extract("GTA5.exe", output_);
    QVERIFY2(copied, copied ? "" : copied.error().message.c_str());
    QCOMPARE(contentOf(output_ / "GTA5.exe"), std::string("not a real executable"));
    QVERIFY(!browser.extract("GTA5.exe", output_));
    QVERIFY(!browser.extract("update", output_));
}

void TestGameBrowser::hostileNameIsListedButNotWritten()
{
    RpfBuilder builder;
    builder.addBinary("ab/x.txt", toVector("payload"), false);
    builder.addBinary("ok.txt", toVector("fine"), false);
    auto bytes = builder.build();
    const auto name = asBytes(std::string_view("ab\0", 3));
    const auto found = std::ranges::search(bytes, name);
    QVERIFY(!found.empty());
    std::ranges::copy(asBytes(".."), found.begin());
    writeFile(root_ / "evil.rpf", bytes);

    GameBrowser browser(root_, nullptr);
    const auto nodes = browser.list("evil.rpf");
    QVERIFY2(nodes, nodes ? "" : nodes.error().message.c_str());
    QVERIFY(named(*nodes, "..") != nullptr);
    QVERIFY(!browser.extract("evil.rpf/..", output_ / "sub"));
    QVERIFY(!std::filesystem::exists(output_ / "x.txt"));
    QVERIFY(!std::filesystem::exists(output_ / "sub" / "x.txt"));
}

QTEST_GUILESS_MAIN(TestGameBrowser)
#include "tst_game_browser.moc"
