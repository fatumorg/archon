#include "core/safety/transaction.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QTest>

#include <memory>

using namespace archon;
using namespace archon::test;

namespace {

std::vector<std::byte> pattern(std::size_t count, unsigned seed)
{
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i)
        bytes[i] = static_cast<std::byte>((i * 31 + seed) & 0xFF);
    return bytes;
}

bool isEmptyDirectory(const std::filesystem::path &directory)
{
    std::error_code error;
    return !std::filesystem::exists(directory, error) || std::filesystem::is_empty(directory, error);
}

Result<void> writeNew(const std::filesystem::path &temporary)
{
    writeFile(temporary, "new content");
    return {};
}

}

class TestTransaction : public QObject {
    Q_OBJECT
private slots:
    void init();
    void overwriteAndAppendRollBackByteIdentical();
    void commitKeepsChangesAndLaterRollbackRestores();
    void destructorRollsBack();
    void replaceFileBacksUpAndRestores();
    void replaceFileCreatesAndRollbackRemoves();
    void removeFileRestores();
    void createdDirectoriesAreRemovedOnRollback();
    void failedProduceLeavesOriginal();
    void crashLeavesUnfinishedJournalThatRestores();
    void tornLastRecordIsIgnored();
    void rollbackOfOlderRollsBackNewerFirst();
    void rollbackOfNewestKeepsOlderRollable();
    void outsideChangeRefusesRollback();
    void pathOutsideRootIsRefused();
    void emptyTransactionLeavesNoHistory();

private:
    Result<Transaction> begin(const char *description = "test change")
    {
        return Transaction::begin(store_, root_, description);
    }
    void applyTwoWrites(Transaction &transaction);

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path root_;
    std::filesystem::path store_;
    std::filesystem::path file_;
    std::vector<std::byte> original_;
};

void TestTransaction::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    root_ = pathOf(*directory_) / "game";
    store_ = pathOf(*directory_) / "store";
    file_ = root_ / "data.bin";
    original_ = pattern(4096, 1);
    writeFile(file_, original_);
}

void TestTransaction::applyTwoWrites(Transaction &transaction)
{
    QVERIFY(transaction.writeAt(file_, 100, pattern(50, 9)));
    QVERIFY(transaction.writeAt(file_, 4096, pattern(1000, 5)));
    const auto size = transaction.sizeOf(file_);
    QVERIFY(size);
    QCOMPARE(*size, std::uint64_t(5096));
}

void TestTransaction::overwriteAndAppendRollBackByteIdentical()
{
    auto transaction = begin();
    QVERIFY(transaction);
    applyTwoWrites(*transaction);
    QVERIFY(readFile(file_) != original_);
    QVERIFY(transaction->rollback());
    QCOMPARE(readFile(file_), original_);
    QVERIFY(isEmptyDirectory(store_));
}

void TestTransaction::commitKeepsChangesAndLaterRollbackRestores()
{
    {
        auto transaction = begin("Add a file");
        QVERIFY(transaction);
        applyTwoWrites(*transaction);
        QVERIFY(transaction->commit());
    }
    const auto changed = readFile(file_);
    QCOMPARE(changed.size(), std::size_t(5096));
    QCOMPARE(std::vector<std::byte>(changed.begin() + 100, changed.begin() + 150), pattern(50, 9));

    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(history[0].committed);
    QCOMPARE(history[0].description, std::string("Add a file"));
    QCOMPARE(history[0].files, std::vector<std::filesystem::path> { file_ });
    QVERIFY(history[0].time > 0);

    QVERIFY(rollbackTransaction(store_, history[0].id));
    QCOMPARE(readFile(file_), original_);
    QVERIFY(listTransactions(store_).empty());
}

void TestTransaction::destructorRollsBack()
{
    {
        auto transaction = begin();
        QVERIFY(transaction);
        applyTwoWrites(*transaction);
    }
    QCOMPARE(readFile(file_), original_);
    QVERIFY(listTransactions(store_).empty());
}

void TestTransaction::replaceFileBacksUpAndRestores()
{
    const std::filesystem::path library = root_ / "a.dll";
    writeFile(library, "old content");
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(transaction->replaceFile(library, writeNew));
    QCOMPARE(toText(readFile(library)), std::string("new content"));
    QVERIFY(!std::filesystem::exists(root_ / "a.dll.archon-tmp"));
    QVERIFY(transaction->commit());
    QCOMPARE(toText(readFile(library)), std::string("new content"));

    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(rollbackTransaction(store_, history[0].id));
    QCOMPARE(toText(readFile(library)), std::string("old content"));
}

void TestTransaction::replaceFileCreatesAndRollbackRemoves()
{
    const std::filesystem::path created = root_ / "fresh.asi";
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(transaction->replaceFile(created, writeNew));
    QVERIFY(std::filesystem::exists(created));
    QVERIFY(transaction->rollback());
    QVERIFY(!std::filesystem::exists(created));
}

void TestTransaction::removeFileRestores()
{
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(transaction->removeFile(file_));
    QVERIFY(!std::filesystem::exists(file_));
    QVERIFY(transaction->commit());
    QVERIFY(!std::filesystem::exists(file_));
    QVERIFY(rollbackTransaction(store_, listTransactions(store_).at(0).id));
    QCOMPARE(readFile(file_), original_);
}

