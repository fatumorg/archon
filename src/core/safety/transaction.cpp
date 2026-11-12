#include "core/safety/transaction.h"

#include "core/crypto/hash.h"
#include "core/io/byte_reader.h"
#include "core/io/byte_writer.h"
#include "core/io/writable_file.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>

#include <algorithm>
#include <map>
#include <optional>

namespace archon {

namespace {

constexpr char format[] = "journal";
constexpr char temporarySuffix[] = ".archon-tmp";
constexpr char backupPrefix[] = "backup-";
constexpr std::size_t maxPreimage = 16u << 20;
constexpr std::size_t maxRecord = maxPreimage + (64u << 10);
constexpr std::size_t recordOverhead = 9;
constexpr std::size_t preimageHeader = 12;

enum class Kind : std::uint8_t { Begin = 1, Touch, Preimage, Backup, Directory, Commit, RollingBack };

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

struct Step {
    Kind kind;
    std::filesystem::path path;
    bool existed = false;
    std::uint64_t length = 0;
    std::int64_t modified = 0;
    std::uint32_t touchIndex = 0;
    std::uint64_t offset = 0;
    std::uint64_t payloadOffset = 0;
    std::uint32_t payloadSize = 0;
    std::string backupName;
};

struct Final {
    std::filesystem::path path;
    bool exists;
    std::uint64_t length;
    std::int64_t modified;
};

struct Journal {
    std::filesystem::path root;
    std::string description;
    std::int64_t time = 0;
    bool begun = false;
    bool committed = false;
    bool rollingBack = false;
    std::uint64_t validSize = 0;
    std::vector<Step> steps;
    std::vector<Final> finals;
};

std::int64_t modifiedOf(const std::filesystem::path &file)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    return error ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
}

void setModified(const std::filesystem::path &file, std::int64_t modified)
{
    std::error_code error;
    using Time = std::filesystem::file_time_type;
    std::filesystem::last_write_time(file, Time(Time::duration(modified)), error);
}

bool isRegularFile(const std::filesystem::path &file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(file, error));
}

bool isInside(const std::filesystem::path &root, const std::filesystem::path &file)
{
    if (!file.is_absolute())
        return false;
    const std::filesystem::path relative = file.lexically_normal().lexically_relative(root);
    return !relative.empty() && relative.native() != "." && *relative.begin() != "..";
}

std::filesystem::path temporaryOf(const std::filesystem::path &file)
{
    return std::filesystem::path(file.native() + temporarySuffix);
}

Result<void> syncFile(const std::filesystem::path &file)
{
    auto opened = WritableFile::open(file, false);
    if (!opened)
        return std::unexpected(opened.error());
    return opened->sync();
}

Result<void> syncParents(const std::filesystem::path &first, const std::filesystem::path &second)
{
    if (const auto synced = WritableFile::syncDirectory(first.parent_path()); !synced)
        return synced;
    if (second.parent_path() == first.parent_path())
        return {};
    return WritableFile::syncDirectory(second.parent_path());
}

Result<void> appendRecord(WritableFile &file, std::uint64_t &size, Kind kind, std::span<const std::byte> payload)
{
    std::vector<std::byte> record;
    record.reserve(payload.size() + recordOverhead);
    putInteger(record, static_cast<std::uint8_t>(kind), 1);
    putInteger(record, payload.size(), 4);
    putBytes(record, payload);
    putInteger(record, jenkinsHash(payload), 4);
    if (const auto written = file.write(size, record); !written)
        return written;
    size += record.size();
    return file.sync();
}

Result<void> copyThenRename(const std::filesystem::path &from, const std::filesystem::path &partial, const std::filesystem::path &to)
{
    std::error_code error;
    std::filesystem::remove(partial, error);
    if (!std::filesystem::copy_file(from, partial, error) || error)
        return failure("cannot copy " + from.string());
    setModified(partial, modifiedOf(from));
    if (const auto synced = syncFile(partial); !synced)
        return synced;
    std::filesystem::rename(partial, to, error);
    if (error)
        return failure("cannot write " + to.string());
    if (const auto synced = WritableFile::syncDirectory(to.parent_path()); !synced)
        return synced;
    std::filesystem::remove(from, error);
    return {};
}

Result<void> moveFile(const std::filesystem::path &from, const std::filesystem::path &partial, const std::filesystem::path &to)
{
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (!error)
        return syncParents(from, to);
    return copyThenRename(from, partial, to);
}

