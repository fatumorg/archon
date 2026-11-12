#include "support/drawable_samples.h"

#include "core/crypto/hash.h"
#include "core/rsc/resource_file.h"

#include <bit>
#include <string_view>

namespace archon::test {

namespace {

constexpr std::uint64_t systemBase = 0x50000000;
constexpr std::uint64_t graphicsBase = 0x60000000;

std::uint32_t hashOf(std::string_view text)
{
    return jenkinsHash(std::as_bytes(std::span(text.data(), text.size())));
}

class Writer {
public:
    explicit Writer(DrawableSample &sample)
        : sample_(sample)
    {
    }

    std::size_t alloc(std::size_t size)
    {
        const std::size_t at = (sample_.system.size() + 15) / 16 * 16;
        sample_.system.resize(at + size);
        return at;
    }

    void put(std::size_t at, std::uint64_t value, int size)
    {
        for (int i = 0; i < size; ++i)
            sample_.system[at + i] = static_cast<std::byte>(value >> (8 * i));
    }

    void pointer(std::size_t at, std::size_t target)
    {
        put(at, systemBase + target, 8);
    }

    std::size_t text(std::string_view value)
    {
        const std::size_t at = alloc(value.size() + 1);
        for (std::size_t i = 0; i < value.size(); ++i)
            sample_.system[at + i] = static_cast<std::byte>(value[i]);
        return at;
    }

    std::size_t texture(std::string_view name, bool withPixels)
    {
        const std::size_t texture = alloc(0x90);
        pointer(texture + 0x28, text(name));
        if (withPixels) {
            put(texture + 0x50, 4, 2);
            put(texture + 0x52, 4, 2);
            put(texture + 0x58, 21, 4);
            put(texture + 0x5D, 1, 1);
            const std::size_t pixels = sample_.graphics.size();
            sample_.graphics.resize(pixels + 64, std::byte { 0xFF });
            put(texture + 0x70, graphicsBase + pixels, 8);
        }
        return texture;
    }

