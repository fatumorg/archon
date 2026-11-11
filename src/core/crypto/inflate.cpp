#include "core/crypto/inflate.h"

#include <zlib.h>

namespace archon {

Result<std::vector<std::byte>> inflateRaw(std::span<const std::byte> data, std::size_t exactSize)
{
    const auto failure = [](std::uint64_t offset, const char *message) { return std::unexpected(Error { "deflate", offset, message }); };
    if (exactSize > maxAllocation)
        return failure(0, "declared size exceeds the allocation limit");
    if (data.size() > maxAllocation)
        return failure(0, "compressed data exceeds the allocation limit");
    z_stream stream {};
    if (inflateInit2(&stream, -15) != Z_OK)
        return failure(0, "inflate initialisation failed");
    std::vector<std::byte> result(exactSize + 1);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = static_cast<uInt>(result.size());
    const int status = inflate(&stream, Z_FINISH);
    const std::size_t produced = stream.total_out;
    const std::uint64_t consumed = stream.total_in;
    inflateEnd(&stream);
    if (produced > exactSize)
        return failure(consumed, "data expands past its declared size");
    if (status != Z_STREAM_END)
        return failure(consumed, "deflate stream is corrupt or truncated");
    if (produced != exactSize)
        return failure(consumed, "data is shorter than its declared size");
    result.resize(exactSize);
    return result;
}

}
