#include "support/meta_samples.h"

#include "core/meta/meta_schema.h"
#include "core/meta/pso_file.h"

#include <string>

namespace archon::test {

namespace {

void append32(std::vector<std::byte> &out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}

void set32(std::vector<std::byte> &out, std::size_t at, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index)
        out[at + index] = static_cast<std::byte>((value >> (24 - 8 * index)) & 0xFF);
}

void set16(std::vector<std::byte> &out, std::size_t at, std::uint16_t value)
{
    out[at] = static_cast<std::byte>(value >> 8);
    out[at + 1] = static_cast<std::byte>(value & 0xFF);
}

void entry(std::vector<std::byte> &out, std::uint32_t hash, std::uint8_t type, std::uint8_t subtype, std::uint16_t offset, std::uint32_t reference)
{
    append32(out, hash);
    out.push_back(static_cast<std::byte>(type));
    out.push_back(static_cast<std::byte>(subtype));
    out.push_back(static_cast<std::byte>(offset >> 8));
    out.push_back(static_cast<std::byte>(offset & 0xFF));
    append32(out, reference);
}

std::uint32_t name(const char *text)
{
    return joaat(text);
}

}

NameTable sampleNames()
{
    NameTable names;
    for (const char *text : { "csample", "fvalue", "modelname", "items", "label", "adder" })
        names.add(text);
    return names;
}

std::vector<std::byte> samplePso()
{
    std::vector<std::byte> schema { std::byte { 'P' }, std::byte { 'S' }, std::byte { 'C' }, std::byte { 'H' }, {}, {}, {}, {} };
    append32(schema, 1);
    append32(schema, name("csample"));
    append32(schema, 0x14);
    append32(schema, 5);
    append32(schema, 0x30);
    append32(schema, 0);
    entry(schema, name("fvalue"), 0x07, 0, 0x08, 0);
    entry(schema, name("modelname"), 0x0B, 7, 0x0C, 0);
    entry(schema, 0x100, 0x06, 0, 0, 0);
    entry(schema, name("items"), 0x0D, 0, 0x10, 2);
    entry(schema, name("label"), 0x0B, 3, 0x20, 0);
    set32(schema, 4, static_cast<std::uint32_t>(schema.size()));
    std::vector<std::byte> seed { std::byte { 'P' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'N' }, {}, {}, {}, std::byte { 8 } };
    for (const char *section : { "PMAP" }) {
        for (int index = 0; index < 4; ++index)
            seed.push_back(static_cast<std::byte>(section[index]));
        append32(seed, 8);
    }
    seed.insert(seed.end(), schema.begin(), schema.end());

    const std::uint32_t root = name("csample");
    const auto order = hashTableOrder(std::vector<std::uint32_t> { root, 0x06, 0x01 });
    const auto index = [&order](std::uint32_t type) { return static_cast<std::uint32_t>(std::ranges::find(order, type) - order.begin() + 1); };
    MetaData data;
    for (const std::uint32_t type : order) {
        MetaBlock block { type, {}, false };
        if (type == root) {
            block.data.resize(0x30);
            set32(block.data, 0x08, 0x3FC00000);
            set32(block.data, 0x0C, name("adder"));
            set32(block.data, 0x10, index(0x06));
            set16(block.data, 0x18, 3);
            set16(block.data, 0x1A, 3);
            set32(block.data, 0x20, index(0x01));
            set16(block.data, 0x28, 5);
            set16(block.data, 0x2A, 6);
        } else if (type == 0x06) {
            for (std::uint32_t value : { 1u, 2u, 3u })
                append32(block.data, value);
        } else {
            for (const char character : std::string("hello"))
                block.data.push_back(static_cast<std::byte>(character));
            block.data.push_back(std::byte { 0 });
        }
        data.blocks.push_back(std::move(block));
    }
    data.root = index(root);
    return *writePsoFile(seed, data);
}

std::vector<std::byte> sampleRbf()
{
    std::vector<std::byte> out;
    const auto bytes = [&out](std::initializer_list<int> values) {
        for (const int value : values)
            out.push_back(static_cast<std::byte>(value));
    };
    const auto text = [&out](std::string_view value) {
        for (const char character : value)
            out.push_back(static_cast<std::byte>(character));
    };
    text("RBF0");
    bytes({ 0, 0x00, 4, 0 });
    text("Root");
    bytes({ 0, 0, 0, 0, 0, 0 });
    bytes({ 1, 0x00, 4, 0 });
    text("Item");
    bytes({ 0, 0, 0, 0, 1, 0 });
    bytes({ 2, 0x60, 4, 0 });
    text("type");
    bytes({ 4, 0 });
    text("CDef");
    bytes({ 3, 0x10, 5, 0 });
    text("count");
    bytes({ 0xFE, 0xFF, 0xFF, 0xFF });
    bytes({ 4, 0x40, 6, 0 });
    text("radius");
    bytes({ 0, 0, 0xC0, 0x3F });
    bytes({ 5, 0x30, 4, 0 });
    text("flag");
    bytes({ 6, 0x50, 3, 0 });
    text("min");
    bytes({ 0, 0, 0x80, 0x3F, 0, 0, 0, 0x40, 0, 0, 0x40, 0x40 });
    bytes({ 7, 0x00, 4, 0 });
    text("Name");
    bytes({ 0, 0, 0, 0, 0, 0 });
    bytes({ 0xFD, 0xFF, 6, 0, 0, 0 });
    text("adder");
    bytes({ 0 });
    bytes({ 0xFF, 0xFF });
    bytes({ 0xFF, 0xFF });
    bytes({ 0xFF, 0xFF });
    return out;
}

}
