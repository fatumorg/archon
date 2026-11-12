#include "core/meta/meta_xml.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <stdfloat>

namespace archon {

namespace {

constexpr std::size_t maxDepth = 64;
constexpr std::size_t maxNodes = 20'000'000;
constexpr std::size_t maxOutput = std::size_t(1) << 30;
constexpr std::uint32_t maxCount = 0xFFFF;
constexpr const char *itemName = "Item";
constexpr std::string_view nullType = "NULL";
constexpr std::array<std::string_view, 12> fieldAttributes { "value", "x", "y", "z", "w", "type", "capacity", "count", "length", "flag", "key", "empty" };

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "meta", 0, std::move(message) });
}

std::uint64_t readNumber(std::span<const std::byte> data, std::size_t at, std::size_t size, bool bigEndian)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        const auto byte = std::to_integer<std::uint64_t>(data[at + index]);
        value |= bigEndian ? byte << (8 * (size - 1 - index)) : byte << (8 * index);
    }
    return value;
}

void writeNumber(std::vector<std::byte> &data, std::size_t at, std::uint64_t value, std::size_t size, bool bigEndian)
{
    for (std::size_t index = 0; index < size; ++index) {
        const std::size_t shift = bigEndian ? 8 * (size - 1 - index) : 8 * index;
        data[at + index] = static_cast<std::byte>((value >> shift) & 0xFF);
    }
}

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

template <typename T>
std::optional<T> parseInteger(std::string_view text)
{
    T value {};
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<float> parseFloat(std::string_view text)
{
    if (text.starts_with("0x")) {
        const auto bits = parseInteger<std::uint32_t>(text);
        if (!bits)
            return std::nullopt;
        return std::bit_cast<float>(*bits);
    }
    float value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::string hexText(std::span<const std::byte> bytes)
{
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::byte byte : bytes)
        text += std::format("{:02X}", std::to_integer<unsigned>(byte));
    return text;
}

std::optional<std::vector<std::byte>> parseHex(std::string_view text)
{
    if (text.size() % 2 != 0)
        return std::nullopt;
    std::vector<std::byte> bytes;
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const auto value = parseInteger<std::uint8_t>(std::string("0x") + std::string(text.substr(index, 2)));
        if (!value)
            return std::nullopt;
        bytes.push_back(static_cast<std::byte>(*value));
    }
    return bytes;
}

class Reader {
public:
    Reader(const MetaData &data, const NameTable &names)
        : data_(data)
        , names_(names)
    {
        std::size_t bytes = 0;
        for (const MetaBlock &block : data.blocks)
            bytes += block.data.size();
        nodeBudget_ = std::min(maxNodes, 4 * bytes + 4096);
    }

    Result<void> run(pugi::xml_document &document)
    {
        if (data_.root == 0 || data_.root > data_.blocks.size())
            return failure("the root block does not exist");
        const MetaBlock &root = data_.blocks[data_.root - 1];
        const MetaStruct *type = structure(root.type);
        if (type == nullptr)
            return failure("the root structure " + hashText(root.type) + " is not described in this file");
        if (type->size > root.data.size())
            return failure("the root structure is larger than its block");
        pugi::xml_node node = document.append_child(names_.name(root.type).c_str());
        return readStruct(*type, data_.root - 1, 0, node, 0);
    }

private:
    const MetaStruct *structure(std::uint32_t name) const
    {
        const auto found = data_.schema.structs.find(name);
        return found == data_.schema.structs.end() ? nullptr : &found->second;
    }

    std::uint64_t number(std::size_t block, std::size_t at, std::size_t size) const
    {
        return readNumber(data_.blocks[block].data, at, size, data_.schema.bigEndian);
    }