Result<std::string> readString(ByteReader &reader)
{
    const auto length = reader.u32();
    if (!length)
        return std::unexpected(length.error());
    const auto bytes = reader.bytes(*length);
    if (!bytes)
        return std::unexpected(bytes.error());
    return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
}

bool decode(Journal &journal, Kind kind, std::span<const std::byte> payload, std::uint64_t payloadOffset)
{
    ByteReader reader(payload, format);
    Step step { kind, {}, false, 0, 0, 0, 0, 0, 0, {} };
    switch (kind) {
    case Kind::Begin: {
        const auto root = readString(reader);
        const auto description = readString(reader);
        const auto time = reader.u64();
        if (!root || !description || !time)
            return false;
        journal.root = *root;
        journal.description = *description;
        journal.time = static_cast<std::int64_t>(*time);
        journal.begun = true;
        return true;
    }
    case Kind::Touch: {
        const auto path = readString(reader);
        const auto existed = reader.u8();
        const auto length = reader.u64();
        const auto modified = reader.u64();
        if (!path || !existed || !length || !modified)
            return false;
        step.path = *path;
        step.existed = *existed != 0;
        step.length = *length;
        step.modified = static_cast<std::int64_t>(*modified);
        break;
    }
    case Kind::Preimage: {
        const auto index = reader.u32();
        const auto offset = reader.u64();
        if (!index || !offset)
            return false;
        step.touchIndex = *index;
        step.offset = *offset;
        step.payloadOffset = payloadOffset + preimageHeader;
        step.payloadSize = static_cast<std::uint32_t>(payload.size() - preimageHeader);
        break;
    }
    case Kind::Backup: {
        const auto path = readString(reader);
        const auto name = readString(reader);
        if (!path || !name || !name->starts_with(backupPrefix) || name->find_first_of("/\\") != std::string::npos)
            return false;
        step.path = *path;
        step.backupName = *name;
        break;
    }
    case Kind::Directory: {
        const auto path = readString(reader);
        if (!path)
            return false;
        step.path = *path;
        break;
    }
    case Kind::Commit: {
        const auto time = reader.u64();
        const auto count = reader.u32();
        if (!time || !count)
            return false;
        for (std::uint32_t i = 0; i < *count; ++i) {
            const auto path = readString(reader);
            const auto exists = reader.u8();
            const auto length = reader.u64();
            const auto modified = reader.u64();
            if (!path || !exists || !length || !modified)
                return false;
            journal.finals.push_back({ *path, *exists != 0, *length, static_cast<std::int64_t>(*modified) });
        }
        journal.committed = true;
        return true;
    }
    case Kind::RollingBack:
        journal.rollingBack = true;
        return true;
    default:
        return false;
    }
    journal.steps.push_back(std::move(step));
    return true;
}

Result<Journal> readJournal(const std::filesystem::path &directory)
{
    auto file = WritableFile::open(directory / "journal", false);
    if (!file)
        return std::unexpected(file.error());
    Journal journal;
    const std::uint64_t size = file->size();
    std::uint64_t position = 0;
    while (size - position >= recordOverhead) {
        const auto head = file->read(position, 5);
        if (!head)
            break;
        ByteReader headReader(*head, format);
        const std::uint8_t kind = *headReader.u8();
        const std::uint32_t length = *headReader.u32();
        if (length > maxRecord || size - position - recordOverhead < length)
            break;
        const auto payload = file->read(position + 5, length);
        const auto checksum = file->read(position + 5 + length, 4);
        if (!payload || !checksum)
            break;
        ByteReader checksumReader(*checksum, format);
        if (*checksumReader.u32() != jenkinsHash(*payload))
            break;
        if (!decode(journal, static_cast<Kind>(kind), *payload, position + 5))
            break;
        position += recordOverhead + length;
    }
    journal.validSize = position;
    if (!journal.begun)
        return failure("journal in " + directory.string() + " is unreadable");
    return journal;
}

Result<void> restoreBackup(const std::filesystem::path &backup, const std::filesystem::path &file)
{
    std::error_code error;
    std::filesystem::remove(temporaryOf(file), error);
    if (!isRegularFile(backup))
        return {};
    std::filesystem::create_directories(file.parent_path(), error);
    return moveFile(backup, temporaryOf(file), file);
}

