#include "core/oiv/oiv_installer.h"

#include "core/io/ascii.h"
#include "core/meta/metadata_document.h"
#include "core/oiv/content_edit.h"
#include "core/rpf/rpf_reader.h"
#include "core/rpf/rpf_writer.h"
#include "core/safety/transaction.h"

#include <QFile>
#include <QTemporaryDir>

namespace archon {

namespace {

constexpr char format[] = "oiv";
constexpr qint64 textLimit = 256 << 20;
constexpr std::string_view modsFolder = "mods";

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

bool isRegular(const std::filesystem::path &file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(file, error));
}

std::filesystem::path resolvedUnder(const std::filesystem::path &base, std::string_view relative)
{
    std::filesystem::path current = base;
    bool exists = true;
    std::size_t start = 0;
    while (start < relative.size()) {
        std::size_t end = relative.find('/', start);
        if (end == std::string_view::npos)
            end = relative.size();
        const std::string part(relative.substr(start, end - start));
        start = end + 1;
        std::filesystem::path next = current / part;
        if (exists) {
            std::error_code error;
            bool found = false;
            for (std::filesystem::directory_iterator it(current, error), last; !error && it != last; it.increment(error)) {
                if (asciiEqualIgnoreCase(it->path().filename().string(), part)) {
                    next = it->path();
                    found = true;
                    break;
                }
            }
            exists = found;
        }
        current = next;
    }
    return current;
}

Result<void> writeBytes(const std::filesystem::path &file, std::span<const std::byte> bytes)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
        return failure("cannot write " + file.string());
    return {};
}

Result<std::vector<std::byte>> readDiskFile(const std::filesystem::path &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly) || in.size() > textLimit)
        return failure("cannot read " + file.string());
    const QByteArray content = in.readAll();
    const auto bytes = std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

struct Level {
    std::filesystem::path file;
    std::vector<std::string> nested;
};

class Installer {
public:
    Installer(Transaction &transaction, const ZipArchive &zip, const OivPackage &package, InstallTarget target, std::filesystem::path root, const Keys *keys, std::filesystem::path names)
        : transaction_(transaction)
        , zip_(zip)
        , package_(package)
        , target_(target)
        , root_(std::move(root))
        , keys_(keys)
        , namesFile_(std::move(names))
    {
    }

    Result<void> run(const std::vector<OivCommand> &commands)
    {
        if (!temporary_.isValid())
            return failure("cannot create a temporary folder");
        for (const OivCommand &command : commands) {
            if (auto done = disk(command); !done)
                return done;
        }
        return {};
    }

private:
    Result<std::filesystem::path> extract(const std::string &source)
    {
        auto bytes = oivSource(zip_, package_, source);
        if (!bytes)
            return std::unexpected(bytes.error());
        const std::filesystem::path file = std::filesystem::path(temporary_.path().toStdString()) / std::to_string(++extracted_);
        if (auto written = writeBytes(file, *bytes); !written)
            return std::unexpected(written.error());
        return file;
    }

    std::filesystem::path modsRoot() const
    {
        std::error_code error;
        for (std::filesystem::directory_iterator it(root_, error), end; !error && it != end; it.increment(error)) {
            if (asciiEqualIgnoreCase(it->path().filename().string(), modsFolder))
                return it->path();
        }
        return root_ / std::string(modsFolder);
    }

    std::filesystem::path modsPathOf(const std::filesystem::path &original) const
    {
        return resolvedUnder(modsRoot(), original.lexically_relative(root_).generic_string());
    }

    Result<void> createEmpty(const std::filesystem::path &file)
    {
        if (auto made = transaction_.createDirectories(file.parent_path()); !made)
            return made;
        const std::vector<std::byte> empty = emptyArchiveBytes();
        return transaction_.replaceFile(file, [&empty](const std::filesystem::path &temporary) { return writeBytes(temporary, empty); });
    }

    Result<std::filesystem::path> topArchive(const std::string &relative, bool create)
    {
        const std::filesystem::path original = resolvedUnder(root_, relative);
        if (target_ == InstallTarget::Game) {
            if (isRegular(original))
                return original;
            if (!create)
                return failure(relative + " was not found in the game folder");
            if (auto made = createEmpty(original); !made)
                return std::unexpected(made.error());
            return original;
        }
        const std::filesystem::path copy = modsPathOf(original);
        if (isRegular(copy))
            return copy;
        if (auto made = transaction_.createDirectories(copy.parent_path()); !made)
            return std::unexpected(made.error());
        if (isRegular(original)) {
            auto copied = transaction_.replaceFile(copy, [&original](const std::filesystem::path &temporary) -> Result<void> {
                std::error_code error;
                if (!std::filesystem::copy_file(original, temporary, error) || error)
                    return failure("cannot copy " + original.string());
                return {};
            });
            if (!copied)
                return std::unexpected(copied.error());
            return copy;
        }
        if (!create)
            return failure(relative + " was not found in the game folder");
        if (auto made = createEmpty(copy); !made)
            return std::unexpected(made.error());
        return copy;
    }

