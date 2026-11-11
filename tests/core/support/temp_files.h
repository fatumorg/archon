#pragma once

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace archon::test {

inline std::filesystem::path pathOf(const QTemporaryDir &directory)
{
    return std::filesystem::path(directory.path().toStdString());
}

inline void writeFile(const std::filesystem::path &file, std::span<const std::byte> content)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
}

inline std::vector<std::byte> readFile(const std::filesystem::path &file)
{
    std::ifstream stream(file, std::ios::binary);
    const std::vector<char> content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(content));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

inline void writeFile(const std::filesystem::path &file, std::string_view content = {})
{
    writeFile(file, std::as_bytes(std::span(content.data(), content.size())));
}

}
