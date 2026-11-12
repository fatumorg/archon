#include "core/drawable/drawable.h"

#include "core/crypto/hash.h"
#include "core/io/ascii.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/resource_pages.h"
#include "core/rsc/rsc7.h"

#include <bit>
#include <optional>
#include <stdfloat>

namespace archon {

namespace {

enum class Kind { Ydr, Ydd, Yft };

constexpr std::size_t maxNameLength = 255;
constexpr std::uint8_t textureParameter = 0;
constexpr std::uint16_t triangleList = 3;
constexpr std::array<int, 11> componentSizes { 0, 4, 4, 8, 4, 8, 12, 16, 4, 4, 4 };
constexpr int positionComponent = 0;
constexpr int normalComponent = 3;
constexpr int uvComponent = 6;
constexpr std::array<std::uint64_t, 4> lodLists { 0x50, 0x58, 0x60, 0x68 };

enum ComponentType : std::uint32_t {
    Half2 = 1,
    Half4 = 3,
    Float2 = 5,
    Float3 = 6,
    Float4 = 7,
    Dec3n = 10,
};

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "drawable", 0, std::move(message) });
}

std::optional<Kind> kindOf(std::string_view name)
{
    const std::string lower = asciiLower(name);
    if (lower.ends_with(".ydr"))
        return Kind::Ydr;
    if (lower.ends_with(".ydd"))
        return Kind::Ydd;
    if (lower.ends_with(".yft"))
        return Kind::Yft;
    return std::nullopt;
}

enum class Layout { Legacy, Enhanced };

std::optional<Layout> layoutOf(Kind kind, std::uint32_t version)
{
    if (kind == Kind::Yft)
        return version == 162 || version == 160 ? std::optional(Layout::Legacy) : version == 171 ? std::optional(Layout::Enhanced) : std::nullopt;
    return version == 165 ? std::optional(Layout::Legacy) : version == 159 || version == 154 ? std::optional(Layout::Enhanced) : std::nullopt;
}

std::uint32_t nameHash(std::string_view name)
{
    return jenkinsHash(std::as_bytes(std::span(name.data(), name.size())));
}

constexpr std::size_t enhancedSlots = 52;
constexpr std::size_t enhancedNormalSlot = 4;
constexpr std::size_t enhancedUvSlot = 28;

class Reader {
public:
    Reader(std::span<const std::byte> pages, std::uint64_t systemSize, Layout layout)
        : bytes_(pages)
        , pages_(pages, systemSize)
        , enhanced_(layout == Layout::Enhanced)
    {
    }

    Result<std::vector<Drawable>> run(Kind kind)
    {
        switch (kind) {
        case Kind::Ydr: {
            auto drawable = read(resourceSystemBase);
            if (!drawable)
                return std::unexpected(drawable.error());
            return std::vector<Drawable> { std::move(*drawable) };
        }
        case Kind::Yft: {
            const auto root = at(resourceSystemBase, 0x38, "the fragment header");
            if (!root)
                return std::unexpected(root.error());
            auto drawable = read(pages_.value(*root + 0x30, 8));
            if (!drawable)
                return std::unexpected(drawable.error());
            return std::vector<Drawable> { std::move(*drawable) };
        }
        case Kind::Ydd:
            return dictionary();
        }
        return failure("unknown drawable kind");
    }

private:
    Result<std::uint64_t> at(std::uint64_t pointer, std::uint64_t length, const std::string &what) const
    {
        const auto offset = pages_.resolve(pointer, length);
        if (!offset)
            return failure(what + " lies outside the resource");
        return *offset;
    }

