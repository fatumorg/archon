#include "core/io/path_resolver.h"

#include "core/io/ascii.h"

namespace archon {

namespace {

bool isSeparator(char character)
{
    return character == '/' || character == '\\';
}

std::optional<std::filesystem::path> childIgnoringCase(const std::filesystem::path &directory, std::string_view name)
{
    std::error_code error;
    if (std::filesystem::exists(directory / name, error))
        return directory / name;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (asciiEqualIgnoreCase(it->path().filename().string(), name))
            return it->path();
    }
    return std::nullopt;
}

}

std::optional<std::filesystem::path> resolveCaseInsensitive(const std::filesystem::path &root, std::string_view relativePath)
{
    if (!relativePath.empty() && isSeparator(relativePath.front()))
        return std::nullopt;
    std::filesystem::path current = root;
    while (!relativePath.empty()) {
        const auto end = std::ranges::find_if(relativePath, isSeparator);
        const std::string_view component(relativePath.begin(), end);
        relativePath = end == relativePath.end() ? std::string_view() : std::string_view(end + 1, relativePath.end());
        if (component.empty() || component == ".")
            continue;
        if (component == ".." || component.find('\0') != std::string_view::npos)
            return std::nullopt;
        const auto child = childIgnoringCase(current, component);
        if (!child)
            return std::nullopt;
        current = *child;
    }
    std::error_code error;
    if (!std::filesystem::exists(current, error))
        return std::nullopt;
    return current;
}

}
