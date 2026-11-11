#include "support/rpf_builder.h"

#include "support/test_bytes.h"

namespace archon::test {

namespace {

void put(std::vector<std::byte> &out, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i)
        out.push_back(static_cast<std::byte>(value >> (8 * i)));
}

void padTo(std::vector<std::byte> &out, std::size_t alignment)
{
    out.resize((out.size() + alignment - 1) / alignment * alignment);
}

}

RpfBuilder::Node &RpfBuilder::node(const std::string &path)
{
    Node *current = &root_;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string name = path.substr(start, end - start);
        current = &current->children[name];
        current->name = name;
        start = end + 1;
    }
    return *current;
}

void RpfBuilder::addDirectory(const std::string &path)
{
    node(path);
}

void RpfBuilder::addBinary(const std::string &path, std::vector<std::byte> bytes, bool compress)
{
    Node &target = node(path);
    target.kind = RpfEntryKind::Binary;
    target.uncompressedSize = static_cast<std::uint32_t>(bytes.size());
    target.stored = compress ? deflateRaw(bytes) : std::move(bytes);
    target.storedSize = compress ? static_cast<std::uint32_t>(target.stored.size()) : 0;
}

void RpfBuilder::addResource(const std::string &path, const std::vector<std::byte> &pageData, std::uint32_t systemFlags, std::uint32_t graphicsFlags)
{
    Node &target = node(path);
    target.kind = RpfEntryKind::Resource;
    target.systemFlags = systemFlags;
    target.graphicsFlags = graphicsFlags;
    put(target.stored, 0x37435352, 4);
    put(target.stored, ((graphicsFlags >> 28) & 0xF) | (((systemFlags >> 28) & 0xF) << 4), 4);
    put(target.stored, systemFlags, 4);
    put(target.stored, graphicsFlags, 4);
    const auto body = deflateRaw(pageData);
    target.stored.insert(target.stored.end(), body.begin(), body.end());
    target.storedSize = static_cast<std::uint32_t>(target.stored.size());
}

std::vector<std::byte> RpfBuilder::build(RpfEncryption encryption) const
{
    std::vector<const Node *> order { &root_ };
    std::vector<std::uint32_t> firstChild;
    for (std::size_t i = 0; i < order.size(); ++i) {
        firstChild.push_back(static_cast<std::uint32_t>(order.size()));
        for (const auto &child : order[i]->children)
            order.push_back(&child.second);
    }

    std::vector<std::byte> names;
    std::vector<std::uint32_t> nameOffsets;
    for (const Node *entry : order) {
        nameOffsets.push_back(static_cast<std::uint32_t>(names.size()));
        const auto text = asBytes(entry->name);
        names.insert(names.end(), text.begin(), text.end());
        names.push_back(std::byte { 0 });
    }
    padTo(names, 16);

    std::vector<std::byte> data;
    std::vector<std::uint32_t> blocks(order.size(), 0);
    const std::size_t tocSize = 16 + order.size() * 16 + names.size();
    const std::size_t dataStart = (tocSize + 511) / 512 * 512;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i]->kind == RpfEntryKind::Directory)
            continue;
        padTo(data, 512);
        blocks[i] = static_cast<std::uint32_t>((dataStart + data.size()) / 512);
        data.insert(data.end(), order[i]->stored.begin(), order[i]->stored.end());
    }

    std::vector<std::byte> out;
    put(out, 0x52504637, 4);
    put(out, order.size(), 4);
    put(out, names.size(), 4);
    put(out, encryption == RpfEncryption::Open ? 0x4E45504F : 0, 4);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Node &entry = *order[i];
        if (entry.kind == RpfEntryKind::Directory) {
            put(out, nameOffsets[i], 4);
            put(out, 0x7FFFFF00, 4);
            put(out, firstChild[i], 4);
            put(out, entry.children.size(), 4);
            continue;
        }
        put(out, nameOffsets[i], 2);
        put(out, entry.storedSize, 3);
        put(out, entry.kind == RpfEntryKind::Resource ? blocks[i] | 0x800000 : blocks[i], 3);
        put(out, entry.kind == RpfEntryKind::Resource ? entry.systemFlags : entry.uncompressedSize, 4);
        put(out, entry.kind == RpfEntryKind::Resource ? entry.graphicsFlags : 0, 4);
    }
    out.insert(out.end(), names.begin(), names.end());
    padTo(out, 512);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

}
