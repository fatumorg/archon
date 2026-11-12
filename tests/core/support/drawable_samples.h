#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace archon::test {

enum class SampleKind { Ydr, Ydd, Yft };

struct DrawableSampleOptions {
    SampleKind kind = SampleKind::Ydr;
    std::uint32_t mask = 0x59;
    std::uint64_t types = 0x7755555555996996;
    std::uint16_t stride = 36;
    std::vector<std::byte> vertices;
    bool embeddedTexture = false;
};

struct DrawableSample {
    std::vector<std::byte> system;
    std::vector<std::byte> graphics;
    std::size_t geometry = 0;
    std::size_t vertexBuffer = 0;
    std::size_t declaration = 0;
    std::size_t indexBuffer = 0;
    std::size_t indexData = 0;
};

DrawableSample sampleDrawable(const DrawableSampleOptions &options = {});
std::vector<std::byte> drawableFile(const DrawableSample &sample, std::uint32_t version);

}
