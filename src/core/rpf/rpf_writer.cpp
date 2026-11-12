#include "core/rpf/rpf_writer.h"

#include "core/crypto/aes.h"
#include "core/crypto/inflate.h"
#include "core/crypto/ng.h"
#include "core/io/writable_file.h"
#include "core/io/ascii.h"
#include "core/rpf/rpf_reader.h"
#include "core/rpf/rpf_toc.h"
#include "core/rsc/rsc7.h"

#include <QFile>

#include <algorithm>

namespace archon {

namespace {

constexpr char format[] = "rpf7";
constexpr std::size_t copyChunk = 8u << 20;
constexpr std::size_t resourceHeaderSize = 16;
constexpr std::uint64_t maxNestedSize = 0xFFFFFFFFull;
constexpr std::uint32_t archiveMagic = 0x52504637;
constexpr std::uint32_t openTag = 0x4E45504F;
constexpr std::uint32_t aesTag = 0x0FFFFFF9;
constexpr std::array<std::byte, 4> resourceMagic { std::byte { 'R' }, std::byte { 'S' }, std::byte { 'C' }, std::byte { '7' } };

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

std::uint64_t aligned(std::uint64_t offset)
{
    return (offset + rpfBlockSize - 1) / rpfBlockSize * rpfBlockSize;
}

bool endsWith(std::string_view name, std::string_view extension)
{
    return asciiLower(name).ends_with(extension);
}

bool isStoredRaw(std::string_view name)
{
    return endsWith(name, ".rpf") || endsWith(name, ".awc") || endsWith(name, ".bik");
}

struct Node {
    RpfEntry entry {};
    std::vector<Node> children;
    std::filesystem::path source;
    bool pending = false;
    bool keepRaw = false;
};

struct Level {
    std::string pathInParent;
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    RpfEncryption encryption = RpfEncryption::None;
    Node root;
};

struct Flat {
    std::vector<Node *> nodes;
    std::vector<RpfEntry> entries;
};

Node buildTree(const std::vector<RpfEntry> &entries, std::uint32_t index)
{
    Node node;
    node.entry = entries[index];
    if (node.entry.kind == RpfEntryKind::Directory) {
        node.children.reserve(node.entry.childCount);
        for (std::uint32_t child = node.entry.firstChild; child < node.entry.firstChild + node.entry.childCount; ++child)
            node.children.push_back(buildTree(entries, child));
    }
    return node;
}

Flat flatten(Node &root)
{
    Flat flat;
    flat.nodes.push_back(&root);
    for (std::size_t index = 0; index < flat.nodes.size(); ++index) {
        Node &node = *flat.nodes[index];
        if (node.entry.kind != RpfEntryKind::Directory)
            continue;
        std::vector<std::pair<std::string, Node *>> children;
        children.reserve(node.children.size());
        for (Node &child : node.children)
            children.emplace_back(asciiLower(child.entry.name), &child);
        std::ranges::sort(children, {}, &std::pair<std::string, Node *>::first);
        node.entry.firstChild = static_cast<std::uint32_t>(flat.nodes.size());
        node.entry.childCount = static_cast<std::uint32_t>(children.size());
        for (const auto &child : children)
            flat.nodes.push_back(child.second);
    }
    flat.entries.reserve(flat.nodes.size());
    for (const Node *node : flat.nodes)
        flat.entries.push_back(node->entry);
    return flat;
}

Node *findChild(Node &directory, std::string_view name)
{
    const auto found = std::ranges::find_if(directory.children, [name](const Node &child) { return asciiEqualIgnoreCase(child.entry.name, name); });
    return found == directory.children.end() ? nullptr : &*found;
}

std::vector<std::string_view> components(std::string_view path)
{
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const std::size_t separator = path.find('/');
        parts.push_back(path.substr(0, separator));
        path = separator == std::string_view::npos ? std::string_view() : path.substr(separator + 1);
    }
    return parts;
}

Node *findNode(Node &root, std::string_view path)
{
    Node *current = &root;
    for (const std::string_view part : components(path)) {
        if (current->entry.kind != RpfEntryKind::Directory)
            return nullptr;
        current = findChild(*current, part);
        if (current == nullptr)
            return nullptr;
    }
    return current;
}

std::pair<std::string_view, std::string_view> splitLeaf(std::string_view path)
{
    const std::size_t separator = path.rfind('/');
    if (separator == std::string_view::npos)
        return { std::string_view(), path };
    return { path.substr(0, separator), path.substr(separator + 1) };
}

Node directoryNode(std::string_view name)
{
    Node node;
    node.entry.kind = RpfEntryKind::Directory;
    node.entry.name = name;
    return node;
}

Result<Node *> ensureDirectory(Node &root, std::string_view path)
{
    Node *current = &root;
    for (const std::string_view part : components(path)) {
        Node *child = findChild(*current, part);
        if (child == nullptr) {
            current->children.push_back(directoryNode(part));
            child = &current->children.back();
        } else if (child->entry.kind != RpfEntryKind::Directory) {
            return failure(std::string(part) + " is a file, not a folder");
        }
        current = child;
    }
    return current;
}

bool hasScripts(const Node &node)
{
    if (node.entry.kind == RpfEntryKind::Resource)
        return endsWith(node.entry.name, ".ysc");
    return std::ranges::any_of(node.children, hasScripts);
}

Result<std::vector<std::byte>> readSource(const std::filesystem::path &source)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error))
        return failure("cannot read " + source.string());
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly))
        return failure("cannot read " + source.string());
    if (static_cast<std::uint64_t>(file.size()) > maxAllocation)
        return failure(source.string() + " is too large to import");
    const QByteArray content = file.readAll();
    if (content.size() != file.size())
        return failure("cannot read " + source.string());
    const auto bytes = std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

