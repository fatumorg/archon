#pragma once

#include "core/io/error.h"

#include <span>

namespace archon {

class ByteReader {
public:
    ByteReader(std::span<const std::byte> data, std::string format);

    std::size_t size() const;
    std::size_t position() const;
    Result<void> seek(std::size_t offset);
    Result<std::uint8_t> u8();
    Result<std::uint16_t> u16();
    Result<std::uint32_t> u24();
    Result<std::uint32_t> u32();
    Result<std::uint64_t> u64();
    Result<std::span<const std::byte>> bytes(std::size_t count);
    Result<std::string> cString(std::size_t offset, std::size_t maxLength = 256) const;
    Error error(std::string message) const;

private:
    Result<std::uint64_t> integer(std::size_t width);

    std::span<const std::byte> data_;
    std::string format_;
    std::size_t position_ = 0;
};

}
