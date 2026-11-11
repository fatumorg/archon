#pragma once

#include "core/io/error.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace archon {

class Transaction {
public:
    static Result<Transaction> begin(const std::filesystem::path &store, const std::filesystem::path &gameRoot, std::string description);

    Transaction(Transaction &&) noexcept;
    Transaction &operator=(Transaction &&) = delete;
    ~Transaction();

    Result<void> writeAt(const std::filesystem::path &file, std::uint64_t offset, std::span<const std::byte> data);
    Result<std::uint64_t> sizeOf(const std::filesystem::path &file);
    Result<void> replaceFile(const std::filesystem::path &file, const std::function<Result<void>(const std::filesystem::path &temporary)> &produce);
    Result<void> removeFile(const std::filesystem::path &file);
    Result<void> createDirectories(const std::filesystem::path &directory);
    Result<void> commit();
    Result<void> rollback();

private:
    struct State;
    explicit Transaction(std::unique_ptr<State> state);

    std::unique_ptr<State> state_;
};

struct TransactionRecord {
    std::string id;
    std::string description;
    std::int64_t time;
    std::vector<std::filesystem::path> files;
    bool committed;
};

std::vector<TransactionRecord> listTransactions(const std::filesystem::path &store);
Result<void> rollbackTransaction(const std::filesystem::path &store, const std::string &id);
std::filesystem::path safetyStore(const std::filesystem::path &dataDirectory, const std::filesystem::path &gameRoot);

}