bool isResourceFile(std::span<const std::byte> content)
{
    return content.size() >= resourceHeaderSize && std::ranges::equal(content.first(resourceMagic.size()), resourceMagic);
}

template <typename ReadHeader>
Result<std::uint64_t> occupiedSize(const RpfEntry &entry, ReadHeader &&readHeader)
{
    if (entry.kind == RpfEntryKind::Binary)
        return entry.storedSize != 0 ? entry.storedSize : entry.uncompressedSize;
    if (entry.storedSize != rpfSizeInHeaderMarker)
        return entry.storedSize;
    const Result<std::vector<std::byte>> header = readHeader(resourceHeaderSize);
    if (!header)
        return std::unexpected(header.error());
    const auto at = [&header](std::size_t index) { return std::to_integer<std::uint64_t>((*header)[index]); };
    return (at(2) << 24) | (at(5) << 16) | (at(14) << 8) | at(7);
}

class Editor {
public:
    Editor(Transaction &transaction, const std::filesystem::path &file, const Keys *keys)
        : transaction_(transaction)
        , file_(file)
        , keys_(keys)
    {
    }

    Result<void> run(std::span<const std::string> nested, std::span<const ArchiveEdit> edits)
    {
        if (auto step = open(nested); !step)
            return step;
        if (auto step = moveNestedToTail(); !step)
            return step;
        if (auto step = decryptAesEntries(); !step)
            return step;
        for (const ArchiveEdit &edit : edits) {
            if (auto step = apply(levels_.back().root, edit); !step)
                return step;
        }
        if (auto step = finish(); !step)
            return step;
        return verify(nested);
    }

private:
    Result<RpfReader> openChain(std::span<const std::string> nested, const std::function<void(const RpfReader &, std::string_view, std::uint64_t, std::uint64_t)> &visit) const
    {
        auto source = openFileSource(file_);
        const std::uint64_t size = source->size();
        auto reader = RpfReader::open(std::move(source), file_.filename().string(), keys_);
        if (!reader)
            return reader;
        visit(*reader, {}, 0, size);
        std::uint64_t base = 0;
        for (const std::string &path : nested) {
            const auto index = reader->archive().find(path);
            if (!index)
                return failure(path + " not found");
            const RpfEntry entry = reader->archive().entries()[*index];
            auto next = reader->openNested(*index);
            if (!next)
                return next;
            base += entry.offset;
            visit(*next, path, base, entry.uncompressedSize);
            reader = std::move(next);
        }
        return reader;
    }

    Result<void> open(std::span<const std::string> nested)
    {
        const auto opened = openChain(nested, [this](const RpfReader &reader, std::string_view path, std::uint64_t base, std::uint64_t size) {
            levels_.push_back({ std::string(path), base, size, reader.archive().encryption(), buildTree(reader.archive().entries(), 0) });
        });
        if (!opened)
            return std::unexpected(opened.error());
        for (const Level &level : levels_) {
            if (level.encryption != RpfEncryption::Aes)
                continue;
            if (hasScripts(level.root))
                return failure("scripts in an AES archive cannot be re-encrypted");
            if (keys_ == nullptr)
                return failure("archive is encrypted and no keys are loaded");
        }
        const auto size = transaction_.sizeOf(file_);
        if (!size)
            return std::unexpected(size.error());
        end_ = *size;
        return {};
    }