    float floatAt(std::size_t block, std::size_t at) const
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(number(block, at, 4)));
    }

    Result<std::optional<std::pair<std::size_t, std::size_t>>> target(std::size_t block, std::size_t at, std::uint64_t length) const
    {
        const auto pointer = static_cast<std::uint32_t>(number(block, at, 4));
        const std::uint32_t index = pointer & 0xFFF;
        const std::uint32_t offset = pointer >> 12;
        if (index == 0)
            return std::optional<std::pair<std::size_t, std::size_t>>();
        if (index > data_.blocks.size())
            return failure("a pointer names block " + std::to_string(index) + ", which does not exist");
        if (offset + length > data_.blocks[index - 1].data.size())
            return failure("a pointer leads outside block " + std::to_string(index));
        return std::optional(std::pair<std::size_t, std::size_t>(index - 1, offset));
    }

    Result<void> readStruct(const MetaStruct &type, std::size_t block, std::size_t at, pugi::xml_node node, std::size_t depth)
    {
        if (depth > maxDepth)
            return failure("the data is nested too deeply");
        for (const std::size_t index : type.members) {
            const MetaField &field = type.entries[index];
            pugi::xml_node child = node.append_child(names_.name(field.name).c_str());
            if (const auto read = readField(type, field, block, at + field.offset, child, depth); !read)
                return read;
        }
        return {};
    }

    std::string enumText(std::uint32_t enumName, std::uint64_t value, std::size_t width) const
    {
        const std::uint64_t mask = width >= 8 ? ~0ull : (1ull << (8 * width)) - 1;
        if (const auto found = data_.schema.enums.find(enumName); found != data_.schema.enums.end()) {
            for (const auto &[hash, entry] : found->second.values) {
                if ((static_cast<std::uint64_t>(static_cast<std::int64_t>(entry)) & mask) == value)
                    return names_.name(hash);
            }
        }
        return std::to_string(value);
    }

    std::string flagsText(std::uint32_t enumName, std::uint64_t value) const
    {
        std::string text;
        if (const auto found = data_.schema.enums.find(enumName); found != data_.schema.enums.end()) {
            for (const auto &[hash, bit] : found->second.values) {
                if (bit < 0 || bit > 63 || (value & (1ull << bit)) == 0)
                    continue;
                text += (text.empty() ? "" : " ") + names_.name(hash);
                value &= ~(1ull << bit);
            }
        }
        if (value != 0 || text.empty())
            text += (text.empty() ? "" : " ") + std::to_string(value);
        return text;
    }

    Result<std::string> scalarText(const MetaField &field, std::size_t block, std::size_t at) const
    {
        switch (field.type) {
        case MetaType::Hash:
            return names_.value(static_cast<std::uint32_t>(number(block, at, 4)));
        case MetaType::Int32:
            return std::to_string(static_cast<std::int32_t>(number(block, at, 4)));
        case MetaType::UInt32:
            return std::to_string(number(block, at, 4));
        default:
            return failure("a map key of this kind is not supported");
        }
    }

    Result<void> readField(const MetaStruct &owner, const MetaField &field, std::size_t block, std::size_t at, pugi::xml_node node, std::size_t depth)
    {
        if (depth > maxDepth)
            return failure("the data is nested too deeply");
        if (++nodes_ > nodeBudget_)
            return failure("the data holds too many values");
        const std::vector<std::byte> &bytes = data_.blocks[block].data;
        if (at + field.size > bytes.size())
            return failure("a value of " + names_.name(field.name) + " lies outside its block");
        const auto attribute = [&node](const char *name, const std::string &value) { node.append_attribute(name).set_value(value.c_str()); };
        switch (field.type) {
        case MetaType::Bool: {
            const auto value = number(block, at, 1);
            attribute("value", value == 0 ? "false" : value == 1 ? "true" : std::to_string(value));
            return {};
        }
        case MetaType::Int8:
            attribute("value", std::to_string(static_cast<std::int8_t>(number(block, at, 1))));
            return {};
        case MetaType::UInt8:
            attribute("value", std::to_string(number(block, at, 1)));
            return {};
        case MetaType::Int16:
            attribute("value", std::to_string(static_cast<std::int16_t>(number(block, at, 2))));
            return {};
        case MetaType::UInt16:
            attribute("value", std::to_string(number(block, at, 2)));
            return {};
        case MetaType::Int32:
            attribute("value", std::to_string(static_cast<std::int32_t>(number(block, at, 4))));
            return {};
        case MetaType::UInt32:
            attribute("value", std::to_string(number(block, at, 4)));
            return {};
        case MetaType::Color:
            attribute("value", std::format("0x{:08X}", number(block, at, 4)));
            return {};
        case MetaType::Int64:
            attribute("value", std::to_string(static_cast<std::int64_t>(number(block, at, 8))));
            return {};
        case MetaType::Float:
            attribute("value", shortestFloat(floatAt(block, at)));
            return {};
        case MetaType::Half:
            attribute("value", shortestFloat(static_cast<float>(std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(number(block, at, 2))))));
            return {};
        case MetaType::Vector2:
        case MetaType::Vector3:
        case MetaType::Vector4: {
            const int components = field.type == MetaType::Vector2 ? 2 : field.type == MetaType::Vector3 ? 3 : 4;
            for (int index = 0; index < components; ++index)
                attribute(std::array { "x", "y", "z", "w" }[index], shortestFloat(floatAt(block, at + 4 * index)));
            if (field.type == MetaType::Vector3 && field.size >= 16 && number(block, at + 12, 4) != 0)
                attribute("w", shortestFloat(floatAt(block, at + 12)));
            return {};
        }
        case MetaType::Hash: {
            const auto hash = static_cast<std::uint32_t>(number(block, at, 4));
            if (hash != 0)
                node.text().set(names_.value(hash).c_str());
            return {};
        }
        case MetaType::FixedString: {
            const auto text = std::span(bytes).subspan(at, field.size);
            const auto end = std::ranges::find(text, std::byte { 0 });
            const std::string value = latin1ToUtf8(std::span(text.begin(), end));
            if (!value.empty())
                node.text().set(value.c_str());
            return {};
        }
        case MetaType::CString:
        case MetaType::AtString: {
            const bool counted = field.type == MetaType::AtString;
            const std::uint64_t length = counted ? number(block, at + 8, 2) : 0;
            const std::uint64_t capacity = counted ? number(block, at + 10, 2) : 0;
            const auto found = target(block, at, counted ? length : 1);
            if (!found)
                return std::unexpected(found.error());
            if (!*found) {
                if (length != 0 || capacity != 0)
                    attribute("capacity", std::to_string(capacity));
                return {};
            }
            const auto [targetBlock, offset] = **found;
            const auto &text = data_.blocks[targetBlock].data;
            std::size_t end = offset;
            const std::size_t limit = counted ? offset + length : text.size();
            while (end < limit && text[end] != std::byte { 0 })
                ++end;
            if (!counted && end == text.size())
                return failure("a string runs past the end of its block");
            const std::string value = latin1ToUtf8(std::span(text).subspan(offset, end - offset));
            if (value.empty())
                attribute("empty", "true");
            else
                node.text().set(value.c_str());
            if (counted && end - offset != length)
                attribute("length", std::to_string(length));
            if (counted && capacity != length + 1)
                attribute("capacity", std::to_string(capacity));
            return {};
        }
        case MetaType::Struct: {
            const MetaStruct *type = structure(field.reference);
            if (type == nullptr)
                return failure("the structure " + names_.name(field.reference) + " is not described in this file");
            return readStruct(*type, block, at, node, depth + 1);
        }
        case MetaType::StructPointer: {
            const auto found = target(block, at, 0);
            if (!found)
                return std::unexpected(found.error());
            if (!*found) {
                attribute("type", std::string(nullType));
                return {};
            }
            const auto [targetBlock, offset] = **found;
            const MetaStruct *type = structure(data_.blocks[targetBlock].type);
            if (type == nullptr || offset + type->size > data_.blocks[targetBlock].data.size())
                return failure("a structure pointer leads to data that is not a described structure");
            attribute("type", names_.name(type->name));
            return readStruct(*type, targetBlock, offset, node, depth + 1);
        }
        case MetaType::Array:
        case MetaType::PointerArray: {
            const bool counted = field.type == MetaType::Array;
            const std::uint64_t count = counted ? number(block, at + 8, 2) : field.count;
            const std::uint64_t capacity = counted ? number(block, at + 10, 2) : field.count;
            if (counted && capacity != count)
                attribute("capacity", std::to_string(capacity));
            if (count == 0)
                return {};
            const MetaField &element = owner.entries[static_cast<std::size_t>(field.element)];
            if (element.size == 0)
                return failure("the items of " + names_.name(field.name) + " are not described in this file");
            const auto found = target(block, at, count * element.size);
            if (!found)
                return std::unexpected(found.error());
            if (!*found) {
                if (counted)
                    attribute("count", std::to_string(count));
                return {};
            }
            const auto [targetBlock, offset] = **found;
            for (std::uint64_t index = 0; index < count; ++index) {
                if (const auto read = readField(owner, element, targetBlock, offset + index * element.size, node.append_child(itemName), depth + 1); !read)
                    return read;
            }
            return {};
        }
        case MetaType::FixedArray:
        case MetaType::CountedArray: {
            const MetaField &element = owner.entries[static_cast<std::size_t>(field.element)];
            std::uint64_t count = field.count;
            if (field.type == MetaType::CountedArray) {
                const std::size_t countAt = at + std::size_t(field.count) * element.size;
                if (countAt + 4 > data_.blocks[block].data.size())
                    return failure("the count of " + names_.name(field.name) + " lies outside its block");
                count = number(block, countAt, 4);
                if (count > field.count)
                    return failure(names_.name(field.name) + " counts more items than it holds");
            }
            for (std::uint64_t index = 0; index < count; ++index) {
                if (const auto read = readField(owner, element, block, at + index * element.size, node.append_child(itemName), depth + 1); !read)
                    return read;
            }
            return {};
        }
        case MetaType::Map: {
            const auto flag = number(block, at, 1);
            if (flag != 1)
                attribute("flag", std::to_string(flag));
            const std::uint64_t count = number(block, at + 16, 2);
            const std::uint64_t capacity = number(block, at + 18, 2);
            if (capacity != count)
                attribute("capacity", std::to_string(capacity));
            if (count == 0)
                return {};
            const MetaStruct *pair = structure(field.reference);
            if (pair == nullptr || pair->members.size() != 2)
                return failure("the entries of " + names_.name(field.name) + " are not described in this file");
            const auto found = target(block, at + 8, count * pair->size);
            if (!found)
                return std::unexpected(found.error());
            if (!*found)
                return failure(names_.name(field.name) + " counts entries but has no data");
            const auto [targetBlock, offset] = **found;
            const MetaField &key = pair->entries[pair->members[0]];
            const MetaField &value = pair->entries[pair->members[1]];
            for (std::uint64_t index = 0; index < count; ++index) {
                const std::size_t base = offset + index * pair->size;
                pugi::xml_node item = node.append_child(itemName);
                const auto keyText = scalarText(key, targetBlock, base + key.offset);
                if (!keyText)
                    return std::unexpected(keyText.error());
                item.append_attribute("key").set_value(keyText->c_str());
                if (const auto read = readField(*pair, value, targetBlock, base + value.offset, item, depth + 1); !read)
                    return read;
            }
            return {};
        }
        case MetaType::Enum: {
            const std::string text = enumText(field.reference, number(block, at, field.size), field.size);
            node.text().set(text.c_str());
            return {};
        }
        case MetaType::Flags: {
            const std::string text = flagsText(field.reference, number(block, at, field.size));
            node.text().set(text.c_str());
            return {};
        }
        case MetaType::Raw:
            node.text().set(hexText(std::span(bytes).subspan(at, field.size)).c_str());
            return {};
        }
        return failure("a field has an unknown kind");
    }

    const MetaData &data_;
    const NameTable &names_;
    std::size_t nodes_ = 0;
    std::size_t nodeBudget_ = 0;
};

