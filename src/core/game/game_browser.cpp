#include "core/game/game_browser.h"

#include "core/io/ascii.h"
#include "core/rsc/rsc7.h"

namespace archon {

namespace {

constexpr std::uint32_t sizeInResourceHeader = 0xFFFFFF;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "browse", 0, std::move(message) });
}

bool isArchiveName(std::string_view name)
{
    return asciiLower(name).ends_with(".rpf");
}

BrowserNode nodeOf(const RpfEntry &entry)
{
    BrowserNode node { entry.name, NodeKind::File, 0, true, false, entry.encrypted, false };
    switch (entry.kind) {
    case RpfEntryKind::Directory:
        node.kind = NodeKind::Folder;
        node.size = entry.childCount;
        node.encrypted = false;
        break;
    case RpfEntryKind::Binary:
        node.kind = isArchiveName(entry.name) ? NodeKind::Archive : NodeKind::File;
        node.compressed = entry.storedSize != 0;
        node.size = entry.uncompressedSize != 0 ? entry.uncompressedSize : entry.storedSize;
        break;
    case RpfEntryKind::Resource:
        node.resource = true;
        node.size = entry.storedSize == sizeInResourceHeader
            ? rsc7SizeFromFlags(entry.systemFlags) + rsc7SizeFromFlags(entry.graphicsFlags)
            : entry.storedSize;
        break;
    }
    return node;
}

std::optional<BrowserNode> nodeOf(const std::filesystem::directory_entry &item)
{
    std::error_code error;
    BrowserNode node { item.path().filename().string(), NodeKind::File, 0, false, false, false, false };
    if (item.is_directory(error)) {
        node.kind = NodeKind::Folder;
        for (std::filesystem::directory_iterator it(item.path(), error), end; !error && it != end; it.increment(error))
            ++node.size;
        return node;
    }
    if (!item.is_regular_file(error))
        return std::nullopt;
    node.kind = isArchiveName(node.name) ? NodeKind::Archive : NodeKind::File;
    node.size = item.file_size(error);
    return node;
}

std::optional<std::filesystem::path> childOnDisk(const std::filesystem::path &directory, std::string_view name)
{
    std::error_code error;
    const std::filesystem::path exact = directory / name;
    if (std::filesystem::exists(exact, error))
        return exact;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (asciiEqualIgnoreCase(it->path().filename().string(), name))
            return it->path();
    }
    return std::nullopt;
}

std::optional<std::uint32_t> childInArchive(const RpfArchive &archive, std::uint32_t directory, std::string_view name)
{
    const auto &entries = archive.entries();
    const RpfEntry &parent = entries[directory];
    std::optional<std::uint32_t> loose;
    for (std::uint32_t child = parent.firstChild; child < parent.firstChild + parent.childCount; ++child) {
        if (entries[child].name == name)
            return child;
        if (!loose && asciiEqualIgnoreCase(entries[child].name, name))
            loose = child;
    }
    return loose;
}

}

GameBrowser::GameBrowser(std::filesystem::path root, std::shared_ptr<const Keys> keys)
    : root_(std::move(root))
    , keys_(std::move(keys))
{
}

Result<std::shared_ptr<const RpfReader>> GameBrowser::archiveAt(const std::string &canonical, const Location &location)
{
    if (const auto cached = archives_.find(canonical); cached != archives_.end())
        return cached->second;
    auto opened = location.reader
        ? location.reader->openNested(location.entry)
        : RpfReader::open(openFileSource(location.disk), location.name, keys_.get());
    if (!opened) {
        Error error = opened.error();
        error.message = canonical + ": " + error.message;
        return std::unexpected(std::move(error));
    }
    auto reader = std::make_shared<const RpfReader>(std::move(*opened));
    archives_.emplace(canonical, reader);
    return reader;
}

