#include "core/crypto/inflate.h"

#include <zlib.h>

namespace archon {

namespace {

struct Inflated {
    std::vector<std::byte> bytes;
    std::uint64_t consumed;
    bool complete;
};

std::unexpected<Error> failure(std::uint64_t offset, const char *message)
{
    return std::unexpected(Error { "deflate", offset, message });
}

Result<Inflated> inflateWithin(std::span<const std::byte> data, std::size_t limit)
{
    if (limit > maxAllocation)
        return failure(0, "declared size exceeds the allocation limit");
    if (data.size() > maxAllocation)
        return failure(0, "compressed data exceeds the allocation limit");
    z_stream stream {};
    if (inflateInit2(&stream, -15) != Z_OK)
        return failure(0, "inflate initialisation failed");
    Inflated result { std::vector<std::byte>(limit + 1), 0, false };
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(result.bytes.data());
    stream.avail_out = static_cast<uInt>(result.bytes.size());
    result.complete = inflate(&stream, Z_FINISH) == Z_STREAM_END;
    result.consumed = stream.total_in;
    const std::size_t produced = stream.total_out;
    inflateEnd(&stream);
    if (produced > limit)
        return failure(result.consumed, "data expands past its declared size");
    result.bytes.resize(produced);
    return result;
}

}

Result<std::vector<std::byte>> inflateRaw(std::span<const std::byte> data, std::size_t exactSize)
{
    auto inflated = inflateWithin(data, exactSize);
    if (!inflated)
        return std::unexpected(inflated.error());
    if (!inflated->complete)
        return failure(inflated->consumed, "deflate stream is corrupt or truncated");
    if (inflated->bytes.size() != exactSize)
        return failure(inflated->consumed, "data is shorter than its declared size");
    return std::move(inflated->bytes);
}

Result<std::vector<std::byte>> inflateRawUpTo(std::span<const std::byte> data, std::size_t maxSize)
{
    auto inflated = inflateWithin(data, maxSize);
    if (!inflated)
        return std::unexpected(inflated.error());
    return std::move(inflated->bytes);
}

}