class Writer {
public:
    Writer(const MetaData &original, std::string_view source, const NameTable &names)
        : schema_(original.schema)
        , source_(source)
        , names_(names)
    {
        for (const auto &[hash, type] : schema_.structs)
            structNames_.emplace(names_.name(hash), hash);
    }

    Result<MetaData> run(const pugi::xml_document &document, std::uint32_t rootType)
    {
        const pugi::xml_node root = document.document_element();
        for (pugi::xml_node top = document.first_child(); top; top = top.next_sibling()) {
            if (top.type() == pugi::node_element && top != root)
                return failure("the document holds more than one root element");
        }
        if (!root || names_.name(rootType) != root.name())
            return failure("the root element must be <" + names_.name(rootType) + ">");
        const auto found = schema_.structs.find(rootType);
        if (found == schema_.structs.end())
            return failure("the root structure is not described in this file");
        const MetaStruct &type = found->second;
        const auto allocated = allocate(rootType, type.size, type.align, false);
        if (!allocated)
            return std::unexpected(allocated.error());
        if (const auto written = writeStruct(type, allocated->first, allocated->second, root); !written)
            return std::unexpected(written.error());
        return finish();
    }

private:
    struct Block {
        std::uint32_t type;
        std::vector<std::byte> data;
        std::size_t capacity;
        bool own;
    };

