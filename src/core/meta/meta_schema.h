#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace archon {

enum class MetaType : std::uint8_t {
    Bool,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Color,
    Float,
    Vector2,
    Vector3,
    Vector4,
    Half,
    Int64,
    Hash,
    FixedString,
    CString,
    AtString,
    Struct,
    StructPointer,
    Array,
    FixedArray,
    CountedArray,
    PointerArray,
    Map,
    Enum,
    Flags,
    Raw,
};

struct MetaField {
    std::uint32_t name = 0;
    MetaType type = MetaType::Raw;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t align = 1;
    std::uint32_t reference = 0;
    std::uint32_t count = 0;
    std::uint32_t blockType = 0;
    int element = -1;
    int key = -1;
    bool ownBlock = false;
};

struct MetaStruct {
    std::uint32_t name = 0;
    std::uint32_t size = 0;
    std::uint32_t align = 1;
    std::vector<MetaField> entries;
    std::vector<std::size_t> members;
};

struct MetaEnum {
    std::uint32_t name = 0;
    std::vector<std::pair<std::uint32_t, std::int32_t>> values;
};

struct MetaSchema {
    bool bigEndian = false;
    std::uint32_t stringBlock = 0;
    std::uint32_t blockCapacity = 0;
    std::unordered_map<std::uint32_t, MetaStruct> structs;
    std::unordered_map<std::uint32_t, MetaEnum> enums;
};

struct MetaBlock {
    std::uint32_t type = 0;
    std::vector<std::byte> data;
    bool graphics = false;
};

struct MetaData {
    MetaSchema schema;
    std::vector<MetaBlock> blocks;
    std::uint32_t root = 0;
};

std::vector<std::uint32_t> hashTableOrder(std::span<const std::uint32_t> insertion);

}