Result<void> undo(const std::filesystem::path &directory)
{
    const auto journal = readJournal(directory);
    if (!journal)
        return std::unexpected(journal.error());
    for (const Step &step : journal->steps) {
        if (step.kind != Kind::Preimage && !isInside(journal->root, step.path))
            return failure("journal names a file outside the game folder");
    }
    auto journalFile = WritableFile::open(directory / "journal", false);
    if (!journalFile)
        return std::unexpected(journalFile.error());
    if (!journal->rollingBack) {
        std::uint64_t size = journal->validSize;
        if (const auto marked = appendRecord(*journalFile, size, Kind::RollingBack, {}); !marked)
            return marked;
    }

    std::vector<const Step *> touches;
    for (const Step &step : journal->steps) {
        if (step.kind == Kind::Touch && step.existed)
            touches.push_back(&step);
    }
    std::map<std::uint32_t, WritableFile> handles;
    std::error_code error;
    for (auto step = journal->steps.rbegin(); step != journal->steps.rend(); ++step) {
        switch (step->kind) {
        case Kind::Preimage: {
            if (step->touchIndex >= touches.size())
                return failure("journal preimage has no file");
            auto handle = handles.find(step->touchIndex);
            if (handle == handles.end()) {
                auto opened = WritableFile::open(touches[step->touchIndex]->path, false);
                if (!opened)
                    return std::unexpected(opened.error());
                handle = handles.emplace(step->touchIndex, std::move(*opened)).first;
            }
            const auto bytes = journalFile->read(step->payloadOffset, step->payloadSize);
            if (!bytes)
                return std::unexpected(bytes.error());
            if (const auto written = handle->second.write(step->offset, *bytes); !written)
                return written;
            break;
        }
        case Kind::Touch: {
            const auto index = static_cast<std::uint32_t>(std::ranges::find(touches, &*step) - touches.begin());
            handles.erase(index);
            if (!step->existed) {
                std::filesystem::remove(temporaryOf(step->path), error);
                std::filesystem::remove(step->path, error);
                break;
            }
            auto opened = WritableFile::open(step->path, false);
            if (!opened)
                return std::unexpected(opened.error());
            if (const auto resized = opened->resize(step->length); !resized)
                return resized;
            if (const auto synced = opened->sync(); !synced)
                return synced;
            setModified(step->path, step->modified);
            break;
        }
        case Kind::Backup:
            if (const auto restored = restoreBackup(directory / step->backupName, step->path); !restored)
                return restored;
            break;
        case Kind::Directory:
            std::filesystem::remove(step->path, error);
            break;
        default:
            break;
        }
    }
    handles.clear();
    std::filesystem::remove_all(directory, error);
    return {};
}

bool matchesFinal(const Final &final)
{
    std::error_code error;
    if (!isRegularFile(final.path))
        return !final.exists;
    return final.exists && std::filesystem::file_size(final.path, error) == final.length && modifiedOf(final.path) == final.modified;
}

std::array<std::byte, 32> fileSha256(const std::filesystem::path &file)
{
    std::array<std::byte, 32> digest {};
    QFile opened(file);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (opened.open(QIODevice::ReadOnly) && hash.addData(&opened)) {
        const QByteArray result = hash.result();
        std::ranges::copy(std::as_bytes(std::span(result.constData(), digest.size())), digest.begin());
    }
    return digest;
}

}

struct Transaction::State {
    struct Touched {
        std::filesystem::path file;
        WritableFile handle;
        std::uint64_t originalLength;
    };

    std::filesystem::path directory;
    std::filesystem::path root;
    std::optional<WritableFile> journal;
    std::uint64_t journalSize = 0;
    std::vector<Touched> touched;
    std::vector<std::filesystem::path> whole;
    std::map<std::filesystem::path, WritableFile> wholeHandles;
    std::uint32_t backups = 0;
    bool changed = false;
    bool finished = false;

    Result<void> append(Kind kind, const std::vector<std::byte> &payload)
    {
        changed = true;
        return appendRecord(*journal, journalSize, kind, payload);
    }

    Result<std::filesystem::path> checked(const std::filesystem::path &file) const
    {
        if (finished)
            return failure("the transaction is already finished");
        if (!isInside(root, file))
            return failure(file.string() + " is outside the game folder");
        return file.lexically_normal();
    }

    bool isWhole(const std::filesystem::path &file) const
    {
        return std::ranges::find(whole, file) != whole.end();
    }

    Touched *findTouched(const std::filesystem::path &file)
    {
        const auto found = std::ranges::find(touched, file, &Touched::file);
        return found == touched.end() ? nullptr : &*found;
    }

