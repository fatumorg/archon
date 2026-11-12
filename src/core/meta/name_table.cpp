#include "core/meta/name_table.h"

#include "core/crypto/hash.h"
#include "core/io/ascii.h"

#include <charconv>
#include <format>
#include <fstream>

namespace archon {

namespace {

constexpr std::string_view hashPrefix = "hash_";

bool isIdentifierStart(char character)
{
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_';
}

bool isIdentifierPart(char character)
{
    return isIdentifierStart(character) || (character >= '0' && character <= '9');
}

}

std::uint32_t joaat(std::string_view text)
{
    return jenkinsHash(std::as_bytes(std::span(text.data(), text.size())));
}

std::string hashText(std::uint32_t hash)
{
    return std::format("hash_{:08X}", hash);
}

void NameTable::add(std::string_view name)
{
    if (name.empty())
        return;
    lower_.try_emplace(joaat(asciiLower(name)), name);
    literal_.try_emplace(joaat(name), name);
}

void NameTable::harvest(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        if (!isIdentifierStart(text[index]) || (index > 0 && isIdentifierPart(text[index - 1]))) {
            ++index;
            continue;
        }
        std::size_t end = index;
        while (end < text.size() && isIdentifierPart(text[end]))
            ++end;
        if (end - index <= 128)
            add(text.substr(index, end - index));
        index = end;
    }
}

std::string NameTable::name(std::uint32_t hash) const
{
    if (const auto found = literal_.find(hash); found != literal_.end())
        return found->second;
    return value(hash);
}

std::string NameTable::value(std::uint32_t hash) const
{
    if (const auto found = lower_.find(hash); found != lower_.end())
        return found->second;
    return hashText(hash);
}

std::optional<std::uint32_t> NameTable::valueHash(std::string_view text) const
{
    if (text.empty())
        return 0;
    if (text.starts_with(hashPrefix)) {
        const std::string_view digits = text.substr(hashPrefix.size());
        std::uint32_t hash = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
        if (digits.empty() || digits.size() > 8 || error != std::errc() || end != digits.data() + digits.size())
            return std::nullopt;
        return hash;
    }
    return joaat(asciiLower(text));
}

std::size_t NameTable::size() const
{
    return lower_.size();
}

bool NameTable::save(const std::filesystem::path &file) const
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    for (const auto &[hash, name] : lower_)
        out << name << '\n';
    for (const auto &[hash, name] : literal_) {
        if (!lower_.contains(joaat(asciiLower(name))) || lower_.at(joaat(asciiLower(name))) != name)
            out << name << '\n';
    }
    return static_cast<bool>(out.flush());
}

bool NameTable::load(const std::filesystem::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
        add(line);
    return true;
}

std::filesystem::path nameTableFile(const std::filesystem::path &store)
{
    return store / "names.txt";
}

}
