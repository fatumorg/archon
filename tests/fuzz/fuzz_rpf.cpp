#include "core/io/ascii.h"
#include "core/rpf/rpf_reader.h"
#include "support/memory_source.h"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace archon;
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto reader = RpfReader::open(test::memorySource({ bytes.begin(), bytes.end() }), "fuzz.rpf", nullptr);
    if (!reader)
        return 0;
    const auto &entries = reader->archive().entries();
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
        (void)reader->fileData(index);
        (void)reader->storedFile(index);
        (void)reader->archive().path(index);
        if (asciiLower(entries[index].name).ends_with(".rpf"))
            (void)reader->openNested(index);
    }
    return 0;
}
