#include "core/io/byte_reader.h"

#include <algorithm>

namespace archon {

ByteReader::ByteReader(std::span<const std::byte> data, std::string format)
    : data_(data)
    , format_(std::move(format))
{
}

std::size_t ByteReader::size() const
{
    return data_.size();
}

std::size_t ByteReader::position() const
{
    return position_;
}

Result<void> ByteReader::seek(std::size_t offset)
{
    if (offset > data_.size())
        return std::unexpected(error("seek past end of data"));
    position_ = offset;
    return {};
}

Result<std::uint64_t> ByteReader::integer(std::size_t width)
{
    const auto raw = bytes(width);
    if (!raw)
        return std::unexpected(raw.error());
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value |= std::to_integer<std::uint64_t>((*raw)[i]) << (8 * i);
    return value;
}

Result<std::uint8_t> ByteReader::u8()
{
    return integer(1).transform([](std::uint64_t value) { return static_cast<std::uint8_t>(value); });
}

Result<std::uint16_t> ByteReader::u16()
{
    return integer(2).transform([](std::uint64_t value) { return static_cast<std::uint16_t>(value); });
}

Result<std::uint32_t> ByteReader::u24()
{
    return integer(3).transform([](std::uint64_t value) { return static_cast<std::uint32_t>(value); });
}

Result<std::uint32_t> ByteReader::u32()
{
    return integer(4).transform([](std::uint64_t value) { return static_cast<std::uint32_t>(value); });
}

Result<std::uint64_t> ByteReader::u64()
{
    return integer(8);
}

Result<std::span<const std::byte>> ByteReader::bytes(std::size_t count)
{
    if (count > data_.size() - position_)
        return std::unexpected(error("read past end of data"));
    const auto result = data_.subspan(position_, count);
    position_ += count;
    return result;
}

Result<std::string> ByteReader::cString(std::size_t offset, std::size_t maxLength) const
{
    if (offset >= data_.size())
        return std::unexpected(Error { format_, offset, "string starts past end of data" });
    const auto window = data_.subspan(offset, std::min(maxLength, data_.size() - offset));
    const auto terminator = std::ranges::find(window, std::byte { 0 });
    if (terminator == window.end())
        return std::unexpected(Error { format_, offset, "string is not terminated" });
    return std::string(reinterpret_cast<const char *>(window.data()), static_cast<std::size_t>(terminator - window.begin()));
}

Error ByteReader::error(std::string message) const
{
    return Error { format_, position_, std::move(message) };
}

}