    struct Fixup {
        std::size_t block;
        std::size_t at;
        std::size_t target;
        std::size_t offset;
    };

    std::size_t line(pugi::xml_node node) const
    {
        const auto offset = static_cast<std::size_t>(std::max<std::ptrdiff_t>(node.offset_debug(), 0));
        return 1 + static_cast<std::size_t>(std::count(source_.begin(), source_.begin() + static_cast<std::ptrdiff_t>(std::min(offset, source_.size())), '\n'));
    }

    std::unexpected<Error> at(pugi::xml_node node, const std::string &message) const
    {
        return failure("<" + std::string(node.name()) + "> on line " + std::to_string(line(node)) + ": " + message);
    }

    Result<std::pair<std::size_t, std::size_t>> allocate(std::uint32_t type, std::size_t size, std::uint32_t align, bool own)
    {
        total_ += size + align;
        if (total_ > maxOutput)
            return failure("the data would be larger than 1 GiB");
        auto &list = byType_[type];
        if (list.empty())
            typeOrder_.push_back(type);
        if (!own) {
            for (const std::size_t index : list) {
                Block &block = blocks_[index];
                const std::size_t offset = alignUp(block.data.size(), align);
                if (block.capacity != 0 && offset + size > block.capacity)
                    continue;
                block.data.resize(offset + size);
                return std::pair(index, offset);
            }
        }
        const std::size_t capacity = own ? size : schema_.blockCapacity == 0 ? 0 : std::max<std::size_t>(schema_.blockCapacity, size);
        blocks_.push_back(Block { type, std::vector<std::byte>(size), capacity, own });
        list.push_back(blocks_.size() - 1);
        return std::pair(blocks_.size() - 1, std::size_t(0));
    }

    void put(std::size_t block, std::size_t offset, std::uint64_t value, std::size_t size)
    {
        writeNumber(blocks_[block].data, offset, value, size, schema_.bigEndian);
    }

    Result<MetaData> finish()
    {
        const auto order = hashTableOrder(typeOrder_);
        std::vector<std::size_t> index(blocks_.size());
        MetaData result;
        result.schema = schema_;
        for (const bool own : { false, true }) {
            for (const std::uint32_t type : order) {
                for (const std::size_t block : byType_.at(type)) {
                    if (blocks_[block].own != own)
                        continue;
                    index[block] = result.blocks.size() + 1;
                    result.blocks.push_back(MetaBlock { type, {}, own });
                }
            }
        }
        if (result.blocks.size() > 0xFFF)
            return failure("the data would need more than 4095 blocks");
        for (const Fixup &fixup : fixups_) {
            if (fixup.offset > 0xFFFFF)
                return failure("the data would place a value beyond the reach of a pointer");
            put(fixup.block, fixup.at, static_cast<std::uint32_t>(fixup.offset << 12) | static_cast<std::uint32_t>(index[fixup.target]), 4);
        }
        for (std::size_t block = 0; block < blocks_.size(); ++block)
            result.blocks[index[block] - 1].data = std::move(blocks_[block].data);
        result.root = static_cast<std::uint32_t>(index[0]);
        return result;
    }

    Result<void> writeStruct(const MetaStruct &type, std::size_t block, std::size_t base, pugi::xml_node node)
    {
        if (++depth_ > maxDepth)
            return at(node, "the data is nested too deeply");
        pugi::xml_node child = node.first_child();
        for (const std::size_t index : type.members) {
            while (child && child.type() != pugi::node_element)
                child = child.next_sibling();
            const MetaField &field = type.entries[index];
            const std::string expected = names_.name(field.name);
            if (!child)
                return at(node, "<" + expected + "> is missing");
            if (expected != child.name())
                return at(child, "expected <" + expected + "> here");
            if (const auto written = writeField(type, field, block, base + field.offset, child); !written)
                return written;
            child = child.next_sibling();
        }
        while (child && child.type() != pugi::node_element)
            child = child.next_sibling();
        if (child)
            return at(child, "is not a field of " + names_.name(type.name));
        --depth_;
        return {};
    }

