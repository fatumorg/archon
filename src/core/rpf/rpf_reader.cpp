#include "core/rpf/rpf_reader.h"

#include "core/crypto/aes.h"
#include "core/crypto/inflate.h"
#include "core/crypto/ng.h"
#include "core/io/ascii.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"

#include <QFile>

#include <mutex>

namespace archon {

namespace {

constexpr char format[] = "rpf7";
constexpr std::uint32_t sizeInHeaderMarker = 0xFFFFFF;
constexpr std::size_t resourceHeaderSize = 16;

std::unexpected<Error> failure(std::uint64_t offset, std::string message)
{
    return std::unexpected(Error { format, offset, std::move(message) });
}


class FileSource : public RandomAccessSource {
public:
    explicit FileSource(const std::filesystem::path &file)
        : file_(file)
        , name_(file.string())
    {
        if (file_.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
            size_ = static_cast<std::uint64_t>(file_.size());
    }

    std::uint64_t size() const override
    {
        return size_;
    }

    Result<std::vector<std::byte>> read(std::uint64_t offset, std::size_t count) const override
    {
        if (!file_.isOpen())
            return std::unexpected(Error { "file", offset, "cannot open " + name_ });
        if (offset > size_ || count > size_ - offset)
            return std::unexpected(Error { "file", offset, "read past end of " + name_ });
        if (count > maxAllocation)
            return std::unexpected(Error { "file", offset, "read exceeds the allocation limit" });
        std::vector<std::byte> result(count);
        const std::lock_guard lock(mutex_);
        if (!file_.seek(static_cast<qint64>(offset))
            || file_.read(reinterpret_cast<char *>(result.data()), static_cast<qint64>(count)) != static_cast<qint64>(count))
            return std::unexpected(Error { "file", offset, "cannot read " + name_ });
        return result;
    }

private:
    mutable QFile file_;
    mutable std::mutex mutex_;
    std::string name_;
    std::uint64_t size_ = 0;
};

class SubSource : public RandomAccessSource {
public:
    SubSource(std::shared_ptr<RandomAccessSource> parent, std::uint64_t offset, std::uint64_t size)
        : parent_(std::move(parent))
        , offset_(offset)
        , size_(size)
    {
    }

    std::uint64_t size() const override
    {
        return size_;
    }

