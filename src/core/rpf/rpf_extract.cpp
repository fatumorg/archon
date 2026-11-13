#include "core/rpf/rpf_extract.h"

#include <QByteArrayView>
#include <QFile>
#include <QSaveFile>

namespace archon {

namespace {

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "extract", 0, std::move(message) });
}

Result<std::uint64_t> writeEntry(const RpfReader &reader, std::uint32_t entryIndex, const std::filesystem::path &target)
{
    std::error_code error;
    if (std::filesystem::symlink_status(target, error).type() != std::filesystem::file_type::not_found)
        return failure("file already exists");
    const auto content = reader.storedFile(entryIndex);
    if (!content)
        return std::unexpected(content.error());
    std::filesystem::create_directories(target.parent_path(), error);
    QSaveFile file(QFile::decodeName(target.c_str()));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char *>(content->data()), static_cast<qint64>(content->size())) != static_cast<qint64>(content->size())
        || !file.commit())
        return failure("cannot write " + target.string());
    return content->size();
}

}

bool isSafeEntryName(std::string_view name)
{
    return !name.empty()
        && name != "."
        && name != ".."
        && name.front() != ' '
        && name.back() != ' '
        && name.find_first_of(std::string_view("/\\\0", 3)) == std::string_view::npos
        && QByteArrayView(name.data(), static_cast<qsizetype>(name.size())).isValidUtf8();
}

Result<std::filesystem::path> extractEntry(const RpfReader &reader, std::uint32_t entryIndex, const std::filesystem::path &outputDirectory)
{
    const auto &entries = reader.archive().entries();
    if (entryIndex >= entries.size())
        return failure("no such entry");
    if (!isSafeEntryName(entries[entryIndex].name))
        return failure("unsafe entry name");
    const std::filesystem::path target = outputDirectory / entries[entryIndex].name;
    if (const auto written = writeEntry(reader, entryIndex, target); !written)
        return std::unexpected(written.error());
    return target;
}

Result<ExtractSummary> extractTree(const RpfReader &reader, std::uint32_t directoryIndex, const std::filesystem::path &outputDirectory)
{
    const auto &entries = reader.archive().entries();
    if (directoryIndex >= entries.size() || entries[directoryIndex].kind != RpfEntryKind::Directory)
        return failure("entry is not a directory");

    ExtractSummary summary {};
    const auto fail = [&](std::uint32_t index, Error error) {
        error.message = reader.archive().path(index) + ": " + error.message;
        summary.failures.push_back(std::move(error));
    };
    std::error_code ignored;
    std::filesystem::create_directories(outputDirectory, ignored);
    std::vector<std::pair<std::uint32_t, std::filesystem::path>> pending { { directoryIndex, outputDirectory } };
    while (!pending.empty()) {
        const auto [directory, target] = std::move(pending.back());
        pending.pop_back();
        for (std::uint32_t child = entries[directory].firstChild; child < entries[directory].firstChild + entries[directory].childCount; ++child) {
            if (!isSafeEntryName(entries[child].name)) {
                fail(child, Error { "extract", 0, "unsafe entry name" });
                continue;
            }
            const std::filesystem::path childTarget = target / entries[child].name;
            if (entries[child].kind == RpfEntryKind::Directory) {
                std::filesystem::create_directories(childTarget, ignored);
                pending.emplace_back(child, childTarget);
                continue;
            }
            const auto written = writeEntry(reader, child, childTarget);
            if (!written) {
                fail(child, written.error());
                continue;
            }
            ++summary.files;
            summary.bytes += *written;
        }
    }
    return summary;
}

}