void TestTransaction::createdDirectoriesAreRemovedOnRollback()
{
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(transaction->createDirectories(root_ / "mods" / "update"));
    QVERIFY(std::filesystem::is_directory(root_ / "mods" / "update"));
    QVERIFY(transaction->replaceFile(root_ / "mods" / "update" / "x.txt", writeNew));
    QVERIFY(transaction->rollback());
    QVERIFY(!std::filesystem::exists(root_ / "mods"));
}

void TestTransaction::failedProduceLeavesOriginal()
{
    auto transaction = begin();
    QVERIFY(transaction);
    const auto result = transaction->replaceFile(file_, [](const std::filesystem::path &temporary) -> Result<void> {
        writeFile(temporary, "half");
        return std::unexpected(Error { "test", 0, "producer failed" });
    });
    QVERIFY(!result);
    QCOMPARE(result.error().message, std::string("producer failed"));
    QCOMPARE(readFile(file_), original_);
    QVERIFY(!std::filesystem::exists(root_ / "data.bin.archon-tmp"));
}

void TestTransaction::crashLeavesUnfinishedJournalThatRestores()
{
    auto transaction = begin();
    QVERIFY(transaction);
    applyTwoWrites(*transaction);
    [[maybe_unused]] auto *abandoned = new Transaction(std::move(*transaction));

    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(!history[0].committed);
    QVERIFY(rollbackTransaction(store_, history[0].id));
    QCOMPARE(readFile(file_), original_);
    QVERIFY(listTransactions(store_).empty());
}

void TestTransaction::tornLastRecordIsIgnored()
{
    auto transaction = begin();
    QVERIFY(transaction);
    applyTwoWrites(*transaction);
    [[maybe_unused]] auto *abandoned = new Transaction(std::move(*transaction));

    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    std::ofstream(store_ / history[0].id / "journal", std::ios::binary | std::ios::app).write("\x03garbage", 7);
    QVERIFY(rollbackTransaction(store_, history[0].id));
    QCOMPARE(readFile(file_), original_);
}

void TestTransaction::rollbackOfOlderRollsBackNewerFirst()
{
    for (unsigned round = 0; round < 2; ++round) {
        auto transaction = begin();
        QVERIFY(transaction);
        QVERIFY(transaction->writeAt(file_, 10 + round * 5, pattern(40, 50 + round)));
        QVERIFY(transaction->writeAt(file_, 4096 + round * 100, pattern(100, round)));
        QVERIFY(transaction->commit());
    }
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(2));
    QVERIFY(history[0].id > history[1].id);
    QVERIFY(rollbackTransaction(store_, history[1].id));
    QCOMPARE(readFile(file_), original_);
    QVERIFY(listTransactions(store_).empty());
}

void TestTransaction::rollbackOfNewestKeepsOlderRollable()
{
    for (unsigned round = 0; round < 2; ++round) {
        auto transaction = begin();
        QVERIFY(transaction);
        QVERIFY(transaction->writeAt(file_, 10 + round * 5, pattern(40, 50 + round)));
        QVERIFY(transaction->commit());
    }
    QVERIFY(rollbackTransaction(store_, listTransactions(store_).at(0).id));
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));
    QVERIFY(rollbackTransaction(store_, listTransactions(store_).at(0).id));
    QCOMPARE(readFile(file_), original_);
}

void TestTransaction::outsideChangeRefusesRollback()
{
    {
        auto transaction = begin();
        QVERIFY(transaction);
        applyTwoWrites(*transaction);
        QVERIFY(transaction->commit());
    }
    writeFile(file_, "replaced by something else");
    const auto history = listTransactions(store_);
    QCOMPARE(history.size(), std::size_t(1));
    QVERIFY(!rollbackTransaction(store_, history[0].id));
    QCOMPARE(toText(readFile(file_)), std::string("replaced by something else"));
    QCOMPARE(listTransactions(store_).size(), std::size_t(1));
}

void TestTransaction::pathOutsideRootIsRefused()
{
    const std::filesystem::path outside = pathOf(*directory_) / "outside.bin";
    writeFile(outside, "keep");
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(!transaction->writeAt(root_ / ".." / "outside.bin", 0, pattern(4, 0)));
    QVERIFY(!transaction->writeAt(outside, 0, pattern(4, 0)));
    QVERIFY(!transaction->replaceFile(outside, writeNew));
    QVERIFY(!transaction->removeFile(outside));
    QVERIFY(!transaction->createDirectories(pathOf(*directory_) / "elsewhere"));
    QCOMPARE(toText(readFile(outside)), std::string("keep"));
    QVERIFY(!std::filesystem::exists(pathOf(*directory_) / "elsewhere"));
}

void TestTransaction::emptyTransactionLeavesNoHistory()
{
    auto transaction = begin();
    QVERIFY(transaction);
    QVERIFY(transaction->commit());
    QVERIFY(listTransactions(store_).empty());
}

QTEST_APPLESS_MAIN(TestTransaction)
#include "tst_transaction.moc"