    Result<WritableFile *> wholeHandle(const std::filesystem::path &path)
    {
        if (const auto found = wholeHandles.find(path); found != wholeHandles.end())
            return &found->second;
        auto handle = WritableFile::open(path, false);
        if (!handle)
            return std::unexpected(handle.error());
        return &wholeHandles.emplace(path, std::move(*handle)).first->second;
    }

    Result<Touched *> touch(const std::filesystem::path &path)
    {
        if (isWhole(path))
            return failure(path.string() + " was already replaced in this transaction");
        if (Touched *known = findTouched(path))
            return known;
        if (!isRegularFile(path))
            return failure(path.string() + " is not a file");
        auto handle = WritableFile::open(path, false);
        if (!handle)
            return std::unexpected(handle.error());
        const std::uint64_t length = handle->size();
        std::vector<std::byte> payload;
        putString(payload, path.string());
        putInteger(payload, 1, 1);
        putInteger(payload, length, 8);
        putInteger(payload, static_cast<std::uint64_t>(modifiedOf(path)), 8);
        if (const auto recorded = append(Kind::Touch, payload); !recorded)
            return std::unexpected(recorded.error());
        touched.push_back({ path, std::move(*handle), length });
        return &touched.back();
    }

    Result<void> backUp(const std::filesystem::path &file)
    {
        const std::string name = backupPrefix + std::to_string(backups++);
        std::error_code error;
        std::vector<std::byte> payload;
        putString(payload, file.string());
        putString(payload, name);
        putInteger(payload, std::filesystem::file_size(file, error), 8);
        putBytes(payload, fileSha256(file));
        putInteger(payload, static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()), 8);
        if (const auto recorded = append(Kind::Backup, payload); !recorded)
            return recorded;
        return moveFile(file, directory / (name + ".part"), directory / name);
    }
};

Transaction::Transaction(std::unique_ptr<State> state)
    : state_(std::move(state))
{
}

Transaction::Transaction(Transaction &&) noexcept = default;

Transaction::~Transaction()
{
    if (state_ && !state_->finished)
        static_cast<void>(rollback());
}

Result<Transaction> Transaction::begin(const std::filesystem::path &store, const std::filesystem::path &gameRoot, std::string description)
{
    std::error_code error;
    std::filesystem::create_directories(store, error);
    if (error)
        return failure("cannot create " + store.string());
    std::uint64_t next = 1;
    for (std::filesystem::directory_iterator it(store, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.size() <= 18 && std::ranges::all_of(name, [](char c) { return c >= '0' && c <= '9'; }))
            next = std::max<std::uint64_t>(next, std::stoull(name) + 1);
    }
    auto state = std::make_unique<State>();
    state->root = std::filesystem::absolute(gameRoot).lexically_normal();
    for (;; ++next) {
        std::string id = std::to_string(next);
        id.insert(0, 8 - std::min<std::size_t>(8, id.size()), '0');
        state->directory = store / id;
        if (std::filesystem::create_directory(state->directory, error))
            break;
        if (error)
            return failure("cannot create " + state->directory.string());
    }
    auto journal = WritableFile::open(state->directory / "journal", true);
    if (!journal)
        return std::unexpected(journal.error());
    state->journal = std::move(*journal);
    std::vector<std::byte> payload;
    putString(payload, state->root.string());
    putString(payload, description);
    putInteger(payload, static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()), 8);
    if (const auto recorded = state->append(Kind::Begin, payload); !recorded)
        return std::unexpected(recorded.error());
    state->changed = false;
    return Transaction(std::move(state));
}

Result<void> Transaction::writeAt(const std::filesystem::path &file, std::uint64_t offset, std::span<const std::byte> data)
{
    const auto path = state_->checked(file);
    if (!path)
        return std::unexpected(path.error());
    if (state_->isWhole(*path)) {
        const auto handle = state_->wholeHandle(*path);
        if (!handle)
            return std::unexpected(handle.error());
        return (*handle)->write(offset, data);
    }
    const auto found = state_->touch(*path);
    if (!found)
        return std::unexpected(found.error());
    State::Touched *touched = *found;
    const auto index = static_cast<std::uint32_t>(touched - state_->touched.data());
    if (offset < touched->originalLength) {
        const std::uint64_t covered = std::min<std::uint64_t>(data.size(), touched->originalLength - offset);
        for (std::uint64_t done = 0; done < covered;) {
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(maxPreimage, covered - done));
            const auto old = touched->handle.read(offset + done, chunk);
            if (!old)
                return std::unexpected(old.error());
            std::vector<std::byte> payload;
            payload.reserve(chunk + preimageHeader);
            putInteger(payload, index, 4);
            putInteger(payload, offset + done, 8);
            putBytes(payload, *old);
            if (const auto recorded = state_->append(Kind::Preimage, payload); !recorded)
                return recorded;
            done += chunk;
        }
    }
    return touched->handle.write(offset, data);
}