    Result<void> copyRange(std::uint64_t from, std::uint64_t to, std::uint64_t size)
    {
        for (std::uint64_t done = 0; done < size;) {
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(copyChunk, size - done));
            const auto bytes = transaction_.readAt(file_, from + done, chunk);
            if (!bytes)
                return std::unexpected(bytes.error());
            if (auto written = transaction_.writeAt(file_, to + done, *bytes); !written)
                return written;
            done += chunk;
        }
        return {};
    }

    Result<std::uint64_t> appendCopy(std::uint64_t from, std::uint64_t size)
    {
        const std::uint64_t target = aligned(end_);
        if (auto copied = copyRange(from, target, size); !copied)
            return std::unexpected(copied.error());
        end_ = target + size;
        return target;
    }

    Result<void> moveNestedToTail()
    {
        for (std::size_t index = 1; index < levels_.size(); ++index) {
            Level &level = levels_[index];
            if (level.base + level.size == end_)
                continue;
            const auto target = appendCopy(level.base, level.size);
            if (!target)
                return std::unexpected(target.error());
            findNode(levels_[index - 1].root, level.pathInParent)->entry.offset = *target - levels_[index - 1].base;
            const std::uint64_t shift = *target - level.base;
            for (std::size_t moved = index; moved < levels_.size(); ++moved)
                levels_[moved].base += shift;
        }
        return {};
    }

    Result<void> rewriteDecrypted(const Level &level, Node &node, bool aes)
    {
        const std::uint64_t size = node.entry.storedSize != 0 ? node.entry.storedSize : node.entry.uncompressedSize;
        const std::uint64_t start = level.base + node.entry.offset;
        if (!aes && size > maxAllocation)
            return failure(node.entry.name + " is too large to decrypt");
        const std::size_t chunkLimit = aes ? copyChunk : static_cast<std::size_t>(size);
        for (std::uint64_t done = 0; done < size;) {
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(chunkLimit, size - done));
            const auto bytes = transaction_.readAt(file_, start + done, chunk);
            if (!bytes)
                return std::unexpected(bytes.error());
            Result<std::vector<std::byte>> plain = aes
                ? aesEcbDecrypt(*bytes, keys_->aes)
                : Result<std::vector<std::byte>>(ngDecrypt(*keys_, *bytes, asciiLower(node.entry.name), node.entry.uncompressedSize));
            if (!plain)
                return std::unexpected(plain.error());
            if (auto written = transaction_.writeAt(file_, start + done, *plain); !written)
                return written;
            done += chunk;
        }
        node.entry.encrypted = false;
        return {};
    }

    Result<void> decryptAesEntries()
    {
        for (Level &level : levels_) {
            if (level.encryption != RpfEncryption::Aes)
                continue;
            for (Node *node : flatten(level.root).nodes) {
                if (node->entry.kind != RpfEntryKind::Binary || !node->entry.encrypted)
                    continue;
                if (auto decrypted = rewriteDecrypted(level, *node, true); !decrypted)
                    return decrypted;
            }
        }
        return {};
    }

    Result<void> apply(Node &root, const ArchiveEdit &edit)
    {
        const auto [parentPath, leaf] = splitLeaf(edit.path);
        const bool creates = edit.kind == ArchiveEdit::Kind::Put || edit.kind == ArchiveEdit::Kind::MakeDirectory;
        if (creates && !std::ranges::all_of(components(edit.path), isValidEntryName))
            return failure("\"" + edit.path + "\" is not a valid entry name");
        if (edit.path.empty())
            return failure("the archive root cannot be changed");

        if (creates) {
            const auto parent = ensureDirectory(root, parentPath);
            if (!parent)
                return std::unexpected(parent.error());
            Node *existing = findChild(**parent, leaf);
            if (edit.kind == ArchiveEdit::Kind::MakeDirectory) {
                if (existing != nullptr)
                    return failure(edit.path + " already exists");
                (*parent)->children.push_back(directoryNode(leaf));
                return {};
            }
            if (endsWith(leaf, ".ysc"))
                return failure("scripts cannot be imported: ARCHON cannot encrypt them");
            if (existing == nullptr) {
                (*parent)->children.emplace_back();
                existing = &(*parent)->children.back();
                existing->entry.name = leaf;
            } else if (existing->entry.kind == RpfEntryKind::Directory) {
                return failure(edit.path + " is a folder");
            } else {
                existing->keepRaw = existing->entry.kind == RpfEntryKind::Binary && existing->entry.storedSize == 0 && existing->entry.uncompressedSize != 0;
            }
            existing->pending = true;
            existing->source = edit.source;
            return {};
        }

        Node *parent = findNode(root, parentPath);
        Node *target = parent != nullptr && parent->entry.kind == RpfEntryKind::Directory ? findChild(*parent, leaf) : nullptr;
        if (target == nullptr)
            return failure(edit.path + " not found");
        if (edit.kind == ArchiveEdit::Kind::Remove) {
            parent->children.erase(parent->children.begin() + (target - parent->children.data()));
            return {};
        }
        if (!isValidEntryName(edit.newName))
            return failure("\"" + edit.newName + "\" is not a valid entry name");
        const Node *clash = findChild(*parent, edit.newName);
        if (clash != nullptr && clash != target)
            return failure(edit.newName + " already exists");
        if (target->entry.kind == RpfEntryKind::Resource && (endsWith(target->entry.name, ".ysc") || endsWith(edit.newName, ".ysc")))
            return failure("scripts cannot be renamed: ARCHON cannot encrypt them");
        if (auto ng = isNgArchive(*target); !ng || *ng)
            return ng ? failure("encrypted archives inside archives cannot be renamed: the name is part of their key") : std::unexpected(ng.error());
        if (target->entry.kind == RpfEntryKind::Binary && target->entry.encrypted && !target->pending) {
            if (keys_ == nullptr)
                return failure("entry is encrypted and no keys are loaded");
            if (auto decrypted = rewriteDecrypted(levels_.back(), *target, false); !decrypted)
                return decrypted;
        }
        target->entry.name = edit.newName;
        return {};
    }

    Result<bool> isNgArchive(const Node &node)
    {
        const RpfEntry &entry = node.entry;
        if (node.pending || entry.kind != RpfEntryKind::Binary || entry.storedSize != 0 || entry.uncompressedSize < resourceHeaderSize || !endsWith(entry.name, ".rpf"))
            return false;
        const auto header = transaction_.readAt(file_, levels_.back().base + entry.offset, resourceHeaderSize);
        if (!header)
            return std::unexpected(header.error());
        const auto word = [&header](std::size_t offset) {
            std::uint32_t value = 0;
            for (std::size_t i = 0; i < 4; ++i)
                value |= std::to_integer<std::uint32_t>((*header)[offset + i]) << (8 * i);
            return value;
        };
        const std::uint32_t tag = word(12);
        return word(0) == archiveMagic && tag != 0 && tag != openTag && tag != aesTag;
    }

    Result<std::uint64_t> occupied(const Level &level, const Node &node)
    {
        return occupiedSize(node.entry, [&](std::size_t count) { return transaction_.readAt(file_, level.base + node.entry.offset, count); });
    }

    Result<void> store(const Level &level, Node &node)
    {
        auto content = readSource(node.source);
        if (!content)
            return std::unexpected(content.error());
        RpfEntry &entry = node.entry;
        entry.encrypted = false;
        entry.systemFlags = 0;
        entry.graphicsFlags = 0;
        entry.uncompressedSize = 0;
        entry.storedSize = 0;
        if (isResourceFile(*content)) {
            const auto header = parseRsc7Header(std::span(*content).first(resourceHeaderSize));
            if (!header)
                return std::unexpected(header.error());
            entry.kind = RpfEntryKind::Resource;
            entry.systemFlags = header->systemFlags;
            entry.graphicsFlags = header->graphicsFlags;
            const std::size_t size = content->size();
            entry.storedSize = static_cast<std::uint32_t>(std::min<std::size_t>(size, rpfSizeInHeaderMarker));
            if (size >= rpfSizeInHeaderMarker) {
                (*content)[2] = static_cast<std::byte>(size >> 24);
                (*content)[5] = static_cast<std::byte>(size >> 16);
                (*content)[14] = static_cast<std::byte>(size >> 8);
                (*content)[7] = static_cast<std::byte>(size);
            }
        } else {
            entry.kind = RpfEntryKind::Binary;
            entry.uncompressedSize = static_cast<std::uint32_t>(content->size());
            if (!node.keepRaw && !isStoredRaw(entry.name)) {
                auto deflated = compressRaw(*content);
                if (!deflated.empty() && deflated.size() < content->size() && deflated.size() < rpfSizeInHeaderMarker) {
                    entry.storedSize = static_cast<std::uint32_t>(deflated.size());
                    *content = std::move(deflated);
                }
            }
        }
        const std::uint64_t target = aligned(end_);
        if (auto written = transaction_.writeAt(file_, target, *content); !written)
            return written;
        entry.offset = target - level.base;
        end_ = target + content->size();
        node.pending = false;
        return {};
    }

    Result<void> storePending(const Level &level, Node &node, const std::string &path)
    {
        if (node.pending) {
            written_.emplace_back(path, node.source);
            return store(level, node);
        }
        for (Node &child : node.children) {
            if (auto stored = storePending(level, child, path.empty() ? child.entry.name : path + '/' + child.entry.name); !stored)
                return stored;
        }
        return {};
    }

    Result<void> finish()
    {
        for (std::size_t index = levels_.size(); index-- > 0;) {
            Level &level = levels_[index];
            if (index + 1 < levels_.size()) {
                RpfEntry &nested = findNode(level.root, levels_[index + 1].pathInParent)->entry;
                nested.storedSize = 0;
                nested.uncompressedSize = static_cast<std::uint32_t>(levels_[index + 1].size);
            }
            Flat flat = flatten(level.root);
            const auto sizing = serializeToc(flat.entries);
            if (!sizing)
                return std::unexpected(sizing.error());
            const std::uint64_t dataStart = aligned(sizing->size());
            end_ = std::max(end_, level.base + dataStart);
            for (Node *node : flat.nodes) {
                if (node->entry.kind == RpfEntryKind::Directory || node->pending || node->entry.offset >= dataStart)
                    continue;
                const auto size = occupied(level, *node);
                if (!size)
                    return std::unexpected(size.error());
                if (*size == 0) {
                    node->entry.offset = dataStart;
                    continue;
                }
                const auto target = appendCopy(level.base + node->entry.offset, *size);
                if (!target)
                    return std::unexpected(target.error());
                node->entry.offset = *target - level.base;
            }
            if (auto stored = storePending(level, level.root, {}); !stored)
                return stored;
            if (end_ % rpfBlockSize != 0) {
                const std::vector<std::byte> padding(aligned(end_) - end_);
                if (auto written = transaction_.writeAt(file_, end_, padding); !written)
                    return written;
                end_ = aligned(end_);
            }
            const auto fileSize = transaction_.sizeOf(file_);
            if (!fileSize)
                return std::unexpected(fileSize.error());
            if (*fileSize < end_) {
                if (auto written = transaction_.writeAt(file_, end_ - 1, std::vector<std::byte>(1)); !written)
                    return written;
            }
            level.size = end_ - level.base;
            if (index > 0 && level.size > maxNestedSize)
                return failure("archive would exceed 4 GB; rebuild it first");
            const auto toc = serializeToc(flatten(level.root).entries);
            if (!toc)
                return std::unexpected(toc.error());
            if (auto written = transaction_.writeAt(file_, level.base, *toc); !written)
                return written;
        }
        return {};
    }

    Result<void> verify(std::span<const std::string> nested) const
    {
        const auto reader = openChain(nested, [](const RpfReader &, std::string_view, std::uint64_t, std::uint64_t) {});
        if (!reader)
            return std::unexpected(reader.error());
        for (const auto &[path, source] : written_) {
            const auto index = reader->archive().find(path);
            const auto expected = readSource(source);
            if (!index || !expected)
                return failure("verification failed: " + path + " is missing");
            const RpfEntry &entry = reader->archive().entries()[*index];
            bool same = false;
            if (isResourceFile(*expected)) {
                const auto stored = reader->storedFile(*index);
                const auto header = parseRsc7Header(std::span(*expected).first(resourceHeaderSize));
                same = stored && header && entry.kind == RpfEntryKind::Resource
                    && entry.systemFlags == header->systemFlags && entry.graphicsFlags == header->graphicsFlags
                    && std::ranges::equal(std::span(*stored).subspan(resourceHeaderSize), std::span(*expected).subspan(resourceHeaderSize));
            } else {
                const auto data = reader->fileData(*index);
                same = data && *data == *expected;
            }
            if (!same)
                return failure("verification failed: " + path + " does not match its source");
        }
        return {};
    }

    Transaction &transaction_;
    std::filesystem::path file_;
    const Keys *keys_;
    std::vector<Level> levels_;
    std::uint64_t end_ = 0;
    std::vector<std::pair<std::string, std::filesystem::path>> written_;
};