    std::filesystem::path loosePath(const std::string &relative) const
    {
        const std::filesystem::path original = resolvedUnder(root_, relative);
        if (target_ == InstallTarget::Mods && asciiLower(relative).ends_with(".rpf"))
            return modsPathOf(original);
        return original;
    }

    Result<void> replaceWith(const std::filesystem::path &file, std::span<const std::byte> bytes)
    {
        if (auto made = transaction_.createDirectories(file.parent_path()); !made)
            return made;
        return transaction_.replaceFile(file, [bytes](const std::filesystem::path &temporary) { return writeBytes(temporary, bytes); });
    }

    Result<void> disk(const OivCommand &command)
    {
        switch (command.kind) {
        case OivCommand::Kind::Add: {
            auto bytes = oivSource(zip_, package_, command.source);
            if (!bytes)
                return std::unexpected(bytes.error());
            return replaceWith(loosePath(command.path), *bytes);
        }
        case OivCommand::Kind::Delete: {
            const std::filesystem::path file = loosePath(command.path);
            if (!isRegular(file))
                return {};
            return transaction_.removeFile(file);
        }
        case OivCommand::Kind::Archive: {
            auto file = topArchive(command.path, command.createIfNotExist);
            if (!file)
                return std::unexpected(file.error());
            return archive({ *file, {} }, command.children);
        }
        case OivCommand::Kind::Defragment: {
            auto file = topArchive(command.path, false);
            if (!file)
                return std::unexpected(file.error());
            return rebuildArchive(transaction_, *file, keys_);
        }
        case OivCommand::Kind::Text:
        case OivCommand::Kind::Xml:
        case OivCommand::Kind::Pso: {
            const std::filesystem::path file = resolvedUnder(root_, command.path);
            std::vector<std::byte> original;
            if (isRegular(file)) {
                auto read = readDiskFile(file);
                if (!read)
                    return std::unexpected(read.error());
                original = std::move(*read);
            } else if (command.kind != OivCommand::Kind::Text || !command.createIfNotExist) {
                return failure(command.path + " was not found in the game folder");
            }
            auto edited = editedContent(command, original);
            if (!edited)
                return std::unexpected(edited.error());
            return replaceWith(file, *edited);
        }
        }
        return {};
    }

    const NameTable &names()
    {
        if (!namesLoaded_) {
            names_.load(namesFile_);
            namesLoaded_ = true;
        }
        return names_;
    }

    Result<std::vector<std::byte>> editedContent(const OivCommand &command, std::span<const std::byte> original)
    {
        if (command.kind == OivCommand::Kind::Text)
            return applyTextEdits(original, command.text);
        if (command.kind == OivCommand::Kind::Xml)
            return applyXmlEdits(original, command.xml);
        const auto document = openMetadata(original, names());
        if (!document)
            return failure(command.path + ": " + document.error().message);
        if (!document->readOnlyReason.empty())
            return failure(command.path + ": " + document->readOnlyReason);
        const auto edited = applyXmlEdits(std::as_bytes(std::span(document->xml.data(), document->xml.size())), command.xml);
        if (!edited)
            return std::unexpected(edited.error());
        const auto saved = saveMetadata(original, std::string_view(reinterpret_cast<const char *>(edited->data()), edited->size()), names());
        if (!saved)
            return failure(command.path + ": " + saved.error().message);
        return saved;
    }

    Result<RpfReader> openLevel(const Level &level) const
    {
        auto reader = RpfReader::open(openFileSource(level.file), level.file.filename().string(), keys_);
        if (!reader)
            return reader;
        for (const std::string &path : level.nested) {
            const auto index = reader->archive().find(path);
            if (!index)
                return failure(path + " not found");
            auto next = reader->openNested(*index);
            if (!next)
                return next;
            reader = std::move(next);
        }
        return reader;
    }

    Result<std::optional<std::vector<std::byte>>> readEntry(const Level &level, const std::string &path) const
    {
        const auto reader = openLevel(level);
        if (!reader)
            return std::unexpected(reader.error());
        const auto index = reader->archive().find(path);
        if (!index)
            return std::optional<std::vector<std::byte>>();
        if (reader->archive().entries()[*index].kind == RpfEntryKind::Directory)
            return failure(path + " is a folder");
        auto data = reader->fileData(*index);
        if (!data)
            return std::unexpected(data.error());
        return std::optional<std::vector<std::byte>>(std::move(*data));
    }