Result<std::vector<std::byte>> Transaction::readAt(const std::filesystem::path &file, std::uint64_t offset, std::size_t count)
{
    const auto path = state_->checked(file);
    if (!path)
        return std::unexpected(path.error());
    if (state_->isWhole(*path)) {
        const auto handle = state_->wholeHandle(*path);
        if (!handle)
            return std::unexpected(handle.error());
        return (*handle)->read(offset, count);
    }
    const auto touched = state_->touch(*path);
    if (!touched)
        return std::unexpected(touched.error());
    return (*touched)->handle.read(offset, count);
}

Result<std::uint64_t> Transaction::sizeOf(const std::filesystem::path &file)
{
    const auto path = state_->checked(file);
    if (!path)
        return std::unexpected(path.error());
    if (state_->isWhole(*path)) {
        const auto handle = state_->wholeHandle(*path);
        if (!handle)
            return std::unexpected(handle.error());
        return (*handle)->size();
    }
    if (const State::Touched *touched = state_->findTouched(*path))
        return touched->handle.size();
    std::error_code error;
    const std::uint64_t size = std::filesystem::file_size(*path, error);
    if (error)
        return failure("cannot read the size of " + path->string());
    return size;
}

Result<void> Transaction::replaceFile(const std::filesystem::path &file, const std::function<Result<void>(const std::filesystem::path &temporary)> &produce)
{
    const auto path = state_->checked(file);
    if (!path)
        return std::unexpected(path.error());
    state_->wholeHandles.erase(*path);
    std::error_code error;
    const bool exists = std::filesystem::symlink_status(*path, error).type() != std::filesystem::file_type::not_found;
    if (exists && !isRegularFile(*path))
        return failure(path->string() + " is not a file");
    const std::filesystem::path temporary = temporaryOf(*path);
    std::filesystem::remove(temporary, error);
    auto produced = produce(temporary);
    if (produced)
        produced = syncFile(temporary);
    if (!produced) {
        std::filesystem::remove(temporary, error);
        return produced;
    }
    Result<void> recorded;
    if (exists) {
        recorded = state_->backUp(*path);
    } else {
        std::vector<std::byte> payload;
        putString(payload, path->string());
        putInteger(payload, 0, 1);
        putInteger(payload, 0, 8);
        putInteger(payload, 0, 8);
        recorded = state_->append(Kind::Touch, payload);
    }
    if (!recorded) {
        std::filesystem::remove(temporary, error);
        return recorded;
    }
    state_->whole.push_back(*path);
    std::filesystem::rename(temporary, *path, error);
    if (error)
        return failure("cannot write " + path->string());
    return WritableFile::syncDirectory(path->parent_path());
}

Result<void> Transaction::removeFile(const std::filesystem::path &file)
{
    const auto path = state_->checked(file);
    if (!path)
        return std::unexpected(path.error());
    if (state_->isWhole(*path) || state_->findTouched(*path) != nullptr)
        return failure(path->string() + " was already changed in this transaction");
    if (!isRegularFile(*path))
        return failure(path->string() + " is not a file");
    state_->whole.push_back(*path);
    return state_->backUp(*path);
}

Result<void> Transaction::createDirectories(const std::filesystem::path &directory)
{
    std::filesystem::path normal = directory.lexically_normal();
    if (normal.filename().empty())
        normal = normal.parent_path();
    if (normal == state_->root && !state_->finished)
        return {};
    const auto path = state_->checked(directory);
    if (!path)
        return std::unexpected(path.error());
    std::filesystem::path current = state_->root;
    std::error_code error;
    for (const auto &component : path->lexically_relative(state_->root)) {
        current /= component;
        if (std::filesystem::is_directory(current, error))
            continue;
        std::vector<std::byte> payload;
        putString(payload, current.string());
        if (const auto recorded = state_->append(Kind::Directory, payload); !recorded)
            return recorded;
        if (!std::filesystem::create_directory(current, error) || error)
            return failure("cannot create " + current.string());
    }
    return {};
}

