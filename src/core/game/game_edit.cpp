#include "core/game/game_edit.h"

#include "core/io/ascii.h"
#include "core/rpf/rpf_toc.h"
#include "core/rpf/rpf_writer.h"

namespace archon {

namespace {

constexpr std::string_view modsFolder = "mods";

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "edit", 0, std::move(message) });
}

std::string placeName(const std::string &canonical)
{
    return canonical.empty() ? "the game folder" : canonical;
}

std::string joined(const std::string &folder, std::string_view name)
{
    return folder.empty() ? std::string(name) : folder + '/' + std::string(name);
}

std::string counted(std::size_t count, std::string_view singular, std::string_view plural)
{
    return std::to_string(count) + ' ' + std::string(count == 1 ? singular : plural);
}

std::string parentOf(const std::string &canonical)
{
    const std::size_t separator = canonical.rfind('/');
    return separator == std::string::npos ? std::string() : canonical.substr(0, separator);
}

struct ImportFile {
    std::filesystem::path source;
    std::string relative;
};

Result<std::vector<ImportFile>> collect(std::span<const std::filesystem::path> sources)
{
    std::vector<ImportFile> files;
    for (const std::filesystem::path &source : sources) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(source, error);
        if (std::filesystem::is_symlink(status))
            continue;
        const std::string name = source.filename().string();
        if (std::filesystem::is_regular_file(status)) {
            files.push_back({ source, name });
            continue;
        }
        if (!std::filesystem::is_directory(status))
            return failure("cannot read " + source.string());
        std::filesystem::recursive_directory_iterator it(source, error);
        for (const std::filesystem::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
            if (it->is_symlink(error) || !it->is_regular_file(error))
                continue;
            files.push_back({ it->path(), name + '/' + it->path().lexically_relative(source).generic_string() });
        }
        if (error)
            return failure("cannot read " + source.string() + ": " + error.message());
    }
    return files;
}

bool validRelative(std::string_view relative)
{
    while (true) {
        const std::size_t separator = relative.find('/');
        if (!isValidEntryName(relative.substr(0, separator)))
            return false;
        if (separator == std::string_view::npos)
            return true;
        relative = relative.substr(separator + 1);
    }
}

Result<void> copyInto(Transaction &transaction, const std::filesystem::path &target, const std::filesystem::path &source)
{
    return transaction.replaceFile(target, [&source](const std::filesystem::path &temporary) -> Result<void> {
        std::error_code error;
        if (!std::filesystem::copy_file(source, temporary, error) || error)
            return failure("cannot copy " + source.string() + ": " + error.message());
        return {};
    });
}

std::optional<std::string> existingName(const std::filesystem::path &directory, std::string_view name)
{
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const std::string candidate = it->path().filename().string();
        if (asciiEqualIgnoreCase(candidate, name))
            return candidate;
    }
    return std::nullopt;
}

std::string modsName(const std::filesystem::path &root)
{
    return existingName(root, modsFolder).value_or(std::string(modsFolder));
}

}

GameEditor::GameEditor(std::filesystem::path root, std::shared_ptr<const Keys> keys, std::filesystem::path store)
    : root_(std::move(root))
    , keys_(std::move(keys))
    , store_(std::move(store))
{
}

Result<EditTarget> GameEditor::locate(std::string_view path, bool enterArchive) const
{
    GameBrowser browser(root_, keys_);
    return browser.locate(path, enterArchive);
}

Result<void> GameEditor::run(const std::string &description, const std::function<Result<void>(Transaction &)> &work) const
{
    auto transaction = Transaction::begin(store_, root_, description);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto done = work(*transaction); !done)
        return done;
    return transaction->commit();
}

Result<void> GameEditor::import(std::string_view folder, std::span<const std::filesystem::path> sources)
{
    const auto target = locate(folder, true);
    if (!target)
        return std::unexpected(target.error());
    if (!target->directory)
        return failure(target->canonical + " is not a folder");
    const auto files = collect(sources);
    if (!files)
        return std::unexpected(files.error());
    if (files->empty())
        return failure("nothing to import");
    for (const ImportFile &file : *files) {
        if (!validRelative(file.relative))
            return failure("\"" + file.relative + "\" is not a valid name");
    }
    const std::string description = "Add " + counted(files->size(), "file", "files") + " to " + placeName(target->canonical);
    return run(description, [&](Transaction &transaction) -> Result<void> {
        if (!target->archiveFile.empty()) {
            std::vector<ArchiveEdit> edits;
            for (const ImportFile &file : *files)
                edits.push_back({ ArchiveEdit::Kind::Put, joined(target->inner, file.relative), file.source, {} });
            return editArchive(transaction, target->archiveFile, target->nested, edits, keys_.get());
        }
        for (const ImportFile &file : *files) {
            const std::filesystem::path destination = target->disk / std::filesystem::path(file.relative);
            if (auto made = transaction.createDirectories(destination.parent_path()); !made)
                return made;
            if (auto copied = copyInto(transaction, destination, file.source); !copied)
                return copied;
        }
        return {};
    });
}