    Result<std::string> attribute(pugi::xml_node node, const char *name) const
    {
        const pugi::xml_attribute found = node.attribute(name);
        if (!found)
            return at(node, std::string("needs a ") + name + " attribute");
        return std::string(found.value());
    }

    template <typename T>
    Result<T> integerAttribute(pugi::xml_node node, const char *name) const
    {
        const auto text = attribute(node, name);
        if (!text)
            return std::unexpected(text.error());
        const auto value = parseInteger<T>(trim(*text));
        if (!value)
            return at(node, std::string(name) + " \"" + *text + "\" is not a valid number");
        return *value;
    }

    Result<float> floatAttribute(pugi::xml_node node, const char *name) const
    {
        const auto text = attribute(node, name);
        if (!text)
            return std::unexpected(text.error());
        const auto value = parseFloat(trim(*text));
        if (!value)
            return at(node, std::string(name) + " \"" + *text + "\" is not a valid number");
        return *value;
    }

    Result<std::uint32_t> hashValue(pugi::xml_node node, std::string_view text) const
    {
        const auto hash = names_.valueHash(trim(text));
        if (!hash)
            return at(node, "\"" + std::string(text) + "\" is not a valid name or hash");
        return *hash;
    }

    Result<std::uint64_t> enumValue(pugi::xml_node node, std::uint32_t enumName, std::string_view text, std::size_t width) const
    {
        const std::uint64_t mask = width >= 8 ? ~0ull : (1ull << (8 * width)) - 1;
        const std::string name = trim(text);
        if (const auto found = schema_.enums.find(enumName); found != schema_.enums.end()) {
            for (const auto &[hash, value] : found->second.values) {
                if (names_.name(hash) == name)
                    return static_cast<std::uint64_t>(static_cast<std::int64_t>(value)) & mask;
            }
        }
        const auto number = parseInteger<std::uint64_t>(name);
        if (!number)
            return at(node, "\"" + name + "\" is not a value of " + names_.name(enumName));
        return *number & mask;
    }

    Result<std::uint64_t> flagsValue(pugi::xml_node node, std::uint32_t enumName, std::string_view text) const
    {
        std::uint64_t result = 0;
        const std::string all = trim(text);
        std::size_t start = 0;
        while (start < all.size()) {
            std::size_t end = all.find_first_of(" \t\r\n", start);
            if (end == std::string::npos)
                end = all.size();
            const std::string token = all.substr(start, end - start);
            start = all.find_first_not_of(" \t\r\n", end);
            if (start == std::string::npos)
                start = all.size();
            if (token.empty())
                continue;
            bool named = false;
            if (const auto found = schema_.enums.find(enumName); found != schema_.enums.end()) {
                for (const auto &[hash, bit] : found->second.values) {
                    if (names_.name(hash) == token && bit >= 0 && bit < 64) {
                        result |= 1ull << bit;
                        named = true;
                        break;
                    }
                }
            }
            if (named)
                continue;
            const auto number = parseInteger<std::uint64_t>(token);
            if (!number)
                return at(node, "\"" + token + "\" is not a flag of " + names_.name(enumName));
            result |= *number;
        }
        return result;
    }