Result<void> Transaction::commit()
{
    if (state_->finished)
        return failure("the transaction is already finished");
    std::error_code error;
    if (!state_->changed) {
        state_->finished = true;
        state_->journal.reset();
        std::filesystem::remove_all(state_->directory, error);
        return {};
    }
    std::vector<std::byte> finals;
    std::uint32_t count = 0;
    const auto addFinal = [&finals, &count](const std::filesystem::path &file) {
        std::error_code sizeError;
        const bool exists = isRegularFile(file);
        putString(finals, file.string());
        putInteger(finals, exists ? 1 : 0, 1);
        putInteger(finals, exists ? std::filesystem::file_size(file, sizeError) : 0, 8);
        putInteger(finals, static_cast<std::uint64_t>(exists ? modifiedOf(file) : 0), 8);
        ++count;
    };
    for (State::Touched &touched : state_->touched) {
        if (!state_->isWhole(touched.file)) {
            if (const auto synced = touched.handle.sync(); !synced)
                return synced;
        }
    }
    for (auto &[file, handle] : state_->wholeHandles) {
        if (const auto synced = handle.sync(); !synced)
            return synced;
    }
    std::vector<std::filesystem::path> files;
    for (const State::Touched &touched : state_->touched)
        files.push_back(touched.file);
    for (const auto &file : state_->whole) {
        if (std::ranges::find(files, file) == files.end())
            files.push_back(file);
    }
    state_->touched.clear();
    state_->wholeHandles.clear();
    for (const auto &file : files)
        addFinal(file);
    std::vector<std::byte> payload;
    putInteger(payload, static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()), 8);
    putInteger(payload, count, 4);
    putBytes(payload, finals);
    if (const auto recorded = state_->append(Kind::Commit, payload); !recorded)
        return recorded;
    state_->finished = true;
    state_->journal.reset();
    return {};
}

Result<void> Transaction::rollback()
{
    if (state_->finished)
        return failure("the transaction is already finished");
    state_->finished = true;
    state_->touched.clear();
    state_->wholeHandles.clear();
    state_->journal.reset();
    return undo(state_->directory);
}

std::vector<TransactionRecord> listTransactions(const std::filesystem::path &store)
{
    std::vector<TransactionRecord> records;
    std::error_code error;
    for (std::filesystem::directory_iterator it(store, error), end; !error && it != end; it.increment(error)) {
        const auto journal = readJournal(it->path());
        if (!journal)
            continue;
        TransactionRecord record { it->path().filename().string(), journal->description, journal->time, {}, journal->committed && !journal->rollingBack };
        for (const Step &step : journal->steps) {
            if ((step.kind == Kind::Touch || step.kind == Kind::Backup) && std::ranges::find(record.files, step.path) == record.files.end())
                record.files.push_back(step.path);
        }
        records.push_back(std::move(record));
    }
    std::ranges::sort(records, std::greater(), &TransactionRecord::id);
    return records;
}

Result<void> rollbackTransaction(const std::filesystem::path &store, const std::string &id)
{
    const auto records = listTransactions(store);
    const auto target = std::ranges::find(records, id, &TransactionRecord::id);
    if (target == records.end())
        return failure("no such change: " + id);
    std::vector<std::filesystem::path> checked;
    for (auto record = records.begin(); record <= target; ++record) {
        const auto journal = readJournal(store / record->id);
        if (!journal)
            return std::unexpected(journal.error());
        if (journal->rollingBack)
            continue;
        for (const Final &final : journal->finals) {
            if (std::ranges::find(checked, final.path) != checked.end())
                continue;
            checked.push_back(final.path);
            if (!matchesFinal(final))
                return failure(final.path.string() + " was changed outside ARCHON since this change; it cannot be rolled back");
        }
        for (const Step &step : journal->steps) {
            if (step.kind == Kind::Touch && std::ranges::find(checked, step.path) == checked.end())
                checked.push_back(step.path);
        }
    }
    for (auto record = records.begin(); record <= target; ++record) {
        if (const auto undone = undo(store / record->id); !undone)
            return undone;
    }
    return {};
}

std::filesystem::path safetyStore(const std::filesystem::path &dataDirectory, const std::filesystem::path &gameRoot)
{
    const std::string root = std::filesystem::absolute(gameRoot).lexically_normal().string();
    const auto digest = sha1(std::as_bytes(std::span(root.data(), root.size())));
    std::string name;
    for (std::size_t i = 0; i < 8; ++i) {
        constexpr char digits[] = "0123456789abcdef";
        name += digits[std::to_integer<unsigned>(digest[i]) >> 4];
        name += digits[std::to_integer<unsigned>(digest[i]) & 0xF];
    }
    return dataDirectory / "safety" / name;
}

}