Result<void> GameEditor::replace(std::string_view file, const std::filesystem::path &source)
{
    const auto target = locate(file, false);
    if (!target)
        return std::unexpected(target.error());
    if (target->directory || target->canonical.empty())
        return failure(placeName(target->canonical) + " is a folder");
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error))
        return failure("cannot read " + source.string());
    return run("Replace " + target->canonical, [&](Transaction &transaction) -> Result<void> {
        if (target->archiveFile.empty())
            return copyInto(transaction, target->disk, source);
        const std::vector<ArchiveEdit> edits { { ArchiveEdit::Kind::Put, target->inner, source, {} } };
        return editArchive(transaction, target->archiveFile, target->nested, edits, keys_.get());
    });
}

Result<void> GameEditor::remove(std::span<const std::string> paths)
{
    if (paths.empty())
        return failure("nothing to delete");
    std::vector<EditTarget> targets;
    for (const std::string &path : paths) {
        auto target = locate(path, false);
        if (!target)
            return std::unexpected(target.error());
        if (target->canonical.empty())
            return failure("the game folder cannot be deleted");
        if (!targets.empty() && (target->archiveFile != targets.front().archiveFile || target->nested != targets.front().nested || parentOf(target->canonical) != parentOf(targets.front().canonical)))
            return failure("items to delete must be in one folder");
        if (target->archiveFile.empty() && target->directory)
            return failure("folders outside archives cannot be deleted");
        targets.push_back(std::move(*target));
    }
    const EditTarget &first = targets.front();
    const std::string description = "Delete " + counted(targets.size(), "item", "items") + " from " + placeName(parentOf(first.canonical));
    return run(description, [&](Transaction &transaction) -> Result<void> {
        if (first.archiveFile.empty()) {
            for (const EditTarget &target : targets) {
                if (auto removed = transaction.removeFile(target.disk); !removed)
                    return removed;
            }
            return {};
        }
        std::vector<ArchiveEdit> edits;
        for (const EditTarget &target : targets)
            edits.push_back({ ArchiveEdit::Kind::Remove, target.inner, {}, {} });
        return editArchive(transaction, first.archiveFile, first.nested, edits, keys_.get());
    });
}

Result<void> GameEditor::makeFolder(std::string_view folder, std::string_view name)
{
    const auto target = locate(folder, true);
    if (!target)
        return std::unexpected(target.error());
    if (!target->directory)
        return failure(target->canonical + " is not a folder");
    if (!isValidEntryName(name))
        return failure("\"" + std::string(name) + "\" is not a valid name");
    return run("New folder " + joined(target->canonical, name), [&](Transaction &transaction) -> Result<void> {
        if (!target->archiveFile.empty()) {
            const std::vector<ArchiveEdit> edits { { ArchiveEdit::Kind::MakeDirectory, joined(target->inner, name), {}, {} } };
            return editArchive(transaction, target->archiveFile, target->nested, edits, keys_.get());
        }
        if (existingName(target->disk, name))
            return failure(joined(target->canonical, name) + " already exists");
        return transaction.createDirectories(target->disk / std::string(name));
    });
}

Result<void> GameEditor::rename(std::string_view path, std::string_view newName)
{
    const auto target = locate(path, false);
    if (!target)
        return std::unexpected(target.error());
    if (target->archiveFile.empty())
        return failure("files and folders outside archives cannot be renamed");
    return run("Rename " + target->canonical + " to " + std::string(newName), [&](Transaction &transaction) -> Result<void> {
        const std::vector<ArchiveEdit> edits { { ArchiveEdit::Kind::Rename, target->inner, {}, std::string(newName) } };
        return editArchive(transaction, target->archiveFile, target->nested, edits, keys_.get());
    });
}

Result<void> GameEditor::rebuild(std::string_view archive)
{
    const auto target = locate(archive, true);
    if (!target)
        return std::unexpected(target.error());
    if (target->archiveFile.empty())
        return failure(placeName(target->canonical) + " is not inside an archive");
    const std::string name = target->archiveFile.lexically_relative(root_).generic_string();
    return run("Rebuild " + name, [&](Transaction &transaction) {
        return rebuildArchive(transaction, target->archiveFile, keys_.get());
    });
}

Result<std::string> GameEditor::copyToMods(std::string_view path)
{
    const auto target = locate(path, false);
    if (!target)
        return std::unexpected(target.error());
    if (isInMods(target->canonical))
        return target->canonical;
    const std::filesystem::path top = target->archiveFile.empty() ? target->disk : target->archiveFile;
    if (target->archiveFile.empty() && target->directory)
        return failure("folders cannot be copied to the mods folder");
    const std::string mods = modsName(root_);
    const std::string mapped = mods + '/' + target->canonical;
    const std::string relative = top.lexically_relative(root_).generic_string();
    const std::filesystem::path destination = root_ / mods / std::filesystem::path(relative);
    std::error_code error;
    if (std::filesystem::exists(destination, error))
        return mapped;
    const auto copied = run("Copy " + relative + " to " + mods, [&](Transaction &transaction) -> Result<void> {
        if (auto made = transaction.createDirectories(destination.parent_path()); !made)
            return made;
        return copyInto(transaction, destination, top);
    });
    if (!copied)
        return std::unexpected(copied.error());
    return mapped;
}

bool isInMods(std::string_view path)
{
    return asciiEqualIgnoreCase(path.substr(0, path.find('/')), modsFolder);
}

bool isInArchive(GameBrowser &browser, std::string_view path)
{
    const auto target = browser.locate(path, true);
    return target && !target->archiveFile.empty();
}

}
