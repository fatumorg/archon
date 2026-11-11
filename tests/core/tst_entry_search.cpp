#include "core/search/entry_search.h"
#include "support/browse_fixture.h"
#include "support/temp_files.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

class TestEntrySearch : public QObject {
    Q_OBJECT
private slots:
    void init();
    void matchesPatternCases_data();
    void matchesPatternCases();
    void findsInNestedArchives();
    void searchesBelowStartPathOnly();
    void skipsBrokenArchive();
    void stopsWhenCancelled();
    void truncatesAtLimit();

private:
    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::atomic<bool> running_ { false };
};

void TestEntrySearch::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_);
    writeBrowseFixture(root_);
}

void TestEntrySearch::matchesPatternCases_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("expected");
    QTest::newRow("substring") << "a.txt" << "data_A.TXT" << true;
    QTest::newRow("substring miss") << "b.txt" << "data_a.txt" << false;
    QTest::newRow("star suffix") << "*.ytd" << "x.YTD" << true;
    QTest::newRow("star is anchored") << "*.ytd" << "x.ytd.bak" << false;
    QTest::newRow("question") << "?.txt" << "a.txt" << true;
    QTest::newRow("question needs one") << "?.txt" << "ab.txt" << false;
    QTest::newRow("stars around") << "*pist*" << "w_pi_pistol.ytd" << true;
    QTest::newRow("many stars") << "a*b*c" << "aXXbYYbZc" << true;
    QTest::newRow("many stars miss") << "a*b*c" << "aXXbYY" << false;
    QTest::newRow("empty pattern") << "" << "a.txt" << false;
    QTest::newRow("only star") << "*" << "" << true;
}

void TestEntrySearch::matchesPatternCases()
{
    QFETCH(QString, pattern);
    QFETCH(QString, name);
    QFETCH(bool, expected);
    QCOMPARE(matchesPattern(pattern.toStdString(), name.toStdString()), expected);
}

void TestEntrySearch::findsInNestedArchives()
{
    GameBrowser browser(root_, nullptr);
    const auto result = searchEntries(browser, "", "c.txt", running_);
    QCOMPARE(result.hits.size(), std::size_t { 1 });
    QCOMPARE(result.hits.front().path, std::string("common.rpf/inner.rpf/deep/c.txt"));
    QCOMPARE(result.hits.front().node.name, std::string("c.txt"));
    QCOMPARE(result.skipped, std::uint64_t { 0 });
    QVERIFY(!result.truncated);

    const auto folders = searchEntries(browser, "", "data", running_);
    QCOMPARE(folders.hits.size(), std::size_t { 2 });
}

void TestEntrySearch::searchesBelowStartPathOnly()
{
    GameBrowser browser(root_, nullptr);
    QCOMPARE(searchEntries(browser, "update", "*.txt", running_).hits.size(), std::size_t { 0 });
    QCOMPARE(searchEntries(browser, "common.rpf/data", "*.txt", running_).hits.size(), std::size_t { 1 });
    QCOMPARE(searchEntries(browser, "", "*.txt", running_).hits.size(), std::size_t { 2 });
}

void TestEntrySearch::skipsBrokenArchive()
{
    writeFile(root_ / "bad.rpf", "0123456789");
    GameBrowser browser(root_, nullptr);
    const auto result = searchEntries(browser, "", "*.txt", running_);
    QCOMPARE(result.skipped, std::uint64_t { 1 });
    QCOMPARE(result.hits.size(), std::size_t { 2 });
}

void TestEntrySearch::stopsWhenCancelled()
{
    GameBrowser browser(root_, nullptr);
    const std::atomic<bool> cancelled { true };
    QVERIFY(searchEntries(browser, "", "*", cancelled).hits.empty());
}

void TestEntrySearch::truncatesAtLimit()
{
    GameBrowser browser(root_, nullptr);
    const auto result = searchEntries(browser, "", "*", running_, 2);
    QCOMPARE(result.hits.size(), std::size_t { 2 });
    QVERIFY(result.truncated);
}

QTEST_GUILESS_MAIN(TestEntrySearch)
#include "tst_entry_search.moc"