    Result<void> writeScalar(const MetaField &field, std::size_t block, std::size_t offset, pugi::xml_node node, std::string_view text)
    {
        switch (field.type) {
        case MetaType::Hash: {
            const auto hash = hashValue(node, text);
            if (!hash)
                return std::unexpected(hash.error());
            put(block, offset, *hash, 4);
            return {};
        }
        case MetaType::Int32:
        case MetaType::UInt32: {
            const auto value = field.type == MetaType::Int32 ? parseInteger<std::int32_t>(trim(text)).transform([](std::int32_t v) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)); }) : parseInteger<std::uint32_t>(trim(text)).transform([](std::uint32_t v) { return std::uint64_t(v); });
            if (!value)
                return at(node, "\"" + std::string(text) + "\" is not a valid number");
            put(block, offset, *value, 4);
            return {};
        }
        default:
            return at(node, "has a key of a kind that is not supported");
        }
    }

    Result<void> writeString(const MetaField &field, std::size_t block, std::size_t offset, pugi::xml_node node)
    {
        const bool counted = field.type == MetaType::AtString;
        const pugi::xml_attribute capacityAttribute = node.attribute("capacity");
        const bool hasText = (static_cast<bool>(node.first_child()) && node.first_child().type() == pugi::node_pcdata) || std::string_view(node.attribute("empty").value()) == "true";
        if (!hasText) {
            if (counted && capacityAttribute) {
                const auto capacity = parseInteger<std::uint16_t>(capacityAttribute.value());
                if (!capacity)
                    return at(node, "capacity is not a valid number");
                put(block, offset + 10, *capacity, 2);
            }
            return {};
        }
        auto bytes = utf8ToLatin1(node.child_value());
        if (!bytes)
            return at(node, bytes.error().message);
        std::size_t length = bytes->size();
        if (const pugi::xml_attribute declared = node.attribute("length"); counted && declared) {
            const auto parsed = parseInteger<std::uint16_t>(declared.value());
            if (!parsed || *parsed < bytes->size())
                return at(node, "length must be a number no smaller than the text");
            length = *parsed;
            bytes->resize(length);
        }
        std::size_t capacity = length + 1;
        if (counted && capacityAttribute) {
            const auto parsed = parseInteger<std::uint16_t>(capacityAttribute.value());
            if (!parsed)
                return at(node, "capacity is not a valid number");
            capacity = *parsed;
        }
        if (counted && (length > 0xFFFF || capacity > 0xFFFF))
            return at(node, "the text is longer than 65535 characters");
        const auto allocated = allocate(schema_.stringBlock, std::max(capacity, length), 1, false);
        if (!allocated)
            return std::unexpected(allocated.error());
        std::ranges::copy(*bytes, blocks_[allocated->first].data.begin() + static_cast<std::ptrdiff_t>(allocated->second));
        fixups_.push_back(Fixup { block, offset, allocated->first, allocated->second });
        if (counted) {
            put(block, offset + 8, length, 2);
            put(block, offset + 10, capacity, 2);
        }
        return {};
    }

    std::vector<pugi::xml_node> items(pugi::xml_node node) const
    {
        std::vector<pugi::xml_node> found;
        for (pugi::xml_node child : node.children()) {
            if (child.type() == pugi::node_element)
                found.push_back(child);
        }
        return found;
    }

    Result<void> checkItems(const std::vector<pugi::xml_node> &list) const
    {
        for (const pugi::xml_node item : list) {
            if (std::string_view(item.name()) != itemName)
                return at(item, "expected <Item> here");
        }
        return {};
    }

    Result<void> writeItems(const MetaStruct &owner, const MetaField &element, std::size_t block, std::size_t offset, const std::vector<pugi::xml_node> &list)
    {
        for (std::size_t index = 0; index < list.size(); ++index) {
            if (const auto written = writeField(owner, element, block, offset + index * element.size, list[index]); !written)
                return written;
        }
        return {};
    }

    Result<void> writeField(const MetaStruct &owner, const MetaField &field, std::size_t block, std::size_t offset, pugi::xml_node node)
    {
        for (const pugi::xml_attribute attribute : node.attributes()) {
            if (!std::ranges::contains(fieldAttributes, std::string_view(attribute.name())))
                return at(node, std::string(attribute.name()) + " is not an attribute of a field");
        }
        switch (field.type) {
        case MetaType::Bool: {
            const auto text = attribute(node, "value");
            if (!text)
                return std::unexpected(text.error());
            const std::string value = trim(*text);
            const auto number = value == "true" ? std::optional<std::uint8_t>(1) : value == "false" ? std::optional<std::uint8_t>(0) : parseInteger<std::uint8_t>(value);
            if (!number)
                return at(node, "value must be true or false");
            put(block, offset, *number, 1);
            return {};
        }
        case MetaType::Int8: {
            const auto value = integerAttribute<std::int8_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, static_cast<std::uint8_t>(*value), 1);
            return {};
        }
        case MetaType::UInt8: {
            const auto value = integerAttribute<std::uint8_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, *value, 1);
            return {};
        }
        case MetaType::Int16: {
            const auto value = integerAttribute<std::int16_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, static_cast<std::uint16_t>(*value), 2);
            return {};
        }
        case MetaType::UInt16: {
            const auto value = integerAttribute<std::uint16_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, *value, 2);
            return {};
        }
        case MetaType::Int32: {
            const auto value = integerAttribute<std::int32_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, static_cast<std::uint32_t>(*value), 4);
            return {};
        }
        case MetaType::UInt32:
        case MetaType::Color: {
            const auto value = integerAttribute<std::uint32_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, *value, 4);
            return {};
        }
        case MetaType::Int64: {
            const auto value = integerAttribute<std::int64_t>(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, static_cast<std::uint64_t>(*value), 8);
            return {};
        }
        case MetaType::Float: {
            const auto value = floatAttribute(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, std::bit_cast<std::uint32_t>(*value), 4);
            return {};
        }
        case MetaType::Half: {
            const auto value = floatAttribute(node, "value");
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, std::bit_cast<std::uint16_t>(static_cast<std::float16_t>(*value)), 2);
            return {};
        }
        case MetaType::Vector2:
        case MetaType::Vector3:
        case MetaType::Vector4: {
            const int components = field.type == MetaType::Vector2 ? 2 : field.type == MetaType::Vector3 ? 3 : 4;
            const int written = field.type == MetaType::Vector3 && field.size >= 16 && node.attribute("w") ? 4 : components;
            for (int index = 0; index < written; ++index) {
                const auto value = floatAttribute(node, std::array { "x", "y", "z", "w" }[index]);
                if (!value)
                    return std::unexpected(value.error());
                put(block, offset + 4 * index, std::bit_cast<std::uint32_t>(*value), 4);
            }
            return {};
        }
        case MetaType::Hash: {
            const auto hash = hashValue(node, node.child_value());
            if (!hash)
                return std::unexpected(hash.error());
            put(block, offset, *hash, 4);
            return {};
        }
        case MetaType::FixedString: {
            const auto bytes = utf8ToLatin1(node.child_value());
            if (!bytes)
                return at(node, bytes.error().message);
            if (bytes->size() > field.size)
                return at(node, "the text is longer than " + std::to_string(field.size) + " characters");
            std::ranges::copy(*bytes, blocks_[block].data.begin() + static_cast<std::ptrdiff_t>(offset));
            return {};
        }
        case MetaType::CString:
        case MetaType::AtString:
            return writeString(field, block, offset, node);
        case MetaType::Struct: {
            const auto found = schema_.structs.find(field.reference);
            if (found == schema_.structs.end())
                return at(node, names_.name(field.reference) + " is not described in this file");
            return writeStruct(found->second, block, offset, node);
        }
        case MetaType::StructPointer: {
            const auto typeName = attribute(node, "type");
            if (!typeName)
                return std::unexpected(typeName.error());
            if (*typeName == nullType)
                return {};
            const auto found = structNames_.find(*typeName);
            if (found == structNames_.end())
                return at(node, *typeName + " is not described in this file");
            const MetaStruct &type = schema_.structs.at(found->second);
            const auto allocated = allocate(type.name, type.size, type.align, false);
            if (!allocated)
                return std::unexpected(allocated.error());
            fixups_.push_back(Fixup { block, offset, allocated->first, allocated->second });
            return writeStruct(type, allocated->first, allocated->second, node);
        }
        case MetaType::Array:
        case MetaType::PointerArray: {
            const auto list = items(node);
            if (const auto checked = checkItems(list); !checked)
                return checked;
            const bool counted = field.type == MetaType::Array;
            std::size_t count = list.size();
            if (counted && list.empty() && node.attribute("count")) {
                const auto parsed = integerAttribute<std::uint32_t>(node, "count");
                if (!parsed)
                    return std::unexpected(parsed.error());
                count = *parsed;
            }
            if (counted && count > maxCount)
                return at(node, "holds more than 65535 items");
            if (!counted && !list.empty() && list.size() != field.count)
                return at(node, "must hold exactly " + std::to_string(field.count) + " items");
            if (counted) {
                const pugi::xml_attribute capacity = node.attribute("capacity");
                const auto parsed = capacity ? parseInteger<std::uint16_t>(capacity.value()) : std::optional<std::uint16_t>(static_cast<std::uint16_t>(count));
                if (!parsed)
                    return at(node, "capacity is not a valid number");
                put(block, offset + 8, count, 2);
                put(block, offset + 10, *parsed, 2);
            }
            if (list.empty())
                return {};
            const MetaField &element = owner.entries[static_cast<std::size_t>(field.element)];
            if (element.size == 0)
                return at(node, "its items are not described in this file");
            const auto allocated = allocate(element.blockType, element.size * list.size(), element.align, field.ownBlock);
            if (!allocated)
                return std::unexpected(allocated.error());
            fixups_.push_back(Fixup { block, offset, allocated->first, allocated->second });
            return writeItems(owner, element, allocated->first, allocated->second, list);
        }
        case MetaType::FixedArray:
        case MetaType::CountedArray: {
            const auto list = items(node);
            if (const auto checked = checkItems(list); !checked)
                return checked;
            const MetaField &element = owner.entries[static_cast<std::size_t>(field.element)];
            if (field.type == MetaType::FixedArray && list.size() != field.count)
                return at(node, "must hold exactly " + std::to_string(field.count) + " items");
            if (list.size() > field.count)
                return at(node, "holds at most " + std::to_string(field.count) + " items");
            if (field.type == MetaType::CountedArray)
                put(block, offset + field.count * element.size, list.size(), 4);
            return writeItems(owner, element, block, offset, list);
        }
        case MetaType::Map: {
            const auto list = items(node);
            if (const auto checked = checkItems(list); !checked)
                return checked;
            if (list.size() > maxCount)
                return at(node, "holds more than 65535 entries");
            const auto flag = node.attribute("flag") ? integerAttribute<std::uint8_t>(node, "flag") : Result<std::uint8_t>(1);
            if (!flag)
                return std::unexpected(flag.error());
            const auto capacity = node.attribute("capacity") ? integerAttribute<std::uint16_t>(node, "capacity") : Result<std::uint16_t>(static_cast<std::uint16_t>(list.size()));
            if (!capacity)
                return std::unexpected(capacity.error());
            put(block, offset, *flag, 1);
            put(block, offset + 16, list.size(), 2);
            put(block, offset + 18, *capacity, 2);
            if (list.empty())
                return {};
            const auto pair = schema_.structs.find(field.reference);
            if (pair == schema_.structs.end() || pair->second.members.size() != 2)
                return at(node, "its entries are not described in this file");
            const MetaStruct &entry = pair->second;
            const auto allocated = allocate(entry.name, entry.size * list.size(), entry.align, false);
            if (!allocated)
                return std::unexpected(allocated.error());
            fixups_.push_back(Fixup { block, offset + 8, allocated->first, allocated->second });
            const MetaField &key = entry.entries[entry.members[0]];
            const MetaField &value = entry.entries[entry.members[1]];
            for (std::size_t index = 0; index < list.size(); ++index) {
                const std::size_t base = allocated->second + index * entry.size;
                const auto keyText = attribute(list[index], "key");
                if (!keyText)
                    return std::unexpected(keyText.error());
                if (const auto written = writeScalar(key, allocated->first, base + key.offset, list[index], *keyText); !written)
                    return written;
                if (const auto written = writeField(entry, value, allocated->first, base + value.offset, list[index]); !written)
                    return written;
            }
            return {};
        }
        case MetaType::Enum: {
            const auto value = enumValue(node, field.reference, node.child_value(), field.size);
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, *value, field.size);
            return {};
        }
        case MetaType::Flags: {
            const auto value = flagsValue(node, field.reference, node.child_value());
            if (!value)
                return std::unexpected(value.error());
            put(block, offset, *value, field.size);
            return {};
        }
        case MetaType::Raw: {
            const auto bytes = parseHex(trim(node.child_value()));
            if (!bytes || bytes->size() != field.size)
                return at(node, "must hold " + std::to_string(field.size) + " bytes as hexadecimal");
            std::ranges::copy(*bytes, blocks_[block].data.begin() + static_cast<std::ptrdiff_t>(offset));
            return {};
        }
        }
        return at(node, "has a kind that is not supported");
    }

    const MetaSchema &schema_;
    std::string_view source_;
    const NameTable &names_;
    std::unordered_map<std::string, std::uint32_t> structNames_;
    std::vector<Block> blocks_;
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> byType_;
    std::vector<std::uint32_t> typeOrder_;
    std::vector<Fixup> fixups_;
    std::size_t total_ = 0;
    std::size_t depth_ = 0;
};

