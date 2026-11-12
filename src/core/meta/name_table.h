#pragma once

#include "core/crypto/keys.h"
#include "core/io/error.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace archon {

std::uint32_t joaat(std::string_view text);

class NameTable {
public:
    void add(std::string_view name);
    void harvest(std::string_view text);
    std::string name(std::uint32_t hash) const;
    std::string value(std::uint32_t hash) const;
    std::optional<std::uint32_t> valueHash(std::string_view text) const;
    std::size_t size() const;
    bool save(const std::filesystem::path &file) const;
    bool load(const std::filesystem::path &file);

private:
    std::unordered_map<std::uint32_t, std::string> lower_;
    std::unordered_map<std::uint32_t, std::string> literal_;
};

std::string hashText(std::uint32_t hash);
std::filesystem::path nameTableFile(const std::filesystem::path &store);
Result<NameTable> harvestInstallNames(const std::filesystem::path &root, const Keys *keys, const std::atomic<bool> &cancel);

}