Result<GameBrowser::Location> GameBrowser::resolve(std::string_view path, bool enterArchive)
{
    Location location;
    location.disk = root_;
    bool onDiskFile = false;
    while (!path.empty()) {
        const std::size_t separator = path.find('/');
        const std::string_view component = path.substr(0, separator);
        path = separator == std::string_view::npos ? std::string_view() : path.substr(separator + 1);
        if (component.empty())
            continue;
        if (onDiskFile)
            return failure(location.canonical + " is not a folder");

        bool archive = false;
        if (location.reader) {
            const auto &entries = location.reader->archive().entries();
            if (entries[location.entry].kind != RpfEntryKind::Directory)
                return failure(location.canonical + " is not a folder");
            const auto child = childInArchive(location.reader->archive(), location.entry, component);
            if (!child)
                return failure(std::string(component) + " not found in " + location.canonical);
            location.entry = *child;
            location.name = entries[*child].name;
            location.inner = location.inner.empty() ? location.name : location.inner + '/' + location.name;
            archive = entries[*child].kind == RpfEntryKind::Binary && isArchiveName(location.name);
        } else {
            if (component == "." || component == "..")
                return failure("path leaves the game folder");
            const auto child = childOnDisk(location.disk, component);
            if (!child)
                return failure(std::string(component) + " not found in " + (location.canonical.empty() ? root_.string() : location.canonical));
            std::error_code error;
            location.disk = *child;
            location.name = child->filename().string();
            onDiskFile = !std::filesystem::is_directory(*child, error);
            archive = onDiskFile && isArchiveName(location.name);
        }
        location.canonical = location.canonical.empty() ? location.name : location.canonical + '/' + location.name;

        if (archive && (enterArchive || !path.empty())) {
            auto reader = archiveAt(location.canonical, location);
            if (!reader)
                return std::unexpected(reader.error());
            if (location.reader)
                location.nested.push_back(location.inner);
            else
                location.archiveFile = location.disk;
            location.inner.clear();
            location.reader = std::move(*reader);
            location.entry = 0;
            onDiskFile = false;
        }
    }
    return location;
}

Result<std::vector<BrowserNode>> GameBrowser::list(std::string_view path)
{
    const auto location = resolve(path, true);
    if (!location)
        return std::unexpected(location.error());
    std::vector<BrowserNode> nodes;
    if (location->reader) {
        const auto &entries = location->reader->archive().entries();
        const RpfEntry &directory = entries[location->entry];
        if (directory.kind != RpfEntryKind::Directory)
            return failure(location->canonical + " is not a folder");
        nodes.reserve(directory.childCount);
        for (std::uint32_t child = directory.firstChild; child < directory.firstChild + directory.childCount; ++child)
            nodes.push_back(nodeOf(entries[child]));
        return nodes;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(location->disk, error))
        return failure(location->canonical + " is not a folder");
    for (std::filesystem::directory_iterator it(location->disk, error), end; !error && it != end; it.increment(error)) {
        if (auto node = nodeOf(*it))
            nodes.push_back(std::move(*node));
    }
    if (error)
        return failure("cannot read " + location->disk.string() + ": " + error.message());
    return nodes;
}

Result<std::string> GameBrowser::canonicalPath(std::string_view path)
{
    const auto location = resolve(path, false);
    if (!location)
        return std::unexpected(location.error());
    return location->canonical;
}

Result<ExtractSummary> GameBrowser::extract(std::string_view path, const std::filesystem::path &outputDirectory)
{
    const auto location = resolve(path, false);
    if (!location)
        return std::unexpected(location.error());
    if (location->canonical.empty())
        return failure("nothing to extract");
    if (!isSafeEntryName(location->name))
        return failure("unsafe entry name");
    const std::filesystem::path target = outputDirectory / location->name;
    std::error_code error;

    if (location->reader) {
        if (location->reader->archive().entries()[location->entry].kind == RpfEntryKind::Directory)
            return extractTree(*location->reader, location->entry, target);
        const auto written = extractEntry(*location->reader, location->entry, outputDirectory);
        if (!written)
            return std::unexpected(written.error());
        return ExtractSummary { 1, std::filesystem::file_size(*written, error), {} };
    }

    if (std::filesystem::is_directory(location->disk, error))
        return failure("folders outside archives are not extracted");
    if (std::filesystem::symlink_status(target, error).type() != std::filesystem::file_type::not_found)
        return failure("file already exists");
    std::filesystem::create_directories(outputDirectory, error);
    if (!std::filesystem::copy_file(location->disk, target, error) || error)
        return failure("cannot write " + target.string());
    return ExtractSummary { 1, std::filesystem::file_size(target, error), {} };
}

Result<EditTarget> GameBrowser::locate(std::string_view path, bool enterArchive)
{
    const auto location = resolve(path, enterArchive);
    if (!location)
        return std::unexpected(location.error());
    std::error_code error;
    const bool directory = location->reader
        ? location->reader->archive().entries()[location->entry].kind == RpfEntryKind::Directory
        : std::filesystem::is_directory(location->disk, error);
    return EditTarget { location->disk, location->archiveFile, location->nested, location->inner, location->name, location->canonical, directory };
}

void GameBrowser::forget()
{
    archives_.clear();
}

}