class Rebuilder {
public:
    Rebuilder(std::shared_ptr<RandomAccessSource> source, WritableFile &out, const Keys *keys)
        : source_(std::move(source))
        , out_(out)
        , keys_(keys)
    {
    }

    Result<std::uint64_t> write(const RpfReader &reader, std::uint64_t sourceBase, std::uint64_t target, bool nested)
    {
        const RpfArchive &archive = reader.archive();
        const bool aes = archive.encryption() == RpfEncryption::Aes;
        if (aes && hasScripts(buildTree(archive.entries(), 0)))
            return failure("scripts in an AES archive cannot be re-encrypted");
        std::vector<RpfEntry> entries = archive.entries();
        const auto sizing = serializeToc(entries);
        if (!sizing)
            return std::unexpected(sizing.error());
        const std::uint64_t dataStart = aligned(sizing->size());
        std::uint64_t cursor = target + dataStart;
        for (std::uint32_t index = 0; index < entries.size(); ++index) {
            RpfEntry &entry = entries[index];
            if (entry.kind == RpfEntryKind::Directory)
                continue;
            const std::uint64_t from = sourceBase + entry.offset;
            const auto size = occupiedSize(entry, [&](std::size_t count) { return source_->read(from, count); });
            if (!size)
                return std::unexpected(size.error());
            if (*size == 0) {
                entry.offset = dataStart;
                continue;
            }
            entry.offset = cursor - target;
            if (entry.kind == RpfEntryKind::Binary && entry.storedSize == 0 && !entry.encrypted && endsWith(entry.name, ".rpf")) {
                if (const auto inner = reader.openNested(index)) {
                    const auto written = write(*inner, from, cursor, true);
                    if (!written)
                        return written;
                    entry.uncompressedSize = static_cast<std::uint32_t>(*written);
                    cursor += *written;
                    continue;
                }
            }
            const bool decrypt = aes && entry.kind == RpfEntryKind::Binary && entry.encrypted;
            if (auto copied = copy(from, cursor, *size, decrypt); !copied)
                return std::unexpected(copied.error());
            entry.encrypted = entry.encrypted && !decrypt;
            cursor = aligned(cursor + *size);
        }
        const std::uint64_t total = cursor - target;
        if (nested && total > maxNestedSize)
            return failure("archive would exceed 4 GB");
        const auto toc = serializeToc(entries);
        if (!toc)
            return std::unexpected(toc.error());
        if (auto written = out_.write(target, *toc); !written)
            return std::unexpected(written.error());
        return total;
    }

private:
    Result<void> copy(std::uint64_t from, std::uint64_t to, std::uint64_t size, bool decrypt)
    {
        for (std::uint64_t done = 0; done < size;) {
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(copyChunk, size - done));
            auto bytes = source_->read(from + done, chunk);
            if (!bytes)
                return std::unexpected(bytes.error());
            if (decrypt) {
                if (keys_ == nullptr)
                    return failure("archive is encrypted and no keys are loaded");
                bytes = aesEcbDecrypt(*bytes, keys_->aes);
                if (!bytes)
                    return std::unexpected(bytes.error());
            }
            if (auto written = out_.write(to + done, *bytes); !written)
                return written;
            done += chunk;
        }
        return {};
    }

    std::shared_ptr<RandomAccessSource> source_;
    WritableFile &out_;
    const Keys *keys_;
};

}

Result<void> editArchive(Transaction &transaction, const std::filesystem::path &file, std::span<const std::string> nested, std::span<const ArchiveEdit> edits, const Keys *keys)
{
    return Editor(transaction, file, keys).run(nested, edits);
}

Result<void> rebuildArchive(Transaction &transaction, const std::filesystem::path &file, const Keys *keys)
{
    return transaction.replaceFile(file, [&](const std::filesystem::path &temporary) -> Result<void> {
        auto source = openFileSource(file);
        const auto reader = RpfReader::open(source, file.filename().string(), keys);
        if (!reader)
            return std::unexpected(reader.error());
        auto out = WritableFile::open(temporary, true);
        if (!out)
            return std::unexpected(out.error());
        const auto total = Rebuilder(std::move(source), *out, keys).write(*reader, 0, 0, false);
        if (!total)
            return std::unexpected(total.error());
        return out->resize(*total);
    });
}

}
