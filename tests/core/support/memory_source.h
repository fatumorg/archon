#pragma once

#include "core/rpf/rpf_reader.h"

#include <memory>
#include <optional>

namespace archon::test {

class MemorySource : public RandomAccessSource {
public:
    explicit MemorySource(std::vector<std::byte> bytes, std::optional<std::uint64_t> claimedSize = std::nullopt)
        : bytes_(std::move(bytes))
        , claimedSize_(claimedSize.value_or(bytes_.size()))
    {
    }

    std::uint64_t size() const override
    {
        return claimedSize_;
    }

    Result<std::vector<std::byte>> read(std::uint64_t offset, std::size_t count) const override
    {
        if (offset > bytes_.size() || count > bytes_.size() - offset)
            return std::unexpected(Error { "memory", offset, "read past end of data" });
        return std::vector<std::byte>(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), bytes_.begin() + static_cast<std::ptrdiff_t>(offset + count));
    }

private:
    std::vector<std::byte> bytes_;
    std::uint64_t claimedSize_;
};

inline std::shared_ptr<RandomAccessSource> memorySource(std::vector<std::byte> bytes, std::optional<std::uint64_t> claimedSize = std::nullopt)
{
    return std::make_shared<MemorySource>(std::move(bytes), claimedSize);
}

}
