#include "core/ytd/texture_edit.h"

#include "core/io/ascii.h"

#include <algorithm>

namespace archon {

namespace {

constexpr std::size_t maxNameLength = 63;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "ytd", 0, std::move(message) });
}

std::vector<Texture>::iterator named(std::vector<Texture> &textures, std::string_view name)
{
    return std::ranges::find_if(textures, [name](const Texture &texture) { return asciiEqualIgnoreCase(texture.name, name); });
}

}

bool isValidTextureName(std::string_view name)
{
    return !name.empty() && name.size() <= maxNameLength && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '+';
    });
}

Result<void> addTexture(std::vector<Texture> &textures, Texture texture)
{
    if (!isValidTextureName(texture.name))
        return failure("\"" + texture.name + "\" is not a valid texture name");
    if (named(textures, texture.name) != textures.end())
        return failure("a texture named " + texture.name + " already exists");
    textures.push_back(std::move(texture));
    return {};
}

Result<void> replaceTexture(std::vector<Texture> &textures, std::string_view name, Texture replacement)
{
    const auto found = named(textures, name);
    if (found == textures.end())
        return failure("no texture named " + std::string(name));
    replacement.name = found->name;
    replacement.usage = found->usage;
    *found = std::move(replacement);
    return {};
}

Result<void> removeTexture(std::vector<Texture> &textures, std::string_view name)
{
    const auto found = named(textures, name);
    if (found == textures.end())
        return failure("no texture named " + std::string(name));
    textures.erase(found);
    return {};
}

Result<void> renameTexture(std::vector<Texture> &textures, std::string_view name, std::string_view newName)
{
    const auto found = named(textures, name);
    if (found == textures.end())
        return failure("no texture named " + std::string(name));
    if (!isValidTextureName(newName))
        return failure("\"" + std::string(newName) + "\" is not a valid texture name");
    const auto clash = named(textures, newName);
    if (clash != textures.end() && clash != found)
        return failure("a texture named " + std::string(newName) + " already exists");
    found->name = newName;
    return {};
}

}