struct StringWriter : pugi::xml_writer {
    std::string text;
    void write(const void *data, std::size_t size) override
    {
        text.append(static_cast<const char *>(data), size);
    }
};

}

std::vector<std::uint32_t> hashTableOrder(std::span<const std::uint32_t> insertion)
{
    constexpr std::array<std::uint32_t, 12> primes { 11, 29, 59, 107, 191, 331, 563, 953, 1609, 2729, 4621, 7841 };
    std::size_t level = 0;
    std::vector<std::vector<std::uint32_t>> buckets(primes[level]);
    std::size_t count = 0;
    for (const std::uint32_t hash : insertion) {
        auto &bucket = buckets[hash % primes[level]];
        bucket.insert(bucket.begin(), hash);
        if (++count >= primes[level] && level + 1 < primes.size()) {
            ++level;
            std::vector<std::vector<std::uint32_t>> grown(primes[level]);
            for (const auto &old : buckets) {
                for (const std::uint32_t moved : old) {
                    auto &target = grown[moved % primes[level]];
                    target.insert(target.begin(), moved);
                }
            }
            buckets = std::move(grown);
        }
    }
    std::vector<std::uint32_t> order;
    for (const auto &bucket : buckets)
        order.insert(order.end(), bucket.begin(), bucket.end());
    return order;
}