    Result<std::vector<Drawable>> dictionary()
    {
        const auto root = at(resourceSystemBase, 0x40, "the drawable dictionary");
        if (!root)
            return std::unexpected(root.error());
        const std::uint64_t count = pages_.value(*root + 0x38, 2);
        const std::uint64_t hashCount = pages_.value(*root + 0x28, 2);
        const auto list = at(pages_.value(*root + 0x30, 8), count * 8, "the drawable list");
        if (!list)
            return std::unexpected(list.error());
        const auto hashes = at(pages_.value(*root + 0x20, 8), hashCount * 4, "the drawable names");
        if (!hashes)
            return std::unexpected(hashes.error());
        std::vector<Drawable> drawables;
        for (std::uint64_t index = 0; index < count; ++index) {
            auto drawable = read(pages_.value(*list + index * 8, 8));
            if (!drawable)
                return std::unexpected(drawable.error());
            if (index < hashCount)
                drawable->nameHash = static_cast<std::uint32_t>(pages_.value(*hashes + index * 4, 4));
            drawables.push_back(std::move(*drawable));
        }
        return drawables;
    }

    Result<Drawable> read(std::uint64_t pointer)
    {
        const auto root = at(pointer, 0x70, "a drawable");
        if (!root)
            return std::unexpected(root.error());
        Drawable drawable;
        std::vector<std::string> diffuse;
        if (const std::uint64_t group = pages_.value(*root + 0x10, 8); group != 0) {
            const auto shaders = shaderGroup(group, drawable);
            if (!shaders)
                return std::unexpected(shaders.error());
            diffuse = std::move(*shaders);
        }
        for (const std::uint64_t lod : lodLists) {
            const std::uint64_t list = pages_.value(*root + lod, 8);
            if (list == 0)
                continue;
            const auto header = at(list, 0x10, "a model list");
            if (!header)
                return std::unexpected(header.error());
            const std::uint64_t count = pages_.value(*header + 8, 2);
            if (count == 0)
                continue;
            const auto models = at(pages_.value(*header, 8), count * 8, "a model list");
            if (!models)
                return std::unexpected(models.error());
            for (std::uint64_t index = 0; index < count; ++index) {
                if (const auto done = model(pages_.value(*models + index * 8, 8), diffuse, drawable); !done)
                    return std::unexpected(done.error());
            }
            break;
        }
        return drawable;
    }

    Result<std::vector<std::string>> shaderGroup(std::uint64_t pointer, Drawable &drawable)
    {
        const auto group = at(pointer, 0x20, "the shader group");
        if (!group)
            return std::unexpected(group.error());
        if (const std::uint64_t dictionary = pages_.value(*group + 0x08, 8); dictionary != 0) {
            const auto root = pages_.system(dictionary, 0x40);
            if (!root)
                return failure("the embedded texture dictionary lies outside the resource");
            auto textures = readTextureDictionary(bytes_, pages_.systemSize(), enhanced_ ? enhancedDictionaryVersion : legacyDictionaryVersion, *root);
            if (!textures)
                return std::unexpected(textures.error());
            drawable.textures = std::move(*textures);
        }
        const std::uint64_t count = pages_.value(*group + 0x18, 2);
        const auto list = at(pages_.value(*group + 0x10, 8), count * 8, "the shader list");
        if (!list)
            return std::unexpected(list.error());
        std::vector<std::string> diffuse;
        for (std::uint64_t index = 0; index < count; ++index) {
            const auto name = enhanced_ ? enhancedShaderDiffuse(pages_.value(*list + index * 8, 8)) : shaderDiffuse(pages_.value(*list + index * 8, 8));
            if (!name)
                return std::unexpected(name.error());
            diffuse.push_back(*name);
        }
        return diffuse;
    }

    Result<std::string> shaderDiffuse(std::uint64_t pointer)
    {
        const auto shader = at(pointer, 0x18, "a shader");
        if (!shader)
            return std::unexpected(shader.error());
        const std::uint64_t count = pages_.value(*shader + 0x10, 1);
        const std::uint64_t hashesAt = pages_.value(*shader + 0x14, 2);
        if (count == 0)
            return std::string();
        const auto parameters = at(pages_.value(*shader, 8), std::max(count * 16, hashesAt + count * 4), "the shader parameters");
        if (!parameters)
            return std::unexpected(parameters.error());
        for (std::uint64_t index = 0; index < count; ++index) {
            const std::uint64_t parameter = *parameters + index * 16;
            if (pages_.value(parameter, 1) != textureParameter || pages_.value(*parameters + hashesAt + index * 4, 4) != nameHash("diffusesampler"))
                continue;
            return textureName(pages_.value(parameter + 8, 8));
        }
        return std::string();
    }