    std::size_t drawable(const DrawableSampleOptions &options)
    {
        const std::size_t root = alloc(0xD0);
        const std::size_t group = alloc(0x40);
        pointer(root + 0x10, group);
        if (options.embeddedTexture) {
            const std::size_t dictionary = alloc(0x40);
            const std::size_t list = alloc(8);
            pointer(list, texture("tri_d", true));
            pointer(dictionary + 0x30, list);
            put(dictionary + 0x38, 1, 2);
            put(dictionary + 0x3A, 1, 2);
            pointer(group + 0x08, dictionary);
        }
        const std::size_t shaders = alloc(8);
        const std::size_t shader = alloc(0x30);
        pointer(shaders, shader);
        pointer(group + 0x10, shaders);
        put(group + 0x18, 1, 2);
        const std::size_t parameters = alloc(0x20);
        pointer(shader, parameters);
        put(shader + 0x10, 1, 1);
        put(shader + 0x14, 0x10, 2);
        put(parameters, 0, 1);
        pointer(parameters + 0x08, texture("tri_d", false));
        put(parameters + 0x10, hashOf("diffusesampler"), 4);

        const std::size_t list = alloc(0x10);
        const std::size_t models = alloc(8);
        const std::size_t model = alloc(0x30);
        pointer(root + 0x50, list);
        pointer(list, models);
        put(list + 8, 1, 2);
        put(list + 10, 1, 2);
        pointer(models, model);
        const std::size_t geometries = alloc(8);
        const std::size_t shaderMap = alloc(2);
        pointer(model + 0x08, geometries);
        put(model + 0x10, 1, 2);
        put(model + 0x12, 1, 2);
        pointer(model + 0x20, shaderMap);
        const std::size_t geometry = alloc(0x98);
        pointer(geometries, geometry);

        std::vector<std::byte> vertices = options.vertices;
        if (vertices.empty()) {
            const float data[3][9] = { { 0, 0, 0, 0, 0, 1, 0, 0, 0 }, { 1, 0, 0, 0, 0, 1, 0, 1, 0 }, { 0, 1, 0, 0, 0, 1, 0, 0, 1 } };
            for (const auto &vertex : data) {
                for (int i = 0; i < 6; ++i)
                    for (int b = 0; b < 4; ++b)
                        vertices.push_back(static_cast<std::byte>(std::bit_cast<std::uint32_t>(vertex[i]) >> (8 * b)));
                for (int b = 0; b < 4; ++b)
                    vertices.push_back(std::byte { 0xFF });
                for (int i = 7; i < 9; ++i)
                    for (int b = 0; b < 4; ++b)
                        vertices.push_back(static_cast<std::byte>(std::bit_cast<std::uint32_t>(vertex[i]) >> (8 * b)));
            }
        }
        const std::size_t count = vertices.size() / options.stride;
        const std::size_t vertexBuffer = alloc(0x80);
        const std::size_t declaration = alloc(0x10);
        const std::size_t indexBuffer = alloc(0x60);
        const std::size_t vertexData = alloc(vertices.size());
        std::ranges::copy(vertices, sample_.system.begin() + static_cast<std::ptrdiff_t>(vertexData));
        const std::size_t indexData = alloc(6);
        put(indexData, 0, 2);
        put(indexData + 2, 1, 2);
        put(indexData + 4, 2, 2);
        pointer(geometry + 0x18, vertexBuffer);
        pointer(geometry + 0x38, indexBuffer);
        put(geometry + 0x58, 3, 4);
        put(geometry + 0x5C, 1, 4);
        put(geometry + 0x60, count, 2);
        put(geometry + 0x62, 3, 2);
        put(geometry + 0x70, options.stride, 2);
        put(vertexBuffer + 0x08, options.stride, 2);
        pointer(vertexBuffer + 0x10, vertexData);
        put(vertexBuffer + 0x18, count, 4);
        pointer(vertexBuffer + 0x20, vertexData);
        pointer(vertexBuffer + 0x30, declaration);
        put(declaration, options.mask, 4);
        put(declaration + 4, options.stride, 2);
        put(declaration + 8, options.types, 8);
        put(indexBuffer + 0x08, 3, 4);
        pointer(indexBuffer + 0x10, indexData);
        sample_.geometry = geometry;
        sample_.vertexBuffer = vertexBuffer;
        sample_.declaration = declaration;
        sample_.indexBuffer = indexBuffer;
        sample_.indexData = indexData;
        return root;
    }

private:
    DrawableSample &sample_;
};

}

DrawableSample sampleDrawable(const DrawableSampleOptions &options)
{
    DrawableSample sample;
    Writer writer(sample);
    switch (options.kind) {
    case SampleKind::Ydr:
        writer.drawable(options);
        break;
    case SampleKind::Yft: {
        const std::size_t root = writer.alloc(0x100);
        writer.pointer(root + 0x30, writer.drawable(options));
        break;
    }
    case SampleKind::Ydd: {
        const std::size_t root = writer.alloc(0x40);
        const std::size_t hashes = writer.alloc(8);
        const std::size_t list = writer.alloc(16);
        writer.put(hashes, hashOf("first"), 4);
        writer.put(hashes + 4, hashOf("second"), 4);
        writer.pointer(root + 0x20, hashes);
        writer.put(root + 0x28, 2, 2);
        writer.put(root + 0x2A, 2, 2);
        writer.pointer(root + 0x30, list);
        writer.put(root + 0x38, 2, 2);
        writer.put(root + 0x3A, 2, 2);
        writer.pointer(list, writer.drawable(options));
        writer.pointer(list + 8, writer.drawable(options));
        break;
    }
    }
    return sample;
}

std::vector<std::byte> drawableFile(const DrawableSample &sample, std::uint32_t version)
{
    return *buildResourceFile(version, sample.system, sample.graphics);
}

}
