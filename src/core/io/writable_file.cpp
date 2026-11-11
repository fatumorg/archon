#include "core/io/writable_file.h"

#include <QFile>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace archon {

WritableFile::WritableFile() = default;
WritableFile::WritableFile(WritableFile &&) noexcept = default;
WritableFile &WritableFile::operator=(WritableFile &&) noexcept = default;
WritableFile::~WritableFile() = default;

std::unexpected<Error> WritableFile::failure(std::uint64_t offset, const char *action) const
{
    return std::unexpected(Error { "file", offset, std::string("cannot ") + action + " " + name_ });
}

Result<WritableFile> WritableFile::open(const std::filesystem::path &file, bool create)
{
    WritableFile opened;
    opened.name_ = file.string();
    opened.file_ = std::make_unique<QFile>(file);
    QIODevice::OpenMode mode = QIODevice::ReadWrite | QIODevice::Unbuffered;
    if (!create)
        mode |= QIODevice::ExistingOnly;
    if (!opened.file_->open(mode))
        return opened.failure(0, "open");
    return opened;
}

std::uint64_t WritableFile::size() const
{
    return static_cast<std::uint64_t>(file_->size());
}

Result<std::vector<std::byte>> WritableFile::read(std::uint64_t offset, std::size_t count)
{
    if (offset > size() || count > size() - offset || count > maxAllocation)
        return failure(offset, "read past the end of");
    std::vector<std::byte> result(count);
    if (!file_->seek(static_cast<qint64>(offset))
        || file_->read(reinterpret_cast<char *>(result.data()), static_cast<qint64>(count)) != static_cast<qint64>(count))
        return failure(offset, "read");
    return result;
}

Result<void> WritableFile::write(std::uint64_t offset, std::span<const std::byte> data)
{
    if (!file_->seek(static_cast<qint64>(offset))
        || file_->write(reinterpret_cast<const char *>(data.data()), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size()))
        return failure(offset, "write");
    return {};
}

Result<void> WritableFile::resize(std::uint64_t size)
{
    if (!file_->resize(static_cast<qint64>(size)))
        return failure(size, "resize");
    return {};
}

Result<void> WritableFile::sync()
{
#ifdef Q_OS_WIN
    const bool synced = _commit(file_->handle()) == 0;
#else
    const bool synced = fsync(file_->handle()) == 0;
#endif
    if (!synced)
        return failure(0, "flush");
    return {};
}

}