    Result<bool> entryExists(const Level &level, const std::string &path) const
    {
        const auto reader = openLevel(level);
        if (!reader)
            return std::unexpected(reader.error());
        return reader->archive().find(path).has_value();
    }

    Result<void> putBytes(const Level &level, const std::string &path, std::span<const std::byte> bytes)
    {
        const std::filesystem::path file = std::filesystem::path(temporary_.path().toStdString()) / std::to_string(++extracted_);
        if (auto written = writeBytes(file, bytes); !written)
            return written;
        const std::vector<ArchiveEdit> edits { { ArchiveEdit::Kind::Put, path, file, {} } };
        return editArchive(transaction_, level.file, level.nested, edits, keys_);
    }

    Result<void> archive(const Level &level, const std::vector<OivCommand> &commands)
    {
        std::vector<ArchiveEdit> pending;
        const auto flush = [&]() -> Result<void> {
            if (pending.empty())
                return {};
            auto edited = editArchive(transaction_, level.file, level.nested, pending, keys_);
            pending.clear();
            return edited;
        };
        for (const OivCommand &command : commands) {
            if (command.kind == OivCommand::Kind::Add) {
                auto source = extract(command.source);
                if (!source)
                    return std::unexpected(source.error());
                pending.push_back({ ArchiveEdit::Kind::Put, command.path, *source, {} });
                continue;
            }
            if (auto flushed = flush(); !flushed)
                return flushed;
            switch (command.kind) {
            case OivCommand::Kind::Delete: {
                auto exists = entryExists(level, command.path);
                if (!exists)
                    return std::unexpected(exists.error());
                if (*exists)
                    pending.push_back({ ArchiveEdit::Kind::Remove, command.path, {}, {} });
                break;
            }
            case OivCommand::Kind::Archive: {
                auto exists = entryExists(level, command.path);
                if (!exists)
                    return std::unexpected(exists.error());
                if (!*exists) {
                    if (!command.createIfNotExist)
                        return failure(command.path + " was not found in " + level.file.filename().string());
                    if (auto made = putBytes(level, command.path, emptyArchiveBytes()); !made)
                        return made;
                }
                Level inner = level;
                inner.nested.push_back(command.path);
                if (auto done = archive(inner, command.children); !done)
                    return done;
                break;
            }
            case OivCommand::Kind::Defragment:
                if (auto rebuilt = rebuildArchive(transaction_, level.file, keys_); !rebuilt)
                    return rebuilt;
                break;
            case OivCommand::Kind::Text:
            case OivCommand::Kind::Xml:
            case OivCommand::Kind::Pso: {
                auto original = readEntry(level, command.path);
                if (!original)
                    return std::unexpected(original.error());
                if (!*original && (command.kind != OivCommand::Kind::Text || !command.createIfNotExist))
                    return failure(command.path + " was not found in " + level.file.filename().string());
                auto edited = editedContent(command, original->value_or(std::vector<std::byte>()));
                if (!edited)
                    return std::unexpected(edited.error());
                if (auto put = putBytes(level, command.path, *edited); !put)
                    return put;
                break;
            }
            case OivCommand::Kind::Add:
                break;
            }
        }
        return flush();
    }

    Transaction &transaction_;
    const ZipArchive &zip_;
    const OivPackage &package_;
    InstallTarget target_;
    std::filesystem::path root_;
    const Keys *keys_;
    std::filesystem::path namesFile_;
    NameTable names_;
    bool namesLoaded_ = false;
    QTemporaryDir temporary_;
    std::size_t extracted_ = 0;
};

}

Result<void> installOivPackage(const ZipArchive &zip, const OivPackage &package, std::size_t content, InstallTarget target, const std::filesystem::path &root, std::shared_ptr<const Keys> keys, const std::filesystem::path &store)
{
    if (content >= package.contents.size())
        return failure("the package has no such installation option");
    const bool legacy = package.version.starts_with('1');
    const std::string description = "Install package " + package.name + (legacy ? std::string() : " " + std::to_string(package.major) + "." + std::to_string(package.minor));
    auto transaction = Transaction::begin(store, root, description);
    if (!transaction)
        return std::unexpected(transaction.error());
    Installer installer(*transaction, zip, package, target, root, keys.get(), nameTableFile(store));
    if (auto done = installer.run(package.contents[content].commands); !done)
        return done;
    return transaction->commit();
}

}