std::string latin1ToUtf8(std::span<const std::byte> bytes)
{
    std::string text;
    for (const std::byte byte : bytes) {
        const auto value = std::to_integer<unsigned char>(byte);
        if (value < 0x80) {
            text += static_cast<char>(value);
        } else {
            text += static_cast<char>(0xC0 | (value >> 6));
            text += static_cast<char>(0x80 | (value & 0x3F));
        }
    }
    return text;
}

Result<std::vector<std::byte>> utf8ToLatin1(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto value = static_cast<unsigned char>(text[index]);
        if (value < 0x80) {
            bytes.push_back(static_cast<std::byte>(value));
        } else if ((value == 0xC2 || value == 0xC3) && index + 1 < text.size() && (static_cast<unsigned char>(text[index + 1]) & 0xC0) == 0x80) {
            bytes.push_back(static_cast<std::byte>(((value & 0x03) << 6) | (static_cast<unsigned char>(text[index + 1]) & 0x3F)));
            ++index;
        } else {
            return failure("the text holds a character this file cannot store");
        }
    }
    return bytes;
}

std::string shortestFloat(float value)
{
    if (std::isnan(value))
        return std::format("0x{:08X}", std::bit_cast<std::uint32_t>(value));
    std::array<char, 32> buffer {};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), end);
}

Result<std::string> metaToXml(const MetaData &data, const NameTable &names)
{
    pugi::xml_document document;
    Reader reader(data, names);
    if (const auto read = reader.run(document); !read)
        return std::unexpected(read.error());
    StringWriter writer;
    document.save(writer, "  ", pugi::format_indent, pugi::encoding_utf8);
    return std::move(writer.text);
}

Result<MetaData> metaFromXml(const MetaData &original, std::string_view xml, const NameTable &names)
{
    if (original.root == 0 || original.root > original.blocks.size())
        return failure("the original file has no root block");
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size(), pugi::parse_default & ~pugi::parse_eol, pugi::encoding_utf8);
    if (!parsed)
        return failure(std::string("the XML is not well formed: ") + parsed.description() + " at character " + std::to_string(parsed.offset));
    Writer writer(original, xml, names);
    return writer.run(document, original.blocks[original.root - 1].type);
}

}