    Result<std::string> enhancedShaderDiffuse(std::uint64_t pointer)
    {
        const auto shader = at(pointer, 0x28, "a shader");
        if (!shader)
            return std::unexpected(shader.error());
        const auto descriptor = at(pages_.value(*shader + 0x20, 8), 8, "the shader descriptor");
        if (!descriptor)
            return std::unexpected(descriptor.error());
        const std::uint64_t count = pages_.value(*descriptor + 1, 1);
        const auto entries = at(pages_.value(*shader + 0x20, 8), 8 + count * 8, "the shader descriptor");
        if (!entries)
            return std::unexpected(entries.error());
        for (std::uint64_t index = 0; index < count; ++index) {
            if (pages_.value(*entries + 8 + index * 8, 4) != nameHash("diffusetex"))
                continue;
            const std::uint64_t slot = pages_.value(*entries + 12 + index * 8, 4) >> 2;
            const auto slots = at(pages_.value(*shader + 0x10, 8), (slot + 1) * 8, "the shader textures");
            if (!slots)
                return std::unexpected(slots.error());
            return textureName(pages_.value(*slots + slot * 8, 8));
        }
        return std::string();
    }

    Result<std::string> textureName(std::uint64_t texture) const
    {
        if (texture == 0)
            return std::string();
        const auto header = at(texture, 0x30, "a shader texture");
        if (!header)
            return std::unexpected(header.error());
        const auto name = pages_.string(pages_.value(*header + 0x28, 8), maxNameLength);
        if (!name)
            return failure("a shader texture name lies outside the resource");
        return *name;
    }

    Result<void> model(std::uint64_t pointer, const std::vector<std::string> &diffuse, Drawable &drawable)
    {
        const auto header = at(pointer, 0x30, "a model");
        if (!header)
            return std::unexpected(header.error());
        const std::uint64_t count = pages_.value(*header + 0x10, 2);
        if (count == 0)
            return {};
        const auto geometries = at(pages_.value(*header + 0x08, 8), count * 8, "the geometry list");
        if (!geometries)
            return std::unexpected(geometries.error());
        const auto shaderMap = at(pages_.value(*header + 0x20, 8), count * 2, "the shader map");
        if (!shaderMap)
            return std::unexpected(shaderMap.error());
        for (std::uint64_t index = 0; index < count; ++index) {
            auto mesh = geometry(pages_.value(*geometries + index * 8, 8));
            if (!mesh)
                return std::unexpected(mesh.error());
            const std::uint64_t shader = pages_.value(*shaderMap + index * 2, 2);
            if (shader < diffuse.size())
                mesh->diffuse = diffuse[shader];
            drawable.meshes.push_back(std::move(*mesh));
        }
        return {};
    }