    Result<std::vector<std::byte>> read(std::uint64_t offset, std::size_t count) const override
    {
        if (offset > size_ || count > size_ - offset || offset_ > parent_->size() || size_ > parent_->size() - offset_)
            return std::unexpected(Error { "file", offset, "read past end of nested data" });
        return parent_->read(offset_ + offset, count);
    }

private:
    std::shared_ptr<RandomAccessSource> parent_;
    std::uint64_t offset_;
    std::uint64_t size_;
};

}

std::shared_ptr<RandomAccessSource> openFileSource(const std::filesystem::path &file)
{
    return std::make_shared<FileSource>(file);
}

std::shared_ptr<RandomAccessSource> openSubSource(std::shared_ptr<RandomAccessSource> parent, std::uint64_t offset, std::uint64_t size)
{
    return std::make_shared<SubSource>(std::move(parent), offset, size);
}

Result<RpfReader> RpfReader::open(std::shared_ptr<RandomAccessSource> source, std::string_view archiveName, const Keys *keys, int depth)
{
    if (depth >= maxArchiveDepth)
        return failure(0, "archive nesting too deep");
    const auto header = source->read(0, 16);
    if (!header)
        return std::unexpected(header.error());
    const auto tocSize = RpfArchive::tocSize(*header);
    if (!tocSize)
        return std::unexpected(tocSize.error());
    const auto toc = source->read(0, *tocSize);
    if (!toc)
        return std::unexpected(toc.error());
    auto archive = RpfArchive::parse(*toc, archiveName, source->size(), keys);
    if (!archive)
        return std::unexpected(archive.error());
    RpfReader reader;
    reader.source_ = std::move(source);
    reader.archive_ = std::move(*archive);
    reader.keys_ = keys;
    reader.depth_ = depth;
    return reader;
}

const RpfArchive &RpfReader::archive() const
{
    return archive_;
}

Result<const RpfEntry *> RpfReader::fileEntry(std::uint32_t entryIndex) const
{
    if (entryIndex >= archive_.entries().size())
        return failure(0, "no such entry");
    const RpfEntry &entry = archive_.entries()[entryIndex];
    if (entry.kind == RpfEntryKind::Directory)
        return failure(entry.offset, "entry is a directory");
    return &entry;
}

Result<std::vector<std::byte>> RpfReader::decrypted(std::vector<std::byte> data, const RpfEntry &entry, std::uint32_t length) const
{
    if (keys_ == nullptr)
        return failure(entry.offset, "entry is encrypted and no keys are loaded");
    if (archive_.encryption() == RpfEncryption::Aes)
        return aesEcbDecrypt(data, keys_->aes);
    return ngDecrypt(*keys_, data, asciiLower(entry.name), length);
}

Result<std::vector<std::byte>> RpfReader::binaryData(const RpfEntry &entry) const
{
    const std::uint32_t occupied = entry.storedSize != 0 ? entry.storedSize : entry.uncompressedSize;
    if (occupied > maxAllocation)
        return failure(entry.offset, "declared size exceeds the allocation limit");
    auto data = source_->read(entry.offset, occupied);
    if (!data)
        return data;
    if (entry.encrypted) {
        data = decrypted(std::move(*data), entry, entry.uncompressedSize);
        if (!data)
            return data;
    }
    if (entry.storedSize == 0)
        return data;
    return inflateRaw(*data, entry.uncompressedSize);
}

Result<std::vector<std::byte>> RpfReader::resourceBody(const RpfEntry &entry) const
{
    std::uint32_t size = entry.storedSize;
    if (size == sizeInHeaderMarker) {
        const auto header = source_->read(entry.offset, resourceHeaderSize);
        if (!header)
            return header;
        const auto at = [&header](std::size_t index) { return std::to_integer<std::uint32_t>((*header)[index]); };
        size = (at(2) << 24) | (at(5) << 16) | (at(14) << 8) | at(7);
    }
    if (size < resourceHeaderSize)
        return failure(entry.offset, "resource is smaller than its header");
    if (size > maxAllocation)
        return failure(entry.offset, "declared size exceeds the allocation limit");
    auto body = source_->read(entry.offset + resourceHeaderSize, size - resourceHeaderSize);
    if (!body)
        return body;
    if (asciiLower(entry.name).ends_with(".ysc"))
        return decrypted(std::move(*body), entry, size);
    return body;
}

Result<std::vector<std::byte>> RpfReader::fileData(std::uint32_t entryIndex) const
{
    const auto entry = fileEntry(entryIndex);
    if (!entry)
        return std::unexpected(entry.error());
    if ((*entry)->kind == RpfEntryKind::Binary)
        return binaryData(**entry);
    const auto body = resourceBody(**entry);
    if (!body)
        return body;
    const std::uint64_t pages = rsc7SizeFromFlags((*entry)->systemFlags) + rsc7SizeFromFlags((*entry)->graphicsFlags);
    if (pages > maxAllocation)
        return failure((*entry)->offset, "declared size exceeds the allocation limit");
    return inflateResourceBody(*body, static_cast<std::size_t>(pages));
}

Result<std::vector<std::byte>> RpfReader::storedFile(std::uint32_t entryIndex) const
{
    const auto entry = fileEntry(entryIndex);
    if (!entry)
        return std::unexpected(entry.error());
    if ((*entry)->kind == RpfEntryKind::Binary)
        return binaryData(**entry);
    const auto body = resourceBody(**entry);
    if (!body)
        return body;
    const auto header = buildRsc7Header({ rsc7VersionFromFlags((*entry)->systemFlags, (*entry)->graphicsFlags), (*entry)->systemFlags, (*entry)->graphicsFlags });
    std::vector<std::byte> file(header.begin(), header.end());
    file.insert(file.end(), body->begin(), body->end());
    return file;
}

Result<RpfReader> RpfReader::openNested(std::uint32_t entryIndex) const
{
    const auto entry = fileEntry(entryIndex);
    if (!entry)
        return std::unexpected(entry.error());
    const RpfEntry &nested = **entry;
    if (nested.kind != RpfEntryKind::Binary || !asciiLower(nested.name).ends_with(".rpf"))
        return failure(nested.offset, "entry is not an archive");
    if (nested.storedSize != 0)
        return failure(nested.offset, "nested archive is compressed");
    return open(openSubSource(source_, nested.offset, nested.uncompressedSize), nested.name, keys_, depth_ + 1);
}

}