    float floatAt(std::uint64_t offset) const
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(pages_.value(offset, 4)));
    }

    float halfAt(std::uint64_t offset) const
    {
        return static_cast<float>(std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(pages_.value(offset, 2))));
    }

    std::array<float, 3> vectorAt(std::uint64_t offset, std::uint32_t type) const
    {
        switch (type) {
        case Float3:
        case Float4:
            return { floatAt(offset), floatAt(offset + 4), floatAt(offset + 8) };
        case Half4:
            return { halfAt(offset), halfAt(offset + 2), halfAt(offset + 4) };
        case Dec3n: {
            const auto packed = static_cast<std::uint32_t>(pages_.value(offset, 4));
            const auto component = [packed](int shift) {
                const auto bits = static_cast<std::int32_t>((packed >> shift) & 0x3FF);
                return static_cast<float>(bits >= 512 ? bits - 1024 : bits) / 511.0f;
            };
            return { component(0), component(10), component(20) };
        }
        default:
            return { 0, 0, 0 };
        }
    }

    std::array<float, 2> uvAt(std::uint64_t offset, std::uint32_t type) const
    {
        switch (type) {
        case Float2:
            return { floatAt(offset), floatAt(offset + 4) };
        case Half2:
            return { halfAt(offset), halfAt(offset + 2) };
        default:
            return { 0, 0 };
        }
    }

    struct VertexLayout {
        std::uint64_t data = 0;
        std::uint64_t count = 0;
        std::uint64_t stride = 0;
        std::uint64_t position = 0;
        std::optional<std::pair<std::uint64_t, std::uint32_t>> normal;
        std::optional<std::pair<std::uint64_t, std::uint32_t>> uv;
    };

    struct IndexLayout {
        std::uint64_t data = 0;
        std::uint64_t count = 0;
    };

    Result<std::pair<VertexLayout, IndexLayout>> enhancedBuffers(std::uint64_t header)
    {
        const auto buffer = at(pages_.value(header + 0x18, 8), 0x40, "a vertex buffer");
        if (!buffer)
            return std::unexpected(buffer.error());
        VertexLayout vertices;
        vertices.count = pages_.value(*buffer + 0x08, 4);
        vertices.stride = pages_.value(*buffer + 0x0C, 4);
        vertices.data = pages_.value(*buffer + 0x18, 8);
        const auto table = at(pages_.value(*buffer + 0x38, 8), enhancedSlots * 4, "a vertex layout");
        if (!table)
            return std::unexpected(table.error());
        const auto offset = [&](std::size_t slot) { return slot == enhancedSlots ? vertices.stride : pages_.value(*table + slot * 4, 4); };
        for (std::size_t slot = 0; slot < enhancedSlots; ++slot) {
            if (offset(slot) > offset(slot + 1))
                return failure("a vertex layout is out of order or larger than its stride");
        }
        const auto size = [&](std::size_t slot) { return offset(slot + 1) - offset(slot); };
        if (offset(0) != 0 || size(0) != 12)
            return failure("a vertex declaration has no position");
        if (size(enhancedNormalSlot) == 12)
            vertices.normal = std::pair(offset(enhancedNormalSlot), static_cast<std::uint32_t>(Float3));
        if (size(enhancedUvSlot) == 8 || size(enhancedUvSlot) == 4)
            vertices.uv = std::pair(offset(enhancedUvSlot), static_cast<std::uint32_t>(size(enhancedUvSlot) == 8 ? Float2 : Half2));
        const auto indexBuffer = at(pages_.value(header + 0x38, 8), 0x20, "an index buffer");
        if (!indexBuffer)
            return std::unexpected(indexBuffer.error());
        if (pages_.value(*indexBuffer + 0x0C, 4) != 2)
            return failure("an index buffer does not hold 16-bit indices");
        return std::pair(vertices, IndexLayout { pages_.value(*indexBuffer + 0x18, 8), pages_.value(*indexBuffer + 0x08, 4) });
    }

    Result<std::pair<VertexLayout, IndexLayout>> legacyBuffers(std::uint64_t header)
    {
        const auto buffer = at(pages_.value(header + 0x18, 8), 0x38, "a vertex buffer");
        if (!buffer)
            return std::unexpected(buffer.error());
        const auto declaration = at(pages_.value(*buffer + 0x30, 8), 0x10, "a vertex declaration");
        if (!declaration)
            return std::unexpected(declaration.error());
        const std::uint64_t stride = pages_.value(*buffer + 0x08, 2);
        const std::uint64_t mask = pages_.value(*declaration, 4);
        const std::uint64_t types = pages_.value(*declaration + 0x08, 8);
        std::array<std::uint64_t, 16> offsets {};
        std::uint64_t packed = 0;
        for (int component = 0; component < 16; ++component) {
            if ((mask >> component & 1) == 0)
                continue;
            const std::uint32_t type = (types >> (4 * component)) & 0xF;
            if (type >= componentSizes.size())
                return failure("a vertex component has an unknown type");
            offsets[component] = packed;
            packed += componentSizes[type];
        }
        const std::uint32_t positionType = types & 0xF;
        if ((mask & 1) == 0 || (positionType != Float3 && positionType != Float4))
            return failure("a vertex declaration has no position");
        if (stride == 0 || packed > stride)
            return failure("a vertex stride is smaller than its components");
        VertexLayout vertices { pages_.value(*buffer + 0x10, 8), pages_.value(*buffer + 0x18, 4), stride, offsets[positionComponent], std::nullopt, std::nullopt };
        if ((mask >> normalComponent & 1) != 0)
            vertices.normal = std::pair(offsets[normalComponent], static_cast<std::uint32_t>((types >> (4 * normalComponent)) & 0xF));
        if ((mask >> uvComponent & 1) != 0)
            vertices.uv = std::pair(offsets[uvComponent], static_cast<std::uint32_t>((types >> (4 * uvComponent)) & 0xF));
        const auto indexBuffer = at(pages_.value(header + 0x38, 8), 0x18, "an index buffer");
        if (!indexBuffer)
            return std::unexpected(indexBuffer.error());
        return std::pair(vertices, IndexLayout { pages_.value(*indexBuffer + 0x10, 8), pages_.value(*indexBuffer + 0x08, 4) });
    }

    Result<Mesh> geometry(std::uint64_t pointer)
    {
        const auto header = at(pointer, 0x98, "a geometry");
        if (!header)
            return std::unexpected(header.error());
        if (pages_.value(*header + 0x62, 2) != triangleList)
            return failure("a geometry is not a triangle list");
        const auto buffers = enhanced_ ? enhancedBuffers(*header) : legacyBuffers(*header);
        if (!buffers)
            return std::unexpected(buffers.error());
        const auto &[layout, indexLayout] = *buffers;
        const std::uint64_t count = layout.count;
        const std::uint64_t stride = layout.stride;
        const auto vertices = at(layout.data, count * stride, "vertex data");
        if (!vertices)
            return std::unexpected(vertices.error());
        const std::uint64_t indexCount = indexLayout.count;
        if (indexCount % 3 != 0)
            return failure("an index buffer does not hold whole triangles");
        const auto indices = at(indexLayout.data, indexCount * 2, "index data");
        if (!indices)
            return std::unexpected(indices.error());

        Mesh mesh;
        mesh.indices.reserve(static_cast<std::size_t>(indexCount));
        for (std::uint64_t index = 0; index < indexCount; ++index) {
            const std::uint64_t value = pages_.value(*indices + index * 2, 2);
            if (value >= count)
                return failure("a triangle uses a vertex that does not exist");
            mesh.indices.push_back(static_cast<std::uint32_t>(value));
        }
        mesh.vertices.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t index = 0; index < count; ++index) {
            const std::uint64_t vertex = *vertices + index * stride;
            mesh.vertices.push_back(MeshVertex {
                vectorAt(vertex + layout.position, Float3),
                layout.normal ? vectorAt(vertex + layout.normal->first, layout.normal->second) : std::array<float, 3> { 0, 0, 0 },
                layout.uv ? uvAt(vertex + layout.uv->first, layout.uv->second) : std::array<float, 2> { 0, 0 },
            });
        }
        return mesh;
    }

    std::span<const std::byte> bytes_;
    ResourcePages pages_;
    bool enhanced_;
};

}

bool isDrawableName(std::string_view name)
{
    return kindOf(name).has_value();
}

Result<std::vector<Drawable>> readDrawables(std::span<const std::byte> file, std::string_view name)
{
    const auto kind = kindOf(name);
    if (!kind)
        return failure(std::string(name) + " is not a drawable");
    if (file.size() < 16)
        return failure("the file is smaller than a resource header");
    const auto header = parseRsc7Header(file.first(16));
    if (!header)
        return std::unexpected(header.error());
    const auto layout = layoutOf(*kind, header->version);
    if (!layout)
        return failure("drawable version " + std::to_string(header->version) + " is not supported");
    const auto pages = inflateResource(file);
    if (!pages)
        return std::unexpected(pages.error());
    Reader reader(*pages, rsc7SizeFromFlags(header->systemFlags), *layout);
    return reader.run(*kind);
}

}
